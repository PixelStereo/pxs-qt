#include <pxs/QueryServer.h>
#include <WebSocketFrame.h>
#include <pxs/Convert.h>
#include <pxs/core/tree/oscquery_server.h>

#include <QJsonDocument>
#include <QNetworkInterface>
#include <QSet>
#include <QTimer>

namespace pxs {

namespace {
constexpr int kMaxRequestBytes = 16 * 1024;     // the headers of a request
constexpr quint64 kMaxFrame = 1 << 20;          // a WebSocket frame from a client
constexpr int kMaxWebSockets = 32;
constexpr qint64 kMaxBacklog = 1 << 20;         // what a WebSocket client may be behind before it misses a frame
constexpr int kMaxListening = 4096;             // addresses one client listens to
constexpr int kMaxConnections = 128;            // open connections, HTTP and WebSocket
constexpr int kHeaderMs = 10000;                // a request that has not said what it wants by then is dropped

QByteArray statusLine(int status)
{
    switch (status) {
        case 200: return "200 OK";
        case 204: return "204 No Content";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 503: return "503 Service Unavailable";
        default: return QByteArray::number(status);
    }
}
} // namespace

QueryServer::QueryServer(const core::tree::Tree *tree, QObject *parent)
    : QObject(parent)
    , m_tree(tree)
    , m_server(this)
    , m_extensions(convert::jsonObjectFromText(core::tree::defaultExtensionsJson()))
{
    connect(&m_server, &QTcpServer::newConnection, this, &QueryServer::onNewConnection);
}

bool QueryServer::start(quint16 port)
{
    stop();
    if (!m_server.listen(m_bind, port)) {
        emit logMessage(QStringLiteral("OSCQuery: cannot listen on TCP port %1: %2").arg(port).arg(m_server.errorString()));
        return false;
    }
    emit logMessage(QStringLiteral("OSCQuery listening on HTTP port %1").arg(m_server.serverPort()));
    return true;
}

void QueryServer::stop()
{
    if (m_server.isListening()) m_server.close();
    const auto sockets = m_clients.keys();
    const bool had = webSocketCount() > 0;
    m_clients.clear();
    for (QTcpSocket *s : sockets) s->disconnectFromHost();
    if (had) emit webSocketsChanged(0);
}

void QueryServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        if (m_clients.size() >= kMaxConnections) { // a flood of connections must not take the application's memory
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_clients.insert(socket, Client());
        QTimer::singleShot(kHeaderMs, socket, [this, socket] { // one that never finishes its request, or never sends it
            const auto it = m_clients.constFind(socket);
            if (it != m_clients.constEnd() && !it->websocket) socket->abort();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            const bool was = m_clients.value(socket).websocket;
            m_clients.remove(socket);
            socket->deleteLater();
            if (was) emit webSocketsChanged(webSocketCount());
        });
    }
}

int QueryServer::webSocketCount() const
{
    int n = 0;
    for (const Client &c : m_clients) n += c.websocket;
    return n;
}

bool QueryServer::originAllowed(const QByteArray &origin)
{
    // The decision is pxs-core's; what Qt adds is this machine's own addresses
    std::vector<std::string> own;
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) own.push_back(address.toString().toStdString());
    return core::tree::originAllowed(std::string_view(origin.constData(), std::size_t(origin.size())), own);
}

void QueryServer::stream(const QString &address, const QByteArray &packet)
{
    QByteArray frame;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (!it->websocket || !it->listening.contains(address) || it.key()->bytesToWrite() > kMaxBacklog) continue;
        if (frame.isEmpty()) frame = websocket::encode(websocket::Binary, packet);
        it.key()->write(frame);
    }
}

QStringList QueryServer::pollSubscriptions()
{
    QHash<QString, QByteArray> sampled;
    QSet<QString> sent;
    for (auto client = m_clients.begin(); client != m_clients.end(); ++client) {
        if (!client->websocket) continue;
        QTcpSocket *socket = client.key();
        for (auto subscription = client->listening.begin(); subscription != client->listening.end(); ++subscription) {
            if (socket->bytesToWrite() > kMaxBacklog) break;

            const QString &address = subscription.key();
            auto frame = sampled.constFind(address);
            if (frame == sampled.constEnd()) {
                QByteArray encoded;
                const auto *node = m_tree->find(convert::toUtf8(address));
                if (node && !node->type.empty() && (node->access & 1) && node->get) {
                    const auto bytes = core::osc::encode(core::tree::Tree::message(*node));
                    if (!bytes.empty()) {
                        const QByteArray packet(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
                        encoded = websocket::encode(websocket::Binary, packet);
                    }
                }
                frame = sampled.insert(address, std::move(encoded));
            }
            if (frame->isEmpty()) {
                subscription->clear();
            } else if (*subscription != *frame && socket->write(*frame) == frame->size()) {
                *subscription = *frame;
                sent.insert(address);
            }
        }
    }
    return sent.values();
}

void QueryServer::broadcast(const QByteArray &json)
{
    const QByteArray frame = websocket::encode(websocket::Text, json);
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->websocket && it.key()->bytesToWrite() <= kMaxBacklog) it.key()->write(frame);
}

bool QueryServer::upgrade(QTcpSocket *socket, const QList<QByteArray> &request, const QHash<QByteArray, QByteArray> &headers)
{
    const auto refuse = [&](int status) {
        socket->write("HTTP/1.1 " + statusLine(status) + "\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        socket->disconnectFromHost();
        return false;
    };
    const QByteArray key = headers.value("sec-websocket-key");
    if (request.value(0) != "GET" || key.isEmpty()) return refuse(400);
    if (!originAllowed(headers.value("origin"))) return refuse(403);
    if (webSocketCount() >= kMaxWebSockets) return refuse(503);
    socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                  websocket::acceptKey(key) + "\r\n\r\n");
    m_clients[socket].websocket = true;
    emit webSocketsChanged(webSocketCount());
    return true;
}

void QueryServer::command(QTcpSocket *socket, const QByteArray &json)
{
    const core::tree::Command parsed = core::tree::parseCommand(std::string_view(json.constData(), std::size_t(json.size())));
    const QString what = convert::fromUtf8(parsed.command), path = convert::fromUtf8(parsed.data);
    Client &c = m_clients[socket];
    if (what == QLatin1String("LISTEN") && !path.isEmpty() && (c.listening.contains(path) || c.listening.size() < kMaxListening))
        c.listening.insert(path, {});
    else if (what == QLatin1String("IGNORE")) c.listening.remove(path);
}

void QueryServer::onReadyRead(QTcpSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end()) return;
    it->buffer += socket->readAll();
    if (!it->websocket) {
        const int end = it->buffer.indexOf("\r\n\r\n");
        if (end < 0) { // headers not complete yet
            if (it->buffer.size() > kMaxRequestBytes) socket->disconnectFromHost();
            return;
        }
        const QList<QByteArray> lines = it->buffer.left(end).split('\n');
        it->buffer.remove(0, end + 4); // what follows is the client's first WebSocket frames, if it upgrades
        const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
        QHash<QByteArray, QByteArray> headers;
        for (int k = 1; k < lines.size(); ++k) {
            const int colon = lines[k].indexOf(':');
            if (colon > 0) headers.insert(lines[k].left(colon).trimmed().toLower(), lines[k].mid(colon + 1).trimmed());
        }
        if (headers.value("upgrade").toLower() == "websocket") {
            if (!upgrade(socket, request, headers)) return;
        } else {
            Response resp;
            if (request.size() < 2) {
                resp.status = 400;
            } else {
                resp = answer(QString::fromLatin1(request[0]), request[1]);
            }
            QByteArray out = "HTTP/1.1 " + statusLine(resp.status) + "\r\n"
                             "Connection: close\r\n";
            if (m_cors) out += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n";
            if (!resp.body.isEmpty()) out += "Content-Type: application/json; charset=utf-8\r\n";
            out += "Content-Length: " + QByteArray::number(resp.body.size()) + "\r\n\r\n";
            if (request.value(0) != "HEAD") out += resp.body;
            socket->write(out);
            socket->disconnectFromHost();
            return;
        }
    }
    while (true) {
        it = m_clients.find(socket); // a handler may have closed the server
        if (it == m_clients.end()) return;
        int opcode = 0;
        QByteArray payload;
        const int got = websocket::decode(it->buffer, &opcode, &payload, kMaxFrame);
        if (got == 0) return;
        if (got < 0) {
            socket->disconnectFromHost();
            return;
        }
        switch (opcode) {
        case websocket::Text: command(socket, payload); break;
        case websocket::Binary: emit packetReceived(payload); break;
        case websocket::Ping: socket->write(websocket::encode(websocket::Pong, payload.left(125))); break;
        case websocket::Close:
            socket->write(websocket::encode(websocket::Close, {}));
            socket->disconnectFromHost();
            return;
        default: break;
        }
    }
}

QueryServer::Response QueryServer::answer(const QString &method, const QByteArray &target) const
{
    if (m_beforeAnswer && (method == QLatin1String("GET") || method == QLatin1String("HEAD"))) m_beforeAnswer();
    // What is answered is pxs-core's (the same on every platform); this server reads the request and writes the bytes
    core::tree::ServerInfo info;
    info.name = convert::toUtf8(m_name);
    info.oscPort = m_oscPort;
    info.extensionsJson = convert::toJsonText(m_extensions);
    info.moreJson = convert::toJsonText(m_more);
    const core::tree::HttpAnswer a = core::tree::answerRequest(*m_tree, info, method.toStdString(), std::string_view(target.constData(), std::size_t(target.size())));
    Response resp;
    resp.status = a.status;
    resp.body = QByteArray(a.body.data(), qsizetype(a.body.size()));
    return resp;
}

} // namespace pxs
