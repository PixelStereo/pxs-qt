// The OSCQuery client: the tree of a device, its values (polled: this server does not push), writes over OSC, discovery's packets,
// and a control that is a view of a remote parameter.
#include <pxs/Convert.h>
#include <pxs/core/tree/oscquery.h>
#include <pxs/Client.h>
#include <pxs/Discovery.h>
#include <pxs/MdnsResponder.h>
#include <pxs/OscService.h>
#include <pxs/RemoteSource.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QElapsedTimer>
#ifdef PXS_HAS_WEBSOCKETS
#include <QWebSocket>
#include <QWebSocketServer>
#endif
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;
using namespace pxs::oscquery;

// Returns as soon as the condition holds: a long limit costs nothing when it does, and shared CI runners can be slow
static void pump(const std::function<bool()> &until, int ms = 12000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // The tree of a device
    core::tree::Tree tree;
    double level = 0.25;
    bool on = false;
    int cue = -1;
    auto p = std::make_unique<core::tree::Parameter>("level", "Level", core::tree::Parameter::Type::Float);
    p->range(-6, 6).bind([&] { return core::Value(level); }, [&](const core::Value &v) { level = v.number().value_or(0.0); });
    tree.add("/mix/level", std::move(p));
    auto q = std::make_unique<core::tree::Parameter>("on", "On", core::tree::Parameter::Type::Bool);
    q->bind([&] { return core::Value(on); }, [&](const core::Value &v) { on = v.truthy(); });
    tree.add("/mix/on", std::move(q));
    tree.addAction("/show/go", "i", [&](const core::ValueList &a) { cue = int(a.front().number().value_or(-1)); return true; });
    OscService service(&tree, "Fake");
    CHECK(service.start(0, 0, false));

    // RemoteTree reads what Tree says
    RemoteTree remote;
    CHECK(!remote.load(QJsonObject{{"nope", 1}}));
    CHECK(remote.load(convert::jsonObjectFromText(core::tree::describeJson(tree))));
    { // a device that says how a parameter is reset (NYXMAP_RESET)
        pxs::oscquery::RemoteTree withReset;
        const QJsonObject doc{{"FULL_PATH", "/"}, {"CONTENTS", QJsonObject{{"a", QJsonObject{{"FULL_PATH", "/a"}, {"TYPE", "f"}, {"ACCESS", 3}, {"NYXMAP_RESET", "/a/reset"},
                                       {"CONTENTS", QJsonObject{{"reset", QJsonObject{{"FULL_PATH", "/a/reset"}, {"TYPE", "N"}, {"ACCESS", 2}}}}}}}}}};
        CHECK(withReset.load(doc) && withReset.find("/a") && withReset.find("/a")->resetPath == "/a/reset" && withReset.find("/a/reset"));
    }
    { // what the proposal lets a device write: one value for every argument, a null for "none", units and what an argument is
        pxs::oscquery::RemoteTree shorthand;
        const QJsonObject doc{{"FULL_PATH", "/"}, {"CONTENTS", QJsonObject{{"p", QJsonObject{{"FULL_PATH", "/p"}, {"TYPE", "ff"}, {"ACCESS", 3},
                              {"RANGE", QJsonObject{{"MIN", 0}, {"MAX", 1}}}, {"UNIT", "distance.m"}, {"VALUE", QJsonArray{0.5, 0.25}},
                              {"EXTENDED_TYPE", QJsonArray{"position.cartesian.x", QJsonValue()}}, {"TAGS", QJsonArray{"a"}},
                              {"DEFAULT_VALUE", QJsonArray{0.5, 0.5}}}}}}};
        CHECK(shorthand.load(doc));
        const RemoteNode *p = shorthand.find("/p");
        CHECK(p && p->range.size() == 2 && p->range.at(1).toObject().value("MAX").toInt() == 1 && p->unit == QStringList({"distance.m", "distance.m"}));
        CHECK(p->extendedType == QStringList({"position.cartesian.x", ""}) && p->tags == QStringList{"a"} && p->defaultValue.size() == 2);
    }
    const RemoteNode *level_ = remote.find("/mix/level");
    CHECK(level_ && level_->type == "f" && level_->access == 3 && level_->value.at(0).toDouble() == 0.25);
    core::tree::ParamInfo info;
    CHECK(level_->toParamInfo(&info) && info.type == core::tree::ParamInfo::Type::Float && info.min == -6 && info.max == 6);
    CHECK(remote.find("/mix/on")->toParamInfo(&info) && info.type == core::tree::ParamInfo::Type::Bool);
    CHECK(remote.find("/show/go")->toParamInfo(&info) && info.type == core::tree::ParamInfo::Type::Int);
    CHECK(!remote.find("/mix")->toParamInfo(&info) && !remote.find("/nowhere"));
    CHECK(remote.leaves() == QStringList({"/mix/level", "/mix/on", "/show/go"}));

    // The client: tree, polled values, writes
    Client client;
    bool ready = false;
    QString failure;
    QObject::connect(&client, &Client::ready, [&] { ready = true; });
    QObject::connect(&client, &Client::failed, [&](const QString &r) { failure = r; });
    client.open("127.0.0.1", service.httpPort());
    pump([&] { return ready || !failure.isEmpty(); }, 12000); // a slow runner (Windows CI): the tree comes after the host info
    if (!ready) std::fprintf(stderr, "client not ready: failure='%s' state=%d name='%s' oscPort=%d http=%d\n", qPrintable(failure), int(client.state()), qPrintable(client.name()), int(client.oscPort()), int(service.httpPort()));
    CHECK(ready && failure.isEmpty());
    CHECK(client.name() == "Fake" && client.oscPort() == service.oscPort());
    CHECK(client.tree().find("/mix/level") != nullptr);

    QJsonArray heard;
    QObject::connect(&client, &Client::valueChanged, [&](const QString &path, const QJsonArray &v) { if (path == "/mix/level") heard = v; });
    client.listen("/mix/level");
    pump([&] { return !heard.isEmpty(); });
    CHECK(!heard.isEmpty() && heard.at(0).toDouble() == 0.25);
    level = 1.5; // the device changes it itself
    heard = QJsonArray();
    pump([&] { return !heard.isEmpty(); });
    CHECK(!heard.isEmpty() && heard.at(0).toDouble() == 1.5);

    CHECK(client.write("/mix/level", QJsonArray{-2.0}));
    pump([&] { return level == -2.0; });
    CHECK(level == -2.0);
    CHECK(client.write("/mix/on", QJsonArray{true}));
    pump([&] { return on; });
    CHECK(on);
    CHECK(client.write("/show/go", QJsonArray{3}));
    pump([&] { return cue == 3; });
    CHECK(cue == 3);
    // The device's tree changes: the client says so, once
    int changes = 0;
    QObject::connect(&client, &Client::treeChanged, [&] { ++changes; });
    client.refresh();
    pump([&] { return false; }, 300);
    CHECK(changes == 0); // the same tree
    auto extra = std::make_unique<core::tree::Parameter>("extra", "Extra", core::tree::Parameter::Type::Float);
    extra->range(0, 1).bind([] { return core::Value(0.0); }, [](const core::Value &) {});
    tree.add("/mix/extra", std::move(extra));
    client.refresh();
    pump([&] { return changes > 0; });
    CHECK(changes == 1 && client.tree().find("/mix/extra"));
    // This device says LISTEN (OscService serves the WebSocket): what it changes is pushed, and a node it makes is told with PATH_CHANGED
    CHECK(client.pushes());
    level = 2.5;
    heard = QJsonArray();
    tree.changed("/mix/level");
    pump([&] { return !heard.isEmpty(); });
    CHECK(!heard.isEmpty() && heard.at(0).toDouble() == 2.5);
    auto extra2 = std::make_unique<core::tree::Parameter>("extra2", "Extra 2", core::tree::Parameter::Type::Float);
    extra2->range(0, 1).bind([] { return core::Value(0.0); }, [](const core::Value &) {});
    tree.add("/mix/extra2", std::move(extra2));
    pump([&] { return changes > 1; }, 1500); // before the client's own look at the tree (every 3 s), no refresh() asked
    CHECK(changes == 2 && client.tree().find("/mix/extra2"));
    CHECK(!client.write("/nowhere", QJsonArray{1}) && !client.write("/mix/level", QJsonArray{1, 2}));

    // A control that is a view of the remote parameter
    RemoteSource source(&client);
    const auto view = controls::forParameter(source, 0, "/mix/level", 0);
    CHECK(view && view->kind == controls::Control::Kind::Slider && view->min == -6 && view->max == 6 && view->boundParam == "/mix/level");
    controls::Control c = *view;
    CHECK(controls::setBound(c, source, 4.0));
    pump([&] { return level == 4.0; });
    CHECK(level == 4.0);
    controls::fillBound(c, source);
    CHECK(c.value == 4.0);
    CHECK(controls::forParameter(source, 0, "/mix/on", 0)->kind == controls::Control::Kind::Toggle);


#ifdef PXS_HAS_WEBSOCKETS
    // A device that pushes, as NyxMap does: HOST_INFO says LISTEN and WS_PORT; LISTEN over the WebSocket, values come back as binary OSC
    {
        QWebSocketServer wss("fake", QWebSocketServer::NonSecureMode);
        CHECK(wss.listen(QHostAddress::LocalHost, 0));
        QStringList listened;
        QWebSocket *peer = nullptr;
        QObject::connect(&wss, &QWebSocketServer::newConnection, [&] {
            peer = wss.nextPendingConnection();
            QObject::connect(peer, &QWebSocket::textMessageReceived, [&](const QString &t) {
                const QJsonObject o = QJsonDocument::fromJson(t.toUtf8()).object();
                if (o.value("COMMAND").toString() == "LISTEN") listened << o.value("DATA").toString();
            });
        });
        service.query().setExtensions(QJsonObject{{"ACCESS", true}, {"VALUE", true}, {"RANGE", true}, {"LISTEN", true}});
        service.query().setHostInfoMore(QJsonObject{{"WS_PORT", int(wss.serverPort())}});
        Client pushed;
        bool up = false;
        QObject::connect(&pushed, &Client::ready, [&] { up = true; });
        pushed.open("127.0.0.1", service.httpPort());
        pump([&] { return up && peer && pushed.pushes(); });
        CHECK(up && peer && pushed.pushes());
        QJsonArray got;
        QObject::connect(&pushed, &Client::valueChanged, [&](const QString &path, const QJsonArray &v) { if (path == "/mix/level" && v.at(0).toDouble() > 5) got = v; });
        pushed.listen("/mix/level");
        pump([&] { return listened.contains("/mix/level"); });
        CHECK(listened.contains("/mix/level"));
        osc::Message m;
        m.address = "/mix/level";
        m.types = "f";
        m.args << 7.0;
        peer->sendBinaryMessage(osc::encode(m));
        pump([&] { return !got.isEmpty(); });
        CHECK(!got.isEmpty() && got.at(0).toDouble() == 7.0);
        CHECK(pushed.tree().find("/mix/level")->value.at(0).toDouble() == 7.0);
    }
#endif

    // A device by its name, not its number
    {
        Client named;
        bool up = false;
        QObject::connect(&named, &Client::ready, [&] { up = true; });
        named.open("localhost", service.httpPort());
        pump([&] { return up; });
        CHECK(up);
    }

    // An unreachable device fails, once
    Client nobody;
    QString why;
    QObject::connect(&nobody, &Client::failed, [&](const QString &r) { why = r; });
    nobody.open("127.0.0.1", 1);
    // A runner's network stack may drop the connection to a closed port instead of refusing it (macOS and Windows runners
    // did): the client gives up on its own transfer timeout (5 s), so wait longer than that
    pump([&] { return !why.isEmpty(); }, 8000);
    CHECK(!why.isEmpty() && nobody.state() == Client::State::Failed);

    // OSC messages as a device's own arguments
    RemoteNode node;
    node.path = "/x";
    node.type = "fT";
    const osc::Message m = Client::toMessage(node, QJsonArray{0.5, 0});
    CHECK(m.types == "fF" && m.args.size() == 2);
    CHECK(Client::toJson(m) == QJsonArray({0.5, false}));

    // Discovery reads an announcement, and a goodbye
    Discovery discovery;
    QList<Device> found, lost;
    QObject::connect(&discovery, &Discovery::deviceFound, [&](const Device &d) { found << d; });
    QObject::connect(&discovery, &Discovery::deviceLost, [&](const Device &d) { lost << d; });
    { // an instance name with dots stays one label (it is not a domain name), and the host is not the operating system's own
        const QByteArray dotted = MdnsResponder::buildResponse("My.App 1.2", "studio", {{"_oscjson._tcp", 5678}}, {}, 120);
        CHECK(dotted.contains(QByteArray("\x0aMy.App 1.2\x08_oscjson", 19)));
    }
    const QByteArray hello = MdnsResponder::buildResponse("Fake (studio)", "studio", {{"_oscjson._tcp", 5678}}, {QHostAddress("192.168.1.20")}, 120);
    discovery.handle(hello);
    CHECK(found.size() == 1 && found[0].name == "Fake (studio)" && found[0].port == 5678 && found[0].address == QHostAddress("192.168.1.20"));
    discovery.handle(hello);
    CHECK(found.size() == 1); // the same again: nothing new
    CHECK(discovery.devices().size() == 1);
    discovery.handle(MdnsResponder::buildResponse("Fake (studio)", "studio", {{"_oscjson._tcp", 5678}}, {}, 0));
    CHECK(lost.size() == 1 && discovery.devices().isEmpty());
    CHECK(Discovery::parse(QByteArray(7, 'x')).isEmpty() && Discovery::parse(Discovery::query("_oscjson._tcp")).isEmpty());
    QByteArray junk = hello;
    for (int i = 12; i < junk.size(); i += 7) junk[i] = char(0xC0); // pointers into nowhere: refused, no hang
    Discovery::parse(junk);

    if (!failures) std::printf("oscclient: ok\n");
    return failures ? 1 : 0;
}
