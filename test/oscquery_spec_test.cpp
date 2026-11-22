// The examples of the OSCQuery proposal (github.com/Vidvox/OSCQueryProposal), replayed against the server: the same address
// space, the same requests, and what the proposal says comes back. Ours may say more (an extension, a description); it may
// not say less or differently.

#include <pxs/QueryServer.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;
using pxs::core::Value;
using pxs::core::tree::ArgRange;

// Everything `want` says is in `have` (objects by key, arrays by position and size)
static bool includes(const QJsonValue &have, const QJsonValue &want)
{
    if (want.isObject()) {
        if (!have.isObject()) return false;
        const QJsonObject w = want.toObject(), h = have.toObject();
        for (auto it = w.begin(); it != w.end(); ++it)
            if (!h.contains(it.key()) || !includes(h.value(it.key()), it.value())) return false;
        return true;
    }
    if (want.isArray()) {
        if (!have.isArray() || have.toArray().size() != want.toArray().size()) return false;
        for (int k = 0; k < want.toArray().size(); ++k)
            if (!includes(have.toArray().at(k), want.toArray().at(k))) return false;
        return true;
    }
    if (want.isDouble() && have.isDouble()) return have.toDouble() == want.toDouble();
    return have == want;
}

static QJsonValue parse(const char *text)
{
    QJsonParseError e;
    const QJsonDocument d = QJsonDocument::fromJson(text, &e);
    if (e.error != QJsonParseError::NoError) std::fprintf(stderr, "bad JSON in the test: %s\n", text);
    return d.isObject() ? QJsonValue(d.object()) : QJsonValue(d.array());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The proposal's address space: /foo (read-only, 0-100), /bar (two ints), /baz/qux (a choice)
    core::tree::Tree tree;
    tree.root().description = "root node";
    core::tree::Node &foo = tree.add("/foo");
    foo.type = "f"; foo.access = 1; foo.description = "demonstrates a read-only OSC node- single float value ranged 0-100";
    foo.range = {ArgRange{Value(0.0), Value(100.0), {}, {}}};
    foo.get = [] { return core::ValueList{Value(0.5)}; };
    core::tree::Node &bar = tree.add("/bar");
    bar.type = "ii"; bar.access = 3; bar.description = "demonstrates a read/write OSC node- two ints with different ranges";
    bar.range = {ArgRange{Value(0), Value(50), {}, {}}, ArgRange{Value(51), Value(100), {}, {}}};
    bar.get = [] { return core::ValueList{Value(4), Value(51)}; };
    bar.set = [](const core::ValueList &) { return true; };
    tree.add("/baz").description = "simple container node, with one method- qux";
    core::tree::Node &qux = tree.add("/baz/qux");
    qux.type = "s"; qux.access = 3; qux.description = "read/write OSC node- accepts one of several string-type inputs";
    qux.range = {ArgRange{std::nullopt, std::nullopt, {Value("empty"), Value("half-full"), Value("full")}, {}}};
    qux.get = [] { return core::ValueList{Value("half-full")}; };
    qux.set = [](const core::ValueList &) { return true; };

    QueryServer server(&tree);
    server.setName("My Special Server");
    server.setOscPort(1234);
    const auto get = [&](const char *target) { return server.answer("GET", target); };
    const auto body = [&](const char *target) { return QJsonDocument::fromJson(get(target).body).object(); };

    // GET / : the whole address space
    CHECK(includes(body("/"), parse(R"({
        "DESCRIPTION": "root node", "FULL_PATH": "/", "ACCESS": 0,
        "CONTENTS": {
            "foo": {"DESCRIPTION": "demonstrates a read-only OSC node- single float value ranged 0-100", "FULL_PATH": "/foo", "ACCESS": 1,
                    "TYPE": "f", "VALUE": [0.5], "RANGE": [{"MIN": 0.0, "MAX": 100.0}]},
            "bar": {"DESCRIPTION": "demonstrates a read/write OSC node- two ints with different ranges", "FULL_PATH": "/bar", "ACCESS": 3,
                    "TYPE": "ii", "VALUE": [4, 51], "RANGE": [{"MIN": 0, "MAX": 50}, {"MIN": 51, "MAX": 100}]},
            "baz": {"DESCRIPTION": "simple container node, with one method- qux", "FULL_PATH": "/baz", "ACCESS": 0,
                    "CONTENTS": {"qux": {"DESCRIPTION": "read/write OSC node- accepts one of several string-type inputs", "FULL_PATH": "/baz/qux",
                                         "ACCESS": 3, "TYPE": "s", "VALUE": ["half-full"],
                                         "RANGE": [{"VALS": ["empty", "half-full", "full"]}]}}}}})")));
    CHECK(get("/").status == 200);
    // GET /baz : one container and what it holds
    CHECK(includes(body("/baz"), parse(R"({"FULL_PATH": "/baz", "ACCESS": 0, "CONTENTS": {"qux": {"FULL_PATH": "/baz/qux", "TYPE": "s"}}})")));
    // GET /foo?VALUE and /baz/qux?RANGE : one attribute, alone
    CHECK(body("/foo?VALUE") == parse(R"({"VALUE": [0.5]})").toObject());
    CHECK(body("/baz/qux?RANGE") == parse(R"({"RANGE": [{"VALS": ["empty", "half-full", "full"]}]})").toObject());
    // A fragment is stripped and disregarded
    CHECK(body("/foo?VALUE#nose") == parse(R"({"VALUE": [0.5]})").toObject());
    // GET /baz?TYPE : a container has no type, an empty object
    CHECK(get("/baz?TYPE").status == 200 && body("/baz?TYPE").isEmpty());
    // GET /bazzzzz?TYPE : 404
    CHECK(get("/bazzzzz?TYPE").status == 404 && get("/bazzzzz").status == 404);
    // A value asked of what cannot be read: 204 (the proposal's own case: ACCESS says it cannot)
    qux.access = 2;
    CHECK(get("/baz/qux?VALUE").status == 204);
    qux.access = 3;
    // HOST_INFO: the path of the URL is irrelevant; EXTENSIONS says, per attribute, whether it is supported
    const QJsonObject host = body("/foo?HOST_INFO");
    CHECK(host == body("/?HOST_INFO") && host["NAME"] == "My Special Server" && host["OSC_PORT"] == 1234 && host["OSC_TRANSPORT"] == "UDP");
    const QJsonObject extensions = host["EXTENSIONS"].toObject();
    CHECK(!extensions.isEmpty());
    for (auto it = extensions.begin(); it != extensions.end(); ++it) CHECK(it.value().isBool());
    for (const char *key : {"ACCESS", "VALUE", "RANGE", "DESCRIPTION"}) CHECK(extensions.value(key) == true);
    // What the server says it supports, it gives: CLIPMODE is declared, and a bounded writable parameter says it
    CHECK(extensions.value("CLIPMODE") == true);
    // The required attributes of every node: FULL_PATH always; CONTENTS only for a container; TYPE for a method
    for (const char *path : {"/foo", "/bar", "/baz", "/baz/qux"}) {
        const QJsonObject n = body(path);
        CHECK(n["FULL_PATH"] == path);
        CHECK(n.contains("CONTENTS") == (QString(path) == "/baz"));
        CHECK(n.contains("TYPE") == (QString(path) != "/baz"));
    }

    // Values and ranges follow the type tag string, arrays included, with the JSON equivalents of the proposal's table
    core::tree::Node &nested = tree.add("/nested");
    nested.type = "i[ff]i"; nested.access = 3;
    const ArgRange unit{Value(0.0), Value(1.0), {}, {}};
    nested.range = {ArgRange{Value(0), Value(100), {}, {}}, ArgRange{std::nullopt, std::nullopt, {}, {unit, unit}}, ArgRange{Value(0), Value(100), {}, {}}};
    nested.get = [] { return core::ValueList{Value(100), Value(1.0), Value(1.0), Value(100)}; };
    CHECK(includes(body("/nested"), parse(R"({"TYPE": "i[ff]i", "VALUE": [100, [1.0, 1.0], 100],
        "RANGE": [{"MIN": 0, "MAX": 100}, [{"MIN": 0.0, "MAX": 1.0}, {"MIN": 0.0, "MAX": 1.0}], {"MIN": 0, "MAX": 100}]})")));
    core::tree::Node &color = tree.add("/color");
    color.type = "r"; color.access = 3;
    color.get = [] { return core::ValueList{Value(std::int64_t(0xFA6432FFLL))}; };
    CHECK(body("/color?VALUE") == parse(R"({"VALUE": ["#FA6432FF"]})").toObject());
    core::tree::Node &blob = tree.add("/misc");
    blob.type = "bsTF"; blob.access = 1;
    blob.get = [] { return core::ValueList{Value(), Value("s"), Value(true), Value(false)}; };
    CHECK(body("/misc?VALUE") == parse(R"({"VALUE": [null, "s", true, false]})").toObject());

    // T and F are used interchangeably: an F sent to a method of type T is accepted
    bool on = true;
    core::tree::Node &sw = tree.add("/sw");
    sw.type = "T"; sw.access = 3;
    sw.get = [&] { return core::ValueList{Value(on)}; };
    sw.set = [&](const core::ValueList &a) { on = !a.empty() && a.front().truthy(); return true; };
    CHECK(tree.apply("/sw", {Value(false)}) == 1 && !on);
    CHECK(tree.apply("/sw", {Value(true)}) == 1 && on);

    // Errors: the HTTP status that fits
    CHECK(server.answer("POST", "/").status == 405);
    CHECK(server.answer("GET", "/foo?NOPE").status == 204);
    return failures ? 1 : 0;
}
