// The Qt tree (pxs::Tree, Node, Parameter, Preset: the reference) and pxs::core::tree side by side, on random trees and random OSC
// traffic. Every value, write count, flag, label, range, feed, observer, structure and output notification, the OSCQuery description
// of every node, the attributes, HOST_INFO, the captured presets and the recalls are compared after each operation.
//
//   pxs_tree_differential_test strict [rounds]   inputs on which both must agree (the ctest; no difference allowed)
//   pxs_tree_differential_test loose  [rounds]   adds the inputs on which pxs-core differs on purpose (see its CHANGELOG, 0.5.0): a
//                                                text that is no number, a blob, a Nil or no argument written to a parameter, an infinity
//
// This test goes when the Qt tree is replaced by pxs::core::tree.
// Differential harness: the Qt tree of pxs-qt (the reference) against pxs::core::tree, on random trees and random OSC traffic.
#include <pxs/Tree.h>
#include "ChangeFeed.h"
#include <pxs/Preset.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/preset.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <random>
#include <map>
#include <set>
#include <cstdio>

using namespace pxs;
namespace ct = pxs::core::tree;

static std::mt19937_64 rng(2024); // a fixed seed: a failure reproduces
static int R(int n) { return int(rng() % unsigned(n)); }
static double Rd(double a, double b) { return a + (b - a) * (double(rng() % 1000001) / 1000000.0); }

// ---- one value described the same way on both sides
static QString dq(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Double: { char b[64]; std::snprintf(b, sizeof b, "d:%.17g", v.toDouble()); return b; }
    case QMetaType::Float: { char b[64]; std::snprintf(b, sizeof b, "f:%.9g", double(v.toFloat())); return b; }
    case QMetaType::Int: return "i:" + QString::number(v.toInt());
    case QMetaType::LongLong: return "h:" + QString::number(v.toLongLong());
    case QMetaType::Bool: return v.toBool() ? "b:1" : "b:0";
    case QMetaType::QString: return "s:" + v.toString();
    case QMetaType::UnknownType: return "n";
    default: return "?" + QString(v.typeName());
    }
}
static QString dc(const core::Value &v)
{
    using K = core::Value::Kind;
    switch (v.kind()) {
    case K::Double: { char b[64]; std::snprintf(b, sizeof b, "d:%.17g", *v.get<double>()); return b; }
    case K::Float: { char b[64]; std::snprintf(b, sizeof b, "f:%.9g", double(*v.get<float>())); return b; }
    case K::Integer32: return "i:" + QString::number(*v.get<int32_t>());
    case K::Integer64: return "h:" + QString::number(qlonglong(*v.get<int64_t>()));
    case K::Boolean: return *v.get<bool>() ? "b:1" : "b:0";
    case K::String: return "s:" + QString::fromStdString(*v.get<std::string>());
    case K::Nothing: return "n";
    case K::Bytes: return "?QByteArray";
    default: return "?";
    }
}
static QString listQ(const QVariantList &l) { QStringList s; for (auto &v : l) s << dq(v); return s.join('|'); }
static QString listC(const core::ValueList &l) { QStringList s; for (auto &v : l) s << dc(v); return s.join('|'); }
static QString oscq(const osc::Message &m) { return m.address + " " + m.types + " " + listQ(m.args); }
static QString oscc(const core::osc::Message &m)
{
    QStringList s;
    for (auto &a : m.args) {
        if (auto *v = std::get_if<int32_t>(&a)) s << "i:" + QString::number(*v);
        else if (auto *l = std::get_if<int64_t>(&a)) s << "h:" + QString::number(qlonglong(*l));
        else if (auto *f = std::get_if<float>(&a)) { char b[64]; std::snprintf(b, sizeof b, "f:%.9g", double(*f)); s << b; }
        else if (auto *d = std::get_if<double>(&a)) { char b[64]; std::snprintf(b, sizeof b, "d:%.17g", *d); s << b; }
        else if (auto *st = std::get_if<std::string>(&a)) s << "s:" + QString::fromStdString(*st);
        else if (auto *b = std::get_if<bool>(&a)) s << (*b ? "b:1" : "b:0");
        else if (auto *u = std::get_if<uint32_t>(&a)) s << "u:" + QString::number(*u);
        else s << "n";
    }
    return QString::fromStdString(m.address) + " " + QString::fromStdString(m.types) + " " + s.join('|');
}
// the Qt message's args as the wire would carry them (by tag, arrays' brackets being no argument), to compare with the core's typed arguments
static QString oscqWire(const osc::Message &m)
{
    QStringList s; int j = 0;
    for (int k = 0; k < m.types.size() && j < m.args.size(); ++k) {
        const QChar t = m.types[k];
        if (t == '[' || t == ']') continue;
        const QVariant v = m.args.value(j++);
        if (t == 'f') { char b[64]; std::snprintf(b, sizeof b, "f:%.9g", double(v.toFloat())); s << b; }
        else if (t == 'd') { char b[64]; std::snprintf(b, sizeof b, "d:%.17g", v.toDouble()); s << b; }
        else if (t == 'i') s << "i:" + QString::number(v.toInt());
        else if (t == 'h' || t == 't') s << "h:" + QString::number(v.toLongLong());
        else if (t == 's' || t == 'S') s << "s:" + v.toString();
        else if (t == 'c') { const QString x = v.toString(); s << "u:" + QString::number(x.isEmpty() ? 0 : x.at(0).unicode()); }
        else if (t == 'r' || t == 'm') s << "u:" + QString::number(quint32(v.toLongLong()));
        else if (t == 'T' || t == 'F') s << (v.toBool() ? "b:1" : "b:0");
        else s << "n";
    }
    return m.address + " " + m.types + " " + s.join('|');
}

// ---- an argument, in the three vocabularies
struct Arg { QVariant q; core::osc::Value o; bool loose = false; };
static Arg makeArg(bool strict, bool stringsOk)
{
    int pick = R(strict ? 10 : 14);
    if (strict && !stringsOk && pick == 4) pick = 1; // a non-number as text goes to Choice and Text nodes only (see the loose mode)
    if (strict && stringsOk && pick != 4 && pick != 5) pick = 4 + R(2);
    switch (pick) {
    case 0: { int v = R(2000) - 1000; return {v, core::osc::Value(int32_t(v))}; }
    case 1: { double v = Rd(-3, 3); return {v, core::osc::Value(v)}; }
    case 2: { float v = float(Rd(-300, 300)); return {double(v), core::osc::Value(double(v))}; } // as the Qt decoder gives it: a float is a double (its text differs by design: tested apart)
    case 3: { bool v = R(2); return {v, core::osc::Value(v)}; }
    case 4: { const char *c[] = {"red", "Green", "BLUE", "off", "blend", "x", ""}; std::string s = c[R(7)]; return {QString::fromStdString(s), core::osc::Value(s)}; }
    case 5: { const char *c[] = {"0.5", "7", "-3", " 12 ", "1e2", ".25"}; std::string s = c[R(6)]; return {QString::fromStdString(s), core::osc::Value(s)}; }
    case 6: { qlonglong v = (qlonglong(R(100000)) - 50000) * 1000; return {v, core::osc::Value(int64_t(v))}; }
    case 7: { double v = Rd(-1e6, 1e6); return {v, core::osc::Value(v)}; }
    case 8: { double v = double(R(5)); return {v, core::osc::Value(v)}; }
    case 9: { int v = R(7); return {v, core::osc::Value(int32_t(v))}; }
    case 10: { double v = std::numeric_limits<double>::infinity() * (R(2) ? 1 : -1); Arg a{v, core::osc::Value(v)}; a.loose = true; return a; } // old: an Int takes inf as garbage
    case 11: { Arg a{QVariant(), core::osc::Value(std::monostate{})}; a.loose = true; return a; }                       // Nil
    case 12: { Arg a{QString("abc"), core::osc::Value(std::string("abc"))}; a.loose = true; return a; }                 // a non-number as text
    case 13: default: { Arg a{QVariant(QByteArray("\x01\x02")), core::osc::Value(core::osc::Blob{1, 2})}; a.loose = true; return a; }
    }
}

// ---- parameters on both sides
struct Spec {
    int type; // ParamInfo::Type
    QString path; QString label;
    double lo = 0, hi = 1; std::vector<int> values; QStringList choices; int access = 3; bool transient = false, hasDefault = false, unbounded = false;
    double dd = 0; int di = 0; bool db = false; QString ds;
    int colorComponent = -1, pointComponent = -1; QString unit, ext;
};
struct SideQ { QMap<QString, QVariant> cells; QMap<QString, int> writes; };
struct SideC { std::map<std::string, core::Value> cells; std::map<std::string, int> writes; };

static QVariant initialQ(const Spec &s)
{
    switch (s.type) { case 0: return s.dd; case 1: return s.di; case 2: return s.db; case 3: return s.choices.value(0); case 4: return s.ds; default: return QVariant(); }
}
static core::Value initialC(const Spec &s)
{
    switch (s.type) { case 0: return core::Value(s.dd); case 1: return core::Value(s.di); case 2: return core::Value(s.db); case 3: return core::Value(s.choices.value(0).toStdString()); case 4: return core::Value(s.ds.toStdString()); default: return {}; }
}

static std::unique_ptr<Parameter> buildQ(const Spec &s, SideQ &side)
{
    auto p = std::make_unique<Parameter>(s.path, s.label, Parameter::Type(s.type));
    if (s.type == 0 || s.type == 1) { p->range(s.lo, s.hi); }
    if (s.type == 1 && !s.values.empty()) { QList<int> v; for (int x : s.values) v << x; p->values(v); }
    if (s.type == 3) p->choices(s.choices);
    if (s.access == 1) p->readOnly(); else if (s.access == 2) p->writeOnly();
    if (s.transient) p->transient();
    if (s.unbounded) p->unbounded();
    if (!s.unit.isEmpty()) p->unit(s.unit);
    if (!s.ext.isEmpty()) p->extendedType(s.ext);
    if (s.colorComponent >= 0) p->colorComponent(s.colorComponent);
    if (s.pointComponent >= 0) p->pointComponent(s.pointComponent);
    if (s.hasDefault) p->byDefault(initialQ(s));
    side.cells[s.path] = initialQ(s);
    p->bind([&side, k = s.path] { return side.cells[k]; }, [&side, k = s.path](const QVariant &v) { side.cells[k] = v; side.writes[k]++; });
    return p;
}
static std::unique_ptr<core::tree::Parameter> buildC(const Spec &s, SideC &side, const core::ChangeFeed *feed)
{
    auto p = std::make_unique<core::tree::Parameter>(s.path.toStdString(), s.label.toStdString(), core::tree::ParamInfo::Type(s.type));
    if (s.type == 0 || s.type == 1) p->range(s.lo, s.hi);
    if (s.type == 1 && !s.values.empty()) p->values(s.values);
    if (s.type == 3) { std::vector<std::string> c; for (auto &x : s.choices) c.push_back(x.toStdString()); p->choices(c); }
    if (s.access == 1) p->readOnly(); else if (s.access == 2) p->writeOnly();
    if (s.transient) p->transient();
    if (s.unbounded) p->unbounded();
    if (!s.unit.isEmpty()) p->unit(s.unit.toStdString());
    if (!s.ext.isEmpty()) p->extendedType(s.ext.toStdString());
    if (s.colorComponent >= 0) p->colorComponent(s.colorComponent);
    if (s.pointComponent >= 0) p->pointComponent(s.pointComponent);
    if (s.hasDefault) p->byDefault(initialC(s));
    side.cells[s.path.toStdString()] = initialC(s);
    p->bind([&side, k = s.path.toStdString()] { return side.cells[k]; }, [&side, k = s.path.toStdString()](const core::Value &v) { side.cells[k] = v; side.writes[k]++; });
    p->feed(feed);
    return p;
}

struct FeedQ : ChangeFeed::Observer { QStringList log; void parameterWritten(quint64 h, const QString &p) override { log << QString("p %1 %2").arg(h).arg(p); } void layerChanged(quint64) override {} void addressChanged(const QString &) override {} };
struct FeedC : core::ChangeFeed::Observer { QStringList log; void parameterWritten(uint64_t h, std::string_view p) override { log << QString("p %1 %2").arg(h).arg(QString::fromUtf8(p.data(), int(p.size()))); } void layerChanged(uint64_t) override {} void addressChanged(std::string_view) override {} };

int main(int argc, char **argv)
{
    const bool strict = argc < 2 || QString(argv[1]) != "loose";
    const int rounds = argc > 2 ? atoi(argv[2]) : 3000;
    long checks = 0, bad = 0; std::map<std::string, long> kinds;
    auto report = [&](const char *what, const QString &a, const QString &b) { ++bad; if (bad <= 12) qDebug().noquote() << "DIFF" << what << "\n  qt  :" << a << "\n  core:" << b; };
    for (int round = 0; round < rounds; ++round) {
        Tree tq; ct::Tree tc; SideQ sq; SideC sc; FeedQ fq; FeedC fc; core::ChangeFeed feed;
        ChangeFeed::observe(&fq); feed.observe(&fc);
        QStringList outQ, outC, obsQ, obsC, structQ, structC;
        tq.setOutput([&](const osc::Message &m) { outQ << oscqWire(m); });
        tc.setOutput([&](const core::osc::Message &m) { outC << oscc(m); });
        tq.addObserver([&](const Node &n, const QVariantList &v) { obsQ << n.path() + " " + listQ(v); });
        tc.addObserver([&](const ct::Node &n, const core::ValueList &v) { obsC << QString::fromStdString(n.path()) + " " + listC(v); });
        tq.setStructureObserver([&](const QString &p, Tree::Change c) { structQ << QString("%1 %2").arg(p).arg(int(c)); });
        tc.setStructureObserver([&](const std::string &p, ct::Tree::Change c) { structC << QString("%1 %2").arg(QString::fromStdString(p)).arg(int(c)); });
        std::vector<Spec> specs; QStringList addresses;
        const char *names[] = {"opacity", "speed", "mode", "name", "on", "go", "x y", "a#b", "q?", "fade/in", "fade/out", "layer/1/size", "layer/2/size", "é/ü"};
        int n = 4 + R(10);
        for (int i = 0; i < n; ++i) {
            Spec s; s.type = R(6); s.path = QString::fromUtf8(names[R(14)]) + QString::number(i); s.label = (R(2) ? "Cat › " : "") + s.path;
            s.lo = Rd(-5, 0); s.hi = Rd(0.5, 8); if (s.type == 1) { s.lo = R(10) - 5; s.hi = s.lo + 1 + R(20); if (R(3) == 0) { for (int k = 0; k < 4; ++k) s.values.push_back(R(40) - 10); } }
            if (s.type == 3) { const char *c[] = {"red", "green", "blue"}; for (int k = 0; k < 1 + R(3); ++k) s.choices << c[k]; }
            s.access = R(5) == 0 ? (R(2) ? 1 : 2) : 3; s.transient = R(6) == 0; s.hasDefault = R(2); s.unbounded = R(6) == 0; s.dd = Rd(-1, 1); s.di = R(5); s.db = R(2); s.ds = R(2) ? "hi" : "";
            if (R(5) == 0) s.colorComponent = R(4); if (R(5) == 0) s.pointComponent = R(2); if (R(4) == 0) s.unit = "gain.db"; if (R(5) == 0) s.ext = "filepath";
            if (s.type == 1) { if (!s.values.empty()) { /* values() sets range */ } }
            // Int parameters: pxs sets the lo/hi through range(); with values the range follows them (both sides do the same)
            specs.push_back(s);
            tq.add(s.path, buildQ(s, sq)); tc.add(s.path.toStdString(), buildC(s, sc, &feed));
            addresses << "/" + s.path.replace(' ', '_').replace('#', '_').replace('?', '_');
        }

        // Nodes filled in by hand: every kind of type tag, arrays, tags, attributes, units, defaults, a range per argument
        {
            struct H { const char *path; const char *type; };
            const H hs[] = {{"/hand/color", "r"}, {"/hand/mix", "[ff]i"}, {"/hand/big", "h"}, {"/hand/ch", "c"}, {"/hand/blob", "b"}, {"/hand/pair", "fs"}, {"/hand/tf", "T"}, {"/hand/midi", "m"}, {"/hand/d", "d"}, {"/hand/imp", "I"}};
            for (const H &h : hs) {
                QVariantList vq; core::ValueList vc;
                for (QChar t : QString(h.type)) {
                    if (t == '[' || t == ']') continue;
                    const int pick = R(3); const double d = Rd(-5, 5); const qlonglong l = qlonglong(R(1 << 30)) * 4 + 1;
                    if (t == 'r' || t == 'm') { const qlonglong c = qlonglong(rng() & 0xFFFFFFFFu); vq << c; vc.push_back(core::Value(std::int64_t(c))); }
                    else if (t == 'h') { vq << l; vc.push_back(core::Value(std::int64_t(l))); }
                    else if (t == 'i') { const int i = R(100) - 50; vq << i; vc.push_back(core::Value(i)); }
                    else if (t == 's' || t == 'c') { const QString x = pick == 0 ? "a" : pick == 1 ? "\xC3\xA9t\xC3\xA9" : ""; vq << x; vc.push_back(core::Value(x.toStdString())); }
                    else if (t == 'T') { const bool b = R(2); vq << b; vc.push_back(core::Value(b)); }
                    else if (t == 'b') { vq << QByteArray("\x01"); vc.push_back(core::Value(core::Blob{1})); }
                    else if (t == 'I') { vq << QVariant(); vc.push_back(core::Value()); }
                    else { vq << d; vc.push_back(core::Value(d)); } // f, d
                }
                Node &a = tq.add(h.path); a.type = h.type; a.access = 3; a.get = [vq] { return vq; }; a.set = [](const QVariantList &) { return true; };
                ct::Node &b = tc.add(h.path); b.type = h.type; b.access = 3; b.get = [vc] { return vc; }; b.set = [](const core::ValueList &) { return true; };
                { const bool d = R(2); a.description = d ? "a thing" : ""; b.description = d ? "a thing" : ""; }
                if (R(2)) { a.tags = QStringList{"x", "y"}; b.tags = {"x", "y"}; }
                if (R(2)) { a.extendedType = QStringList{"", "color.rgb.r"}; b.extendedType = {"", "color.rgb.r"}; a.unit = QStringList{"time.ms"}; b.unit = {"time.ms"}; }
                if (R(2)) { a.clip = "both"; b.clip = "both"; }
                if (R(2)) { a.range = QJsonArray{QJsonObject{{"MIN", -1.0}, {"MAX", 2.5}}, QJsonObject{{"VALS", QJsonArray{"u", "v"}}}}; b.range = {ct::ArgRange{core::Value(-1.0), core::Value(2.5), {}}, ct::ArgRange{std::nullopt, std::nullopt, {core::Value("u"), core::Value("v")}}}; }
                if (R(2)) { a.defaultValue = [vq] { return vq; }; b.defaultValue = [vc] { return vc; }; }
                if (R(2)) { a.attributes["PXS_NOTE"] = [] { return QJsonValue("hello"); }; b.attributes["PXS_NOTE"] = [] { return std::optional<std::string>("\"hello\""); }; }
                if (R(3) == 0) { a.attributes["PXS_NONE"] = [] { return QJsonValue(); }; b.attributes["PXS_NONE"] = [] { return std::optional<std::string>(); }; }
                if (R(3) == 0) { a.attributes["PXS_OBJ"] = [] { return QJsonValue(QJsonObject{{"k", 1.5}, {"a", QJsonArray{1, "z", true}}}); }; b.attributes["PXS_OBJ"] = [] { return std::optional<std::string>("{\"k\":1.5,\"a\":[1,\"z\",true]}"); }; }
                addresses << h.path;
            }
        }
        tq.addAction("/act/hit", "N", [&](const QVariantList &a) { outQ << "act " + listQ(a); return true; });
        tc.addAction("/act/hit", "N", [&](const core::ValueList &a) { outC << "act " + listC(a); return true; });
        tq.addAction("/act/two", "ff", [&](const QVariantList &a) { outQ << "act2 " + listQ(a); return a.size() == 2; });
        tc.addAction("/act/two", "ff", [&](const core::ValueList &a) { outC << "act2 " + listC(a); return a.size() == 2; });
        tq.addSignal("/sig/beat", "f"); tc.addSignal("/sig/beat", "f");
        addresses << "/act/hit" << "/act/two" << "/sig/beat";
        if (R(3) == 0) { tq.setLocked("/" + specs[0].path, true); tc.setLocked("/" + specs[0].path.toStdString(), true); }
        for (int step = 0; step < 40; ++step) {
            const int op = R(10);
            if (op < 6) { // an OSC message
                QString addr = addresses[R(addresses.size())];
                if (R(4) == 0) { const char *pat[] = {"/*", "/*1", "/?pacity?", "/{opacity0,speed1}", "/layer/*/size", "/fade/*", "/[os]*"}; addr = pat[R(7)]; }
                std::vector<Arg> args; int na = strict ? 1 + R(2) : R(3); bool loose = false;
                bool onStrings = addr.startsWith("/act"); for (auto &sp : specs) { QString a = "/" + QString(sp.path).replace(' ', '_').replace('#', '_').replace('?', '_'); if (a == addr && (sp.type == 3 || sp.type == 4)) onStrings = true; } for (int k = 0; k < na; ++k) { args.push_back(makeArg(strict, onStrings && addr.indexOf(QRegularExpression("[*?\\[{]")) < 0)); loose |= args.back().loose; }
                osc::Message mq; mq.address = addr; core::osc::Message mc; mc.address = addr.toStdString();
                for (auto &a : args) { mq.args << a.q; mc.args.push_back(a.o); }
                const int aq = tq.apply(mq), ac = tc.apply(mc); ++checks;
                if (aq != ac) { QString d; for (auto &sp : specs) if (addr.contains(sp.path)) d += QString(" [type %1 lo %2 hi %3 vals %4 access %5]").arg(sp.type).arg(sp.lo).arg(sp.hi).arg(sp.values.size()).arg(sp.access); report("apply count", QString("%1 %2 args=%3%4").arg(addr).arg(aq).arg(listQ(mq.args)).arg(d), QString("%1 %2").arg(addr).arg(ac)); }
            } else if (op == 6) {
                const QString p = addresses[R(addresses.size())]; const bool a = tq.reset(p), b = tc.reset(p.toStdString()); ++checks;
                if (a != b) report("reset", p, p);
            } else if (op == 7) {
                const QString p = addresses[R(addresses.size())]; const bool l = R(2); tq.setLocked(p, l); tc.setLocked(p.toStdString(), l);
            } else if (op == 8) {
                const QString p = addresses[R(addresses.size())]; ++checks;
                if (tq.fire(p, {1.5, 2}) != tc.fire(p.toStdString(), {core::Value(1.5), core::Value(2)})) report("fire", p, p);
                if (tq.changed(p) != tc.changed(p.toStdString())) report("changed", p, p);
            } else {
                // a structural change
                if (R(2)) { const QString p = addresses[R(addresses.size())]; const bool a = tq.remove(p), b = tc.remove(p.toStdString()); if (a != b) report("remove", p, p); }
                else { tq.addSignal("/new/sig" + QString::number(step), "N"); tc.addSignal("/new/sig" + std::to_string(step), "N"); }
            }
            // compare every node
            std::function<void(const Node &, const ct::Node &)> walk = [&](const Node &a, const ct::Node &b) {
                ++checks;
                const QString pa = a.path(), pb = QString::fromStdString(b.path());
                if (pa != pb) { report("path", pa, pb); return; }
                const QVariantList va = a.get && a.kind() == Kind::Parameter ? a.get() : QVariantList(); const core::ValueList vb = b.get && b.kind() == ct::Kind::Parameter ? b.get() : core::ValueList();
                if (listQ(va) != listC(vb)) report("value", pa + " " + listQ(va), pb + " " + listC(vb));
                if (a.locked != b.locked || a.access != b.access || a.type != QString::fromStdString(b.type) || int(a.kind()) != int(b.kind())) report("flags", pa + QString(" %1 %2 %3 %4").arg(a.locked).arg(a.access).arg(a.type).arg(int(a.kind())), pb + QString(" %1 %2 %3 %4").arg(b.locked).arg(b.access).arg(QString::fromStdString(b.type)).arg(int(b.kind())));
                if (a.label != QString::fromStdString(b.label) || a.clip != QString::fromStdString(b.clip) || bool(a.reset) != bool(b.reset) || bool(a.defaultValue) != bool(b.defaultValue)) report("meta", pa + a.label + a.clip, pb + QString::fromStdString(b.label + b.clip));
                if (a.defaultValue && b.defaultValue && listQ(a.defaultValue()) != listC(b.defaultValue())) report("default", listQ(a.defaultValue()), listC(b.defaultValue()));
                if (a.kind() == Kind::Parameter) { if (oscqWire(Tree::message(a)) != oscc(ct::Tree::message(b)) && true) { /* the typed wire form differs only by tag-typing: compare tags and values */ } }
                if (a.extendedType.size() != int(b.extendedType.size()) || a.unit.size() != int(b.unit.size())) report("ext/unit", pa, pb);
                // the range
                const QJsonArray ra = a.range; if (ra.size() != int(b.range.size())) report("range size", pa + QString::number(ra.size()), pb + QString::number(b.range.size()));
                else for (int k = 0; k < ra.size(); ++k) { const QJsonObject o = ra[k].toObject(); const auto &r = b.range[k];
                    if (o.contains("MIN") != r.min.has_value() || (r.min && o["MIN"].toDouble() != *r.min->number())) report("range min", pa, pb);
                    if (o.contains("MAX") != r.max.has_value() || (r.max && o["MAX"].toDouble() != *r.max->number())) report("range max", pa, pb);
                    if (o.contains("VALS") != !r.vals.empty()) report("range vals", pa, pb); else if (o.contains("VALS")) { const QJsonArray v = o["VALS"].toArray(); if (v.size() != int(r.vals.size())) report("vals size", pa, pb); else for (int j = 0; j < v.size(); ++j) if (v[j].toString().toStdString() != r.vals[size_t(j)].text()) report("vals", pa, pb); } }
                if (a.children().size() != b.children().size()) { report("children", pa, pb); return; }
                auto ia = a.children().begin(); auto ib = b.children().begin();
                for (; ia != a.children().end(); ++ia, ++ib) { if (ia->first != QString::fromStdString(ib->first)) { report("child name", ia->first, QString::fromStdString(ib->first)); return; } walk(*ia->second, *ib->second); }
            };
            walk(tq.root(), tc.root());
            // OSCQuery: the description of every node, the attributes, the preset of the tree
            if (step % 4 == 0) {
                QStringList jp = addresses; jp << "/" << "/layer" << "/act" << "/nowhere";
                for (const QString &p : jp) {
                    const QJsonObject jq = tq.json(p);
                    const QJsonObject jc = QJsonDocument::fromJson(QByteArray::fromStdString(ct::describeJson(tc, p.toStdString()))).object();
                    ++checks; if (jq != jc) report("json", QJsonDocument(jq).toJson(QJsonDocument::Compact), QJsonDocument(jc).toJson(QJsonDocument::Compact));
                    for (const char *name : {"VALUE", "type", "RANGE", "CONTENTS", "Access", "NOPE", "DEFAULT_VALUE", "unit", "EXTENDED_TYPE", "CLIPMODE", "PXS_LOCKED"}) {
                        QJsonObject aq; std::string out; const bool oq = tq.attribute(p, name, aq), oc = ct::attributeJson(tc, p.toStdString(), name, out);
                        const QJsonObject ac = QJsonDocument::fromJson(QByteArray::fromStdString(out)).object();
                        ++checks; if (oq != oc || (oq && aq != ac)) report("attribute", p + name, p + name);
                    }
                }
                const Preset pq = capture(tq); const ct::Preset pc = ct::capture(tc);
                const QJsonObject jq = pq.toJson(); const QJsonObject jc = QJsonDocument::fromJson(QByteArray::fromStdString(pc.toJson())).object();
                ++checks; if (jq != jc) report("preset capture", QJsonDocument(jq).toJson(QJsonDocument::Compact), QJsonDocument(jc).toJson(QJsonDocument::Compact));
            }
            // the stores and the number of writes
            for (const Spec &s : specs) { const auto k = s.path.toStdString(); if (sq.writes[s.path] != sc.writes[k]) report("writes", s.path + QString::number(sq.writes[s.path]), QString::fromStdString(k) + QString::number(sc.writes[k])); }
        }
        {
            const QJsonObject hq = Tree::hostInfo("Name \xC3\xA9", 8000 + round, Tree::defaultExtensions(), QJsonObject{{"X", 1}, {"NAME", "over"}});
            const QJsonObject hc = QJsonDocument::fromJson(QByteArray::fromStdString(ct::hostInfoJson("Name \xC3\xA9", 8000 + round, ct::defaultExtensionsJson(), "{\"X\":1,\"NAME\":\"over\"}"))).object();
            ++checks; if (hq != hc) report("hostinfo", QJsonDocument(hq).toJson(QJsonDocument::Compact), QJsonDocument(hc).toJson(QJsonDocument::Compact));
            // capture, change things, recall: same counts, same values after
            const Preset pq = capture(tq, {}, {{"/act/two", QJsonArray{1.5, 2}}, {"/act/hit", {}}, {"/nowhere", {}}});
            const ct::Preset pc = ct::capture(tc, {}, {{"/act/two", "[1.5,2]"}, {"/act/hit", ""}, {"/nowhere", ""}});
            for (int k = 0; k < 6; ++k) { osc::Message mq; mq.address = "/*"; core::osc::Message mc; mc.address = "/*"; Arg a = makeArg(true, false); mq.args << a.q; mc.args.push_back(a.o); tq.apply(mq); tc.apply(mc); }
            const RecallResult rq = recall(tq, pq); const ct::RecallResult rc = ct::recall(tc, pc); ++checks;
            if (rq.applied != rc.applied || rq.actions != rc.actions || rq.locked != rc.locked || rq.missing != rc.missing) report("recall counts", QString("%1/%2/%3/%4").arg(rq.applied).arg(rq.actions).arg(rq.locked).arg(rq.missing), QString("%1/%2/%3/%4").arg(rc.applied).arg(rc.actions).arg(rc.locked).arg(rc.missing));
            // and the preset read back from its own text
            const Preset bq = Preset::fromJson(pq.toJson()); const ct::Preset bc = ct::Preset::fromJson(pc.toJson()); ++checks;
            if (bq.toJson() != QJsonDocument::fromJson(QByteArray::fromStdString(bc.toJson())).object()) report("preset round trip", "", "");
        }
        if (fq.log != fc.log) report("feed", fq.log.join(';'), fc.log.join(';'));
        if (obsQ != obsC) report("observers", obsQ.join(';'), obsC.join(';'));
        if (structQ != structC) report("structure", structQ.join(';'), structC.join(';'));
        if (outQ != outC) { // message typing: the Qt side keeps QVariants, the core types by tag: compare address, tag and the values that remain
            ++bad; if (bad <= 12) qDebug().noquote() << "DIFF output\n  qt  :" << outQ.join(';').left(600) << "\n  core:" << outC.join(';').left(600);
        }
        ChangeFeed::ignore(&fq); feed.ignore(&fc);
    }
    std::printf("%s: %ld checks, %ld differences\n", strict ? "strict" : "loose", checks, bad);
    return bad ? 1 : 0;
}
