// The Qt face of pxs-core's parameter: what an application with Qt types writes and reads, and the change feed.
#include <pxs/ChangeFeed.h>
#include <pxs/Parameter.h>

#include <QCoreApplication>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

struct Watcher final : ChangeFeed::Observer {
    quint64 holder = 0;
    QString path;
    void parameterWritten(quint64 h, const QString &p) override { holder = h; path = p; }
    void layerChanged(quint64) override {}
    void addressChanged(const QString &) override {}
};
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    double stored = 0.5;
    Parameter p(QStringLiteral("spatial/rotation"), QStringLiteral("Spatial › Rotation (°)"), Parameter::Type::Float);
    p.range(-180, 180).byDefault(0.0).step(1).holder(7).bind([&] { return QVariant(stored); }, [&](const QVariant &v) { stored = v.toDouble(); });

    const ParamInfo i = p.info();
    check(i.path == QStringLiteral("spatial/rotation") && i.name() == QStringLiteral("Rotation (°)"), "path and own name");
    check(i.min == -180 && i.max == 180 && i.isNumber() && i.defaultValue.toDouble() == 0.0, "range and default as Qt types");
    check(i.toCore().path == "spatial/rotation", "the core declaration");

    Watcher w;
    ChangeFeed::observe(&w);
    check(p.setValue(QVariant(90.0)) && stored == 90.0 && p.value().toDouble() == 90.0, "a value is written and read as a QVariant");
    check(w.holder == 7 && w.path == QStringLiteral("spatial/rotation"), "the change feed tells who wrote what");
    check(p.setValue(QVariant(500.0)) && stored == 180.0, "a value is kept within its limits");
    check(p.reset() && stored == 0.0, "a reset writes the default");
    ChangeFeed::ignore(&w);
    check(!ChangeFeed::listening(), "no observer left");

    check(p.isStored() && p.json().toDouble() == 0.0, "its value as a file keeps it");
    const QJsonObject file = parametersToJson(std::vector<Parameter *>{&p});
    check(file.value(QStringLiteral("spatial/rotation")).toDouble() == 0.0, "parametersToJson");

    Parameter c(QStringLiteral("blend_mode"), QStringLiteral("Blend Mode"), Parameter::Type::Choice);
    QString key = QStringLiteral("normal");
    c.choices({QStringLiteral("normal"), QStringLiteral("add")}).byDefault(QStringLiteral("normal"))
        .bind([&] { return QVariant(key); }, [&](const QVariant &v) { key = v.toString(); });
    check(c.setValue(QStringLiteral("add")) && key == QStringLiteral("add"), "a choice takes one of its keys");
    check(!c.setValue(QStringLiteral("nope")) && key == QStringLiteral("add"), "and no other");
    check(c.info().choices == QStringList({QStringLiteral("normal"), QStringLiteral("add")}), "choices as a QStringList");

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
