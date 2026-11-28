// The WebSocket of the OSCQuery port, tried with Qt's own client (a second implementation of the handshake and the frames):
// LISTEN / IGNORE stream binary OSC, a binary frame is applied, PATH_CHANGED follows the address space, and a page of another
// origin is refused.

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;

static void pump(QCoreApplication &app, const std::function<bool()> &until, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) app.processEvents(QEventLoop::AllEvents, 20);
}

static QByteArray command(const char *what, const char *path)
{
    return QJsonDocument(QJsonObject{{"COMMAND", what}, {"DATA", path}}).toJson(QJsonDocument::Compact);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    core::tree::Tree tree;
    int cue = -1;
    tree.addAction("/show/go", "i", [&](const core::ValueList &a) { cue = int(a.front().number().value_or(-1)); return true; });
    tree.addSignal("/detect/peak", "f");
    double level = 0.5;
    auto p = std::make_unique<core::tree::Parameter>("level", "Level", core::tree::Parameter::Type::Float);
    p->range(0, 1).bind([&] { return core::Value(level); }, [&](const core::Value &v) { level = v.number().value_or(0.0); });
    tree.add("/level", std::move(p));
    OscService service(&tree, "WsTest");
    CHECK(service.start(0, 0, false));
    const QUrl url(QStringLiteral("ws://127.0.0.1:%1/").arg(service.httpPort()));

    // HOST_INFO says what the server does
    const QJsonObject info = QJsonDocument::fromJson(service.query().answer("GET", "/?HOST_INFO").body).object();
    for (const char *key : {"LISTEN", "PATH_CHANGED", "PATH_ADDED", "PATH_REMOVED"}) CHECK(info["EXTENSIONS"].toObject()[key] == true);
    CHECK(info["WS_PORT"].toInt() == int(service.httpPort()));

    QWebSocket ws;
    std::vector<osc::Message> heard;
    QStringList texts;
    QObject::connect(&ws, &QWebSocket::binaryMessageReceived, [&](const QByteArray &d) { osc::decode(d, heard); });
    QObject::connect(&ws, &QWebSocket::textMessageReceived, [&](const QString &t) { texts << t; });
    ws.open(url);
    pump(app, [&] { return ws.state() == QAbstractSocket::ConnectedState; });
    CHECK(ws.state() == QAbstractSocket::ConnectedState);

    // Nothing is sent before LISTEN; then the values of that address, whoever writes them
    tree.apply("/level", {core::Value(0.1)});
    pump(app, [&] { return false; }, 150);
    CHECK(heard.empty());
    ws.sendTextMessage(QString::fromUtf8(command("LISTEN", "/level")));
    pump(app, [&] { return false; }, 150);
    tree.apply("/level", {core::Value(0.25)});
    pump(app, [&] { return !heard.empty(); });
    CHECK(heard.size() == 1 && heard[0].address == "/level" && heard[0].types == "f" && std::abs(heard[0].args.value(0).toDouble() - 0.25) < 1e-6);
    // Only that address: a signal nobody listens to is not sent
    tree.fire("/detect/peak", {core::Value(0.9)});
    pump(app, [&] { return false; }, 150);
    CHECK(heard.size() == 1);
    ws.sendTextMessage(QString::fromUtf8(command("LISTEN", "/detect/peak")));
    pump(app, [&] { return false; }, 150);
    tree.fire("/detect/peak", {core::Value(0.9)});
    pump(app, [&] { return heard.size() >= 2; });
    CHECK(heard.size() == 2 && heard[1].address == "/detect/peak");
    // IGNORE stops it
    ws.sendTextMessage(QString::fromUtf8(command("IGNORE", "/level")));
    pump(app, [&] { return false; }, 150);
    tree.apply("/level", {core::Value(0.4)});
    pump(app, [&] { return false; }, 150);
    CHECK(heard.size() == 2);

    // A binary frame is an OSC packet for the application
    ws.sendBinaryMessage(osc::encode({"/show/go", "i", {5}}));
    pump(app, [&] { return cue == 5; });
    CHECK(cue == 5);
    ws.sendBinaryMessage(osc::encode({"/level", "f", {0.9}}));
    pump(app, [&] { return level > 0.85; });
    CHECK(std::abs(level - 0.9) < 1e-6);

    // The address space changes: PATH_ADDED with what was made (what is made together is one message), PATH_REMOVED
    tree.add("/layer/1/opacity");
    tree.add("/layer/1/mode");
    pump(app, [&] { return !texts.isEmpty(); });
    pump(app, [&] { return false; }, 150);
    CHECK(texts.size() == 1);
    const QJsonObject added = QJsonDocument::fromJson(texts.value(0).toUtf8()).object();
    CHECK(added["COMMAND"] == "PATH_ADDED" && added["DATA"] == "/layer" && added["FULL_PATH"] == "/layer");
    CHECK(added["CONTENTS"].toObject()["1"].toObject()["CONTENTS"].toObject().contains("mode"));
    texts.clear();
    tree.remove("/layer/1/mode");
    pump(app, [&] { return !texts.isEmpty(); });
    const QJsonObject removed = QJsonDocument::fromJson(texts.value(0).toUtf8()).object();
    CHECK(removed["COMMAND"] == "PATH_REMOVED" && removed["DATA"] == "/layer/1/mode");
    texts.clear();
    // Too much at once, or everything gone: PATH_CHANGED, the highest path that changed
    for (int k = 0; k < 100; ++k) tree.add("/bulk/" + std::to_string(k));
    pump(app, [&] { return !texts.isEmpty(); });
    pump(app, [&] { return false; }, 150);
    const QJsonObject changed = QJsonDocument::fromJson(texts.value(0).toUtf8()).object();
    CHECK(texts.size() == 1 && changed["COMMAND"] == "PATH_CHANGED" && changed["DATA"] == "/bulk");

    // A page of another site is refused (a WebSocket writes); one of this machine is let in
    QWebSocket evil(QStringLiteral("http://evil.example"));
    evil.open(url);
    pump(app, [&] { return evil.state() == QAbstractSocket::ConnectedState; }, 1000); // refused: it never gets in
    CHECK(evil.state() != QAbstractSocket::ConnectedState && service.query().webSocketCount() == 1);
    QWebSocket local(QStringLiteral("http://localhost:8080"));
    local.open(url);
    pump(app, [&] { return local.state() == QAbstractSocket::ConnectedState; });
    CHECK(local.state() == QAbstractSocket::ConnectedState);
    CHECK(service.query().webSocketCount() == 2);
    // A tree backed by external state is sampled without requiring a Tree notification.
    QueryServer &query = service.query();
    std::vector<osc::Message> second;
    QObject::connect(&local, &QWebSocket::binaryMessageReceived, [&](const QByteArray &d) { osc::decode(d, second); });
    heard.clear();
    ws.sendTextMessage(QString::fromUtf8(command("LISTEN", "/level")));
    pump(app, [&] { return !query.pollSubscriptions().isEmpty(); });
    pump(app, [&] { return !heard.empty(); });
    CHECK(heard.size() == 1 && std::abs(heard.front().args.value(0).toDouble() - 0.9) < 1e-6);

    local.sendTextMessage(QString::fromUtf8(command("LISTEN", "/level")));
    pump(app, [&] { return !query.pollSubscriptions().isEmpty(); });
    pump(app, [&] { return !second.empty(); });
    CHECK(heard.size() == 1 && second.size() == 1);
    CHECK(query.pollSubscriptions().isEmpty());

    level = 0.6;
    CHECK(query.pollSubscriptions() == QStringList{QStringLiteral("/level")});
    pump(app, [&] { return heard.size() == 2 && second.size() == 2; });
    CHECK(heard.size() == 2 && second.size() == 2 &&
          std::abs(heard.back().args.value(0).toDouble() - 0.6) < 1e-6 &&
          std::abs(second.back().args.value(0).toDouble() - 0.6) < 1e-6);

    // A delegated HTTP answer can materialize a path synchronously.
    query.setBeforeAnswer([&] { tree.add("/live"); });
    CHECK(query.answer("GET", "/live").status == 200);
    query.setBeforeAnswer({});
    local.close();
    ws.close();
    pump(app, [&] { return service.query().webSocketCount() == 0; });
    CHECK(service.query().webSocketCount() == 0);

    service.stop();
    return failures ? 1 : 0;
}
