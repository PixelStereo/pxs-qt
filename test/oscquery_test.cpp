#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;
using pxs::core::Value;

static void pump(QCoreApplication &app, const std::function<bool()> &until, int ms = 2000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) app.processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    core::tree::Tree tree;
    int cue = -1;
    tree.addAction("/show/go", "i", [&](const core::ValueList &a) { cue = int(a.front().number().value_or(-1)); return true; }, "Go to a cue");
    tree.addSignal("/detect/peak", "f", "A peak was heard");
    double level = 0.5;
    auto p = std::make_unique<core::tree::Parameter>("level", "Level", core::tree::Parameter::Type::Float);
    p->range(0, 1).bind([&] { return Value(level); }, [&](const Value &v) { level = v.number().value_or(0.0); });
    tree.add("/level", std::move(p));

    OscService service(&tree, "TestApp");
    CHECK(service.start(0, 0, false));
    CHECK(service.oscPort() != 0 && service.httpPort() != 0);

    // HTTP answers, from the tree
    QueryServer &q = service.query();
    auto body = [&](const char *target) { return QJsonDocument::fromJson(q.answer("GET", target).body).object(); };
    CHECK(body("/?HOST_INFO")["NAME"] == "TestApp" && body("/?HOST_INFO")["OSC_PORT"] == int(service.oscPort()));
    CHECK(!body("/?HOST_INFO")["EXTENSIONS"].toObject().contains("PXS_KIND"));
    CHECK(!body("/show/go").contains("PXS_KIND") && !body("/show/go").contains("VALUE") && body("/show/go")["ACCESS"] == 2);
    CHECK(!body("/detect/peak").contains("VALUE") && body("/detect/peak")["ACCESS"] == 1);
    CHECK(body("/level?VALUE")["VALUE"].toArray().at(0).toDouble() == 0.5);
    CHECK(q.answer("GET", "/nothing").status == 404 && true);
    // What an attribute that is absent answers: an empty object, and 204 only for a value that cannot be read or an attribute this server lacks
    CHECK(q.answer("GET", "/detect/peak?VALUE").status == 200 && q.answer("GET", "/detect/peak?VALUE").body == "{}");
    CHECK(q.answer("GET", "/show?TYPE").status == 200 && q.answer("GET", "/show?TYPE").body == "{}");
    CHECK(q.answer("GET", "/show/go?VALUE").status == 204 && q.answer("GET", "/level?NOPE").status == 204);
    CHECK(body("/?HOST_INFO")["EXTENSIONS"].toObject().value("CLIPMODE") == true);
    CHECK(q.answer("POST", "/").status == 405);

    // UDP in: a message and a parameter written; UDP out: the feedback and the signal come back to the sender
    QUdpSocket client;
    CHECK(client.bind(QHostAddress::LocalHost, 0));
    std::vector<osc::Message> heard;
    QObject::connect(&client, &QUdpSocket::readyRead, [&] {
        while (client.hasPendingDatagrams()) {
            QByteArray d(int(client.pendingDatagramSize()), Qt::Uninitialized);
            client.readDatagram(d.data(), d.size());
            osc::decode(d, heard);
        }
    });
    client.writeDatagram(osc::encode({"/show/go", "i", {7}}), QHostAddress::LocalHost, service.oscPort());
    pump(app, [&] { return cue == 7; });
    CHECK(cue == 7);
    client.writeDatagram(osc::encode({"/level", "f", {0.25}}), QHostAddress::LocalHost, service.oscPort());
    pump(app, [&] { return level == 0.25; });
    CHECK(level == 0.25);
    tree.changed("/level");
    tree.fire("/detect/peak", {Value(0.9)}); // a signal does not go to someone who only wrote a parameter
    pump(app, [&] { return heard.size() >= 1; });
    pump(app, [&] { return false; }, 150);
    CHECK(heard.size() == 1 && heard[0].address == "/level");
    service.addTarget(QHostAddress::LocalHost, client.localPort()); // a destination of the application gets the signals
    tree.fire("/detect/peak", {Value(0.9)});
    pump(app, [&] { return heard.size() >= 2; });
    CHECK(heard.size() == 2 && heard[1].address == "/detect/peak");

    // CORS: a page of another site may read the answers, unless the application is for this computer only
    const auto rawGet = [&](quint16 port) {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        QByteArray reply;
        QObject::connect(&socket, &QTcpSocket::connected, [&] { socket.write("GET /?HOST_INFO HTTP/1.1\r\nHost: x\r\n\r\n"); });
        QObject::connect(&socket, &QTcpSocket::readyRead, [&] { reply += socket.readAll(); });
        pump(app, [&] { return reply.contains("}"); });
        return reply;
    };
    CHECK(rawGet(service.httpPort()).contains("Access-Control-Allow-Origin: *"));
    {
        core::tree::Tree quiet;
        OscService local(&quiet, "Local");
        local.setLocalOnly(true);
        CHECK(local.start(0, 0));
        const QByteArray reply = rawGet(local.httpPort());
        CHECK(reply.contains("200 OK") && !reply.contains("Access-Control-Allow-Origin"));
    }

    service.stop();
    CHECK(!service.isRunning());
    return failures ? 1 : 0;
}
