#include "pxs/Server.h"
#include "pxs/Filters.h"
#include "pxs/Physics.h"
#include "pxs/ControlList.h"
#include "pxs/ControlsPage.h"
#include "WebSocketFrame.h"

#include <algorithm>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>

namespace pxs::controls {
namespace {
constexpr int kMaxClients = 16;
constexpr int kMaxHeader = 16384;
constexpr int kMaxFailures = 8;
constexpr qint64 kLockMs = 60'000;

QByteArray hashOf(const QByteArray &text)
{
    return "'sha256-" + QCryptographicHash::hash(text, QCryptographicHash::Sha256).toBase64() + "'";
}

// Same time whatever the digits: a wrong PIN says nothing of how close it was
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    int diff = a.size() ^ b.size();
    for (int i = 0; i < std::max(a.size(), b.size()); ++i) diff |= (i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0);
    return diff == 0;
}

QJsonValue jsonOf(const std::vector<double> &v)
{
    if (v.size() > 1) {
        QJsonArray a;
        for (double x : v) a.append(x);
        return a;
    }
    return v.empty() ? 0.0 : v.front();
}
} // namespace

QByteArray Server::response(int status, const QByteArray &body, const QByteArray &type, const QByteArray &extra, bool frameable)
{
    static const QHash<int, QByteArray> text = {{200, "OK"}, {302, "Found"}, {400, "Bad Request"}, {403, "Forbidden"}, {404, "Not Found"}, {429, "Too Many Requests"}};
    QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + ' ' + text.value(status, "Error") + "\r\n";
    r += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n";
    if (frameable) r.replace("X-Frame-Options: DENY", "X-Frame-Options: SAMEORIGIN"); // framed by a page of the same site only
    r += extra + "Connection: close\r\n\r\n";
    return r + body;
}

QByteArray Server::policy(const QByteArray &script, const QByteArray &style, const QByteArray &extra, bool frameable)
{
    return "default-src 'none'; script-src " + hashOf(script) + "; style-src " + hashOf(style) +
           "; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors " + QByteArray(frameable ? "'self'" : "'none'") + (extra.isEmpty() ? QByteArray() : "; " + extra);
}

QByteArray Server::page(const QString &title)
{
    return QByteArray("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\"><title>") +
           title.toHtmlEscaped().toUtf8() + "</title><style>" + page::style() + "</style></head><body>" + page::kBody + "<script>" + page::script() + "</script></body></html>";
}

Server::Server(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend)
{
    // The timers are children of the server, so that they move with it to another thread (a member without a parent stays where it
    // was made, and starting it from the server's thread is refused: "Timers cannot be started from another thread")
    m_timer.setParent(this);
    m_physics.setParent(this);
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &Server::push);
    m_physics.setInterval(16);
    connect(&m_physics, &QTimer::timeout, this, &Server::stepPhysics);
    m_clock.start();
}

Server::~Server() { stop(); }

QString Server::makePin(int digits)
{
    QString pin;
    for (int i = 0; i < std::clamp(digits, 5, 12); ++i) pin += QChar('0' + QRandomGenerator::system()->bounded(10));
    return pin;
}

bool Server::pinValid(const QString &pin)
{
    if (pin.size() < 4 || pin.size() > 32) return false;
    for (const QChar c : pin)
        if (c.unicode() > 127 || !c.isLetterOrNumber()) return false; // letters and digits: it travels in an address
    return true;
}

bool Server::start(quint16 port, const QString &pin, const QString &listen)
{
    stop();
    m_csp = policy(page::script(), page::style(), {}, m_frameable);
    if (!pin.isEmpty() && !pinValid(pin)) {
        m_status = m_name + QStringLiteral(": the code has to be 4 to 32 letters or digits");
        return false;
    }
    m_pin = pin;
    m_server = new QTcpServer(this);
    const QHostAddress address = listen.isEmpty() ? QHostAddress(QHostAddress::Any) : QHostAddress(listen);
    if (!m_server->listen(address, port)) {
        m_status = m_name + QStringLiteral(": port %1 is not available (%2)").arg(port).arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &Server::onConnection);
    m_timer.start();
    const QStringList list = urls();
    m_status = m_name + QStringLiteral(" on port %1").arg(m_server->serverPort()) + (m_pin.isEmpty() ? QStringLiteral(" (no code)") : QString()) +
               (list.isEmpty() ? QString() : QStringLiteral(": ") + list.join(QStringLiteral("  ")));
    return true;
}

void Server::stop()
{
    m_timer.stop();
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->socket) {
            it->socket->disconnect(this);
            it->socket->abort();
            it->socket->deleteLater();
        }
    m_clients.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_attempts.clear();
    emit clientsChanged(0);
}

bool Server::isRunning() const { return m_server && m_server->isListening(); }
quint16 Server::port() const { return m_server ? m_server->serverPort() : 0; }

int Server::clientCount() const
{
    int n = 0;
    for (const Client &c : m_clients) n += c.websocket;
    return n;
}

QStringList Server::urls() const
{
    QStringList out;
    if (!m_server) return out;
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        if (!(nif.flags() & QNetworkInterface::IsUp) || (nif.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                out << QStringLiteral("http://%1:%2/control").arg(e.ip().toString()).arg(m_server->serverPort());
    }
    return out;
}

bool Server::authorized(const QByteArray &presented) const { return m_pin.isEmpty() || sameBytes(presented, m_pin.toUtf8()); }

bool Server::servePage(const QString &, QByteArray *, QByteArray *) { return false; }
bool Server::message(QTcpSocket *, const QString &, const QJsonObject &) { return false; }
void Server::tick() {}
void Server::clientGone(QTcpSocket *) {}

void Server::sendText(QTcpSocket *client, const QByteArray &json) const { send(client, websocket::encode(websocket::Text, json)); }

QList<QTcpSocket *> Server::websockets() const
{
    QList<QTcpSocket *> out;
    for (const Client &c : m_clients)
        if (c.websocket) out << c.socket;
    return out;
}

void Server::leaveControls(QTcpSocket *client)
{
    const auto it = m_clients.find(client);
    if (it != m_clients.end()) it->ownPages = true;
}

void Server::onConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        if (m_clients.size() >= kMaxClients * 2) { // a flood: no more room
            s->abort();
            s->deleteLater();
            continue;
        }
        Client c;
        c.socket = s;
        m_clients.insert(s, c);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            const bool was = m_clients.value(s).websocket;
            clientGone(s);
            m_clients.remove(s);
            s->deleteLater();
            if (was) emit clientsChanged(clientCount());
        });
    }
}

void Server::send(QTcpSocket *s, const QByteArray &frame) const
{
    if (s->bytesToWrite() > (1 << 20)) { // a client that does not read: dropped, not buffered without end
        s->abort();
        return;
    }
    s->write(frame);
}

QByteArray Server::controlsMessage(const std::vector<Control> &controls) const
{
    QJsonArray list;
    for (const Control &c : controls) list.append(c.wireJson());
    const QString title = m_backend->title();
    static const QJsonObject catalog = filterCatalog(); // what the filter editor of a tile offers
    return QJsonDocument(QJsonObject{{"t", "controls"}, {"title", title.isEmpty() ? m_name : title}, {"show", hasShowPage()}, {"sizes", m_sizes}, {"inspector", m_inspector}, {"nameParts", m_nameParts}, {"sharedPage", m_sharedPage}, {"page", QString::number(m_currentPage)}, {"filters", catalog}, {"list", list}})
        .toJson(QJsonDocument::Compact);
}

void Server::setSharedPage(bool on)
{
    if (on == m_sharedPage) return;
    m_sharedPage = on;
    for (Client &c : m_clients) c.controlsSeen = ~0ull;
}

void Server::setCurrentPage(quint64 id)
{
    if (id == m_currentPage) return;
    m_currentPage = id;
    const QByteArray frame = QJsonDocument(QJsonObject{{"t", "page"}, {"id", QString::number(id)}}).toJson(QJsonDocument::Compact);
    if (m_sharedPage)
        for (QTcpSocket *s : websockets()) sendText(s, frame);
    emit pageShown(id);
}

// The clipboard: copies of the tiles (and what a section holds), made when they were copied, so a cut tile can still be pasted
void Server::pasteClipboard(quint64 page)
{
    if (m_clipboard.empty()) return;
    checkpoint();
    QHash<quint64, quint64> made; // the old id -> the new one
    int below = 0, top = 1 << 20;
    for (const Control &c : m_clipboard)
        if (c.gw > 0) top = std::min(top, c.gy);
    for (const Control &c : m_backend->controls())
        if (c.gw > 0 && c.group == page && c.kind != Control::Kind::Group) below = std::max(below, c.gy + c.gh);
    below = top == (1 << 20) ? 0 : std::max(0, below - top);
    std::vector<quint64> added;
    // The sections first, so what they hold has a group to go to
    for (int pass = 0; pass < 2; ++pass)
        for (const Control &c : m_clipboard) {
            if ((c.kind == Control::Kind::Group) != (pass == 0)) continue;
            Control n = c;
            n.id = 0;
            n.group = c.group && made.contains(c.group) ? made.value(c.group) : page;
            if (n.gw > 0) n.gy += below; // placed: the block lands under what the page already holds, as it was laid out
            if (const quint64 id = m_backend->add(n)) {
                made.insert(c.id, id);
                added.push_back(id);
            }
        }
    // A widget of a physics setting follows its control when that one was pasted with it
    for (const Control &c : m_clipboard) {
        if (!c.physTarget || !made.contains(c.id)) continue;
        std::optional<Control> n = m_backend->control(made.value(c.id));
        if (!n) continue;
        n->physTarget = made.value(c.physTarget, 0);
        if (n->physTarget) m_backend->update(n->id, *n);
    }
    if (!added.empty()) emit layoutEdited();
}

void Server::setDefaultSizes(const QJsonObject &sizes)
{
    QJsonObject clean;
    for (auto it = sizes.begin(); it != sizes.end() && clean.size() < 64; ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() == 2 && it.key().size() < 24) clean.insert(it.key(), QJsonArray{std::clamp(a[0].toInt(1), 1, 12), std::clamp(a[1].toInt(1), 1, 40)});
    }
    if (clean == m_sizes) return;
    m_sizes = clean;
    for (Client &c : m_clients) c.controlsSeen = ~0ull; // each page is sent the list again, with the sizes
}

void Server::setNameParts(int parts)
{
    parts = std::clamp(parts, 1, 4);
    if (parts == m_nameParts) return;
    m_nameParts = parts;
    for (Client &c : m_clients) c.controlsSeen = ~0ull;
}

void Server::setPageOptions(const QString &inspector, const QString &physicsWidget)
{
    const QString i = inspector == QLatin1String("onDemand") ? inspector : QStringLiteral("always");
    const QString w = Control::cleanWidget(physicsWidget);
    if (i == m_inspector && w == m_physicsWidget) return;
    m_inspector = i;
    m_physicsWidget = w;
    for (Client &c : m_clients) c.controlsSeen = ~0ull;
}

void Server::onData(QTcpSocket *s)
{
    auto it = m_clients.find(s);
    if (it == m_clients.end()) return;
    Client &c = *it;
    c.buffer += s->readAll();
    if (!c.websocket) {
        const int end = c.buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (c.buffer.size() > kMaxHeader) s->abort();
            return;
        }
        const QList<QByteArray> lines = c.buffer.left(end).split('\n');
        c.buffer.remove(0, end + 4);
        const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
        QHash<QByteArray, QByteArray> headers;
        for (int k = 1; k < lines.size(); ++k) {
            const int colon = lines[k].indexOf(':');
            if (colon > 0) headers.insert(lines[k].left(colon).trimmed().toLower(), lines[k].mid(colon + 1).trimmed());
        }
        if (request.value(0) != "GET" || request.size() < 2) {
            send(s, response(400, {}, "text/plain"));
            s->disconnectFromHost();
            return;
        }
        const QUrl url = QUrl::fromEncoded(request.value(1));
        const QString path = url.path();
        if (headers.value("upgrade").toLower() == "websocket") {
            const QString who = s->peerAddress().toString();
            Attempts &a = m_attempts[who];
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (a.until > now) {
                send(s, response(429, {}, "text/plain", "Retry-After: 60\r\n"));
                s->disconnectFromHost();
                return;
            }
            // Only the page this server served opens it: a page of another site must not reach the application from a phone's browser
            const QByteArray origin = headers.value("origin");
            const bool sameOrigin = origin.isEmpty() || QUrl(QString::fromUtf8(origin)).authority() == QString::fromUtf8(headers.value("host"));
            const QByteArray pin = QUrl::fromPercentEncoding(QUrlQuery(url).queryItemValue(QStringLiteral("pin"), QUrl::FullyEncoded).toUtf8()).toUtf8();
            int clients = 0;
            for (const Client &other : std::as_const(m_clients)) clients += other.websocket;
            if (path != QLatin1String("/ws") || !sameOrigin || !authorized(pin)) {
                if (path == QLatin1String("/ws") && sameOrigin && ++a.failures >= kMaxFailures) {
                    a.failures = 0;
                    a.until = now + kLockMs;
                }
                send(s, response(403, {}, "text/plain"));
                s->disconnectFromHost();
                return;
            }
            if (clients >= kMaxClients) {
                send(s, response(429, {}, "text/plain"));
                s->disconnectFromHost();
                return;
            }
            a.failures = 0;
            const QByteArray accept = websocket::acceptKey(headers.value("sec-websocket-key"));
            s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
            c.websocket = true;
            emit clientsChanged(clientCount());
            push(); // its first picture of the controls
        } else if (path == QLatin1String("/info")) { // whether it asks a code: nothing else
            send(s, response(200, m_pin.isEmpty() ? "{\"pin\":false}" : "{\"pin\":true}", "application/json"));
            s->disconnectFromHost();
            return;
        } else if (path == QLatin1String("/")) {
            if (QByteArray html, csp; servePage(path, &html, &csp)) { // an application's own front page
                send(s, response(200, html, "text/html; charset=utf-8", "Content-Security-Policy: " + csp + "\r\n"));
                s->disconnectFromHost();
                return;
            }
            send(s, response(302, {}, "text/plain", "Location: /control\r\n"));
            s->disconnectFromHost();
            return;
        } else if (path == QLatin1String("/control")) {
            send(s, response(200, page(m_pageTitle), "text/html; charset=utf-8", "Content-Security-Policy: " + m_csp + "\r\n", m_frameable));
            s->disconnectFromHost();
            return;
        } else if (QByteArray html, csp; servePage(path, &html, &csp)) {
            send(s, response(200, html, "text/html; charset=utf-8", "Content-Security-Policy: " + csp + "\r\n"));
            s->disconnectFromHost();
            return;
        } else {
            send(s, response(404, {}, "text/plain"));
            s->disconnectFromHost();
            return;
        }
    }
    while (c.websocket) {
        int opcode = 0;
        QByteArray payload;
        const int got = websocket::decode(c.buffer, &opcode, &payload, m_maxFrame);
        if (got == 0) return;
        if (got < 0) {
            s->abort();
            return;
        }
        onFrame(c, opcode, payload);
        if (!m_clients.contains(s)) return;
    }
}

void Server::onFrame(Client &c, int opcode, const QByteArray &payload)
{
    switch (opcode) {
    case websocket::Text: {
        if (--c.budget < 0) return; // faster than any hand: dropped
        const QJsonObject o = QJsonDocument::fromJson(payload).object();
        const QString type = o.value("t").toString();
        if (type == QLatin1String("layout")) { // the page laid out again: where each control is, for every device
            QHash<quint64, std::array<int, 4>> places;
            const QJsonObject items = o.value("items").toObject();
            for (auto it = items.begin(); it != items.end() && places.size() < 512; ++it) {
                const QJsonObject p = it.value().toObject();
                places.insert(it.key().toULongLong(), {p.value("x").toInt(), p.value("y").toInt(), p.value("w").toInt(1), p.value("h").toInt(1)});
            }
            bool changed = false;
            for (const Control &control : m_backend->controls()) {
                const auto place = places.find(control.id);
                changed |= place != places.end() && *place != std::array<int, 4>{control.gx, control.gy, control.gw, control.gh};
            }
            if (changed) checkpoint(QStringLiteral("layout"));
            if (m_backend->setLayouts(places)) emit layoutEdited();
            return;
        }
        if (type == QLatin1String("showPage")) {
            if (m_sharedPage) setCurrentPage(o.value("id").toString().toULongLong());
            return;
        }
        if (type == QLatin1String("copy") || type == QLatin1String("cut") || type == QLatin1String("paste") ||
            (type == QLatin1String("remove") && o.contains("ids"))) {
            if (type == QLatin1String("paste")) return pasteClipboard(o.value("page").toString().toULongLong());
            std::vector<quint64> ids;
            for (const QJsonValue &v : o.value("ids").toArray())
                if (ids.size() < 256) ids.push_back(v.toString().toULongLong());
            if (ids.empty()) return;
            if (type == QLatin1String("copy") || type == QLatin1String("cut")) {
                std::vector<Control> clip;
                const std::vector<Control> all = m_backend->controls();
                for (const Control &c : all) {
                    const bool chosen = std::find(ids.begin(), ids.end(), c.id) != ids.end() || (c.group && std::find(ids.begin(), ids.end(), c.group) != ids.end());
                    if (chosen && clip.size() < 512) clip.push_back(c);
                }
                if (clip.empty()) return;
                m_clipboard = clip;
                if (type == QLatin1String("cut"))
                    for (const Control &c : clip)
                        if (std::find(ids.begin(), ids.end(), c.id) == ids.end()) ids.push_back(c.id); // what a cut section held goes with it
                if (type == QLatin1String("copy")) return;
            }
            checkpoint(); // cut or remove several: one step of undo
            bool changed = false;
            for (const quint64 id : ids) changed |= m_backend->remove(id);
            if (changed) emit layoutEdited();
            return;
        }
        if (type == QLatin1String("edit") || type == QLatin1String("remove")) { // a tile's settings changed on the page, or the control removed
            const quint64 target = o.value("id").toString().toULongLong();
            if (!target) return;
            if (type == QLatin1String("remove")) {
                checkpoint();
                if (m_backend->remove(target)) emit layoutEdited();
                return;
            }
            std::optional<Control> found = m_backend->control(target);
            if (!found) return;
            found->applyProps(o.value("props").toObject());
            checkpoint(QStringLiteral("edit%1").arg(target));
            if (m_backend->update(target, *found)) emit layoutEdited();
            return;
        }
        if (type == QLatin1String("physicsWidget")) {
            const std::optional<Control> target = m_backend->control(o.value("id").toString().toULongLong());
            std::optional<Control> made = target ? forPhysics(*target, o.value("key").toString()) : std::nullopt;
            if (made) {
                made->widget = m_physicsWidget;
                checkpoint();
                if (m_backend->add(*made)) emit layoutEdited();
            }
            return;
        }
        if (type == QLatin1String("reset")) {
            if (m_backend->reset(o.value("id").toString().toULongLong())) emit controlMoved();
            return;
        }
        if (type == QLatin1String("undo") || type == QLatin1String("redo")) {
            if (type == QLatin1String("undo") ? undo() : redo()) emit layoutEdited();
            return;
        }
        if (type == QLatin1String("watch")) {
            c.watching = o.value("id").toString().toULongLong();
            c.traceSeen.clear();
            return;
        }
        if (type == QLatin1String("rel")) {
            released(o.value("id").toString().toULongLong(), o.value("i").toInt(-1));
            return;
        }
        const bool setting = type == QLatin1String("set") || type == QLatin1String("color") || type == QLatin1String("point") || type == QLatin1String("adsr") ||
                             type == QLatin1String("multi") || type == QLatin1String("points");
        if (!setting) {
            message(c.socket, type, o);
            return;
        }
        const quint64 id = o.value("id").toString().toULongLong();
        if (!id) return;
        bool moved = false;
        if (intercept(id, type, o)) {
            emit controlMoved();
            break;
        }
        if (type == QLatin1String("set") && o.value("v").isDouble()) {
            moved = m_backend->setValue(id, o.value("v").toDouble());
        } else if (type == QLatin1String("point")) {
            const QJsonArray xy = o.value("xy").toArray();
            moved = xy.size() == 2 && m_backend->setPoint(id, xy[0].toDouble(), xy[1].toDouble());
        } else if (type == QLatin1String("points")) {
            const QJsonArray v = o.value("v").toArray();
            std::vector<double> values;
            for (int i = 0; i < v.size() && i < Control::kPointsMax * 2; ++i) values.push_back(v[i].toDouble());
            moved = !values.empty() && m_backend->setPoints(id, values);
        } else if (type == QLatin1String("multi")) {
            const QJsonArray v = o.value("v").toArray();
            std::vector<double> values;
            for (int i = 0; i < v.size() && i < Control::kMultiMax; ++i) values.push_back(v[i].toDouble());
            moved = !values.empty() && m_backend->setMulti(id, values);
        } else if (type == QLatin1String("adsr")) {
            const QJsonArray v = o.value("v").toArray();
            moved = v.size() == 4 && m_backend->setAdsr(id, {v[0].toDouble(), v[1].toDouble(), v[2].toDouble(), v[3].toDouble()});
        } else if (type == QLatin1String("color")) {
            const QJsonArray rgb = o.value("rgb").toArray();
            moved = rgb.size() == 3 && m_backend->setColor(id, rgb[0].toDouble(), rgb[1].toDouble(), rgb[2].toDouble());
        }
        if (moved) emit controlMoved();
        break;
    }
    case websocket::Close:
        send(c.socket, websocket::encode(websocket::Close, {}));
        c.socket->disconnectFromHost();
        break;
    case websocket::Ping: send(c.socket, websocket::encode(websocket::Pong, payload.left(125))); break;
    default: break;
    }
}

// Undo goes back over what changes the page (its controls, their places and settings), not over the values played
void Server::checkpoint(const QString &what)
{
    const qint64 now = m_clock.elapsed();
    if (!what.isEmpty() && what == m_lastWhat && now - m_lastAt < 1500) {
        m_lastAt = now; // the same thing again (a name typed, a tile dragged): one step
        return;
    }
    m_lastWhat = what;
    m_lastAt = now;
    m_undo.push_back(m_backend->controls());
    if (m_undo.size() > 50) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

bool Server::undo()
{
    if (m_undo.empty()) return false;
    std::vector<Control> before = m_backend->controls();
    if (!m_backend->restore(m_undo.back())) return false;
    m_undo.pop_back();
    m_redo.push_back(std::move(before));
    m_lastWhat.clear();
    return true;
}

bool Server::redo()
{
    if (m_redo.empty()) return false;
    std::vector<Control> before = m_backend->controls();
    if (!m_backend->restore(m_redo.back())) return false;
    m_redo.pop_back();
    m_undo.push_back(std::move(before));
    m_lastWhat.clear();
    return true;
}

// A control with a physics: what the hand sets is where the number should go, and the physics brings it there (and goes on once the hand lets go)
bool Server::intercept(quint64 id, const QString &type, const QJsonObject &o)
{
    if (type != QLatin1String("set") && type != QLatin1String("point") && type != QLatin1String("points") && type != QLatin1String("multi")) return false;
    const std::optional<Control> c = m_backend->control(id);
    if (!c || !c->moving()) {
        m_bodies.remove(id);
        return false;
    }
    std::vector<double> want;
    if (type == QLatin1String("set")) want = {o.value("v").toDouble(std::nan(""))};
    else for (const QJsonValue &v : o.value(type == QLatin1String("point") ? "xy" : "v").toArray()) want.push_back(v.toDouble(std::nan("")));
    if (want.empty()) return false;
    const std::vector<double> now = c->numbers();
    if (want.size() > now.size()) want.resize(now.size());
    auto found = m_bodies.find(id);
    if (found == m_bodies.end() || found->pos.size() != now.size()) {
        physics::Body b;
        b.pos = b.target = physics::toUnits(*c, now);
        b.vel.assign(now.size(), 0.0);
        found = m_bodies.insert(id, b);
    }
    physics::Body &b = *found;
    const std::vector<double> units = physics::toUnits(*c, want);
    // A pad's message names the balls the hand moves ("h"): the others keep going (their numbers in it are only where they were)
    std::vector<char> mine(units.size(), 1);
    if (c->kind == Control::Kind::Points && o.contains("h")) {
        std::fill(mine.begin(), mine.end(), 0);
        for (const QJsonValue &k : o.value("h").toArray()) {
            const int ball = k.toInt(-1);
            for (int j = 2 * ball; ball >= 0 && j < 2 * ball + 2 && j < int(mine.size()); ++j) mine[j] = 1;
        }
    }
    if (b.hand.size() != units.size()) b.hand.assign(units.size(), b.held ? 1 : 0);
    if (!b.held) std::fill(b.hand.begin(), b.hand.end(), 0);
    for (size_t i = 0; i < units.size(); ++i)
        if (mine[i] && std::isfinite(want[i])) {
            b.target[i] = units[i];
            b.hand[i] = 1;
        }
    b.held = true;
    b.moving = true;
    b.movedAt = m_clock.elapsed();
    if (!m_physics.isActive()) m_physics.start();
    return true;
}

void Server::released(quint64 id, int ball)
{
    auto found = m_bodies.find(id);
    if (found == m_bodies.end()) return;
    if (ball >= 0 && !found->hand.empty()) { // one finger up: that ball is thrown, the others are still held
        for (int j = 2 * ball; j < 2 * ball + 2 && j < int(found->hand.size()); ++j) found->hand[j] = 0;
        found->held = std::any_of(found->hand.begin(), found->hand.end(), [](char h) { return h != 0; });
    } else
        found->held = false;
    found->moving = true;
    if (!m_physics.isActive()) m_physics.start();
}

void Server::stepPhysics()
{
    const double dt = 0.016;
    const qint64 now = m_clock.elapsed();
    bool any = false;
    for (auto it = m_bodies.begin(); it != m_bodies.end();) {
        physics::Body &b = it.value();
        const std::optional<Control> c = m_backend->control(it.key());
        if (!c || !c->moving()) {
            it = m_bodies.erase(it);
            continue;
        }
        if (b.held && now - b.movedAt > 4000) b.held = false; // the hand is gone without a word (a lost connection)
        const std::vector<double> before = b.pos;
        b.moving = physics::step(*c, b, dt);
        if (b.pos != before) {
            const std::vector<double> x = physics::fromUnits(*c, b.pos);
            switch (c->kind) {
            case Control::Kind::Slider: m_backend->setValue(c->id, x[0]); break;
            case Control::Kind::Pad: if (x.size() == 2) m_backend->setPoint(c->id, x[0], x[1]); break;
            case Control::Kind::Multi: m_backend->setMulti(c->id, x); break;
            case Control::Kind::Points: m_backend->setPoints(c->id, x); break;
            default: break;
            }
        }
        if (!b.moving && !b.held) {
            it = m_bodies.erase(it);
            continue;
        }
        any |= b.moving;
        ++it;
    }
    if (!any) m_physics.stop();
}

// Every 50 ms: the list to a client that has not got its latest form, then the values that changed
void Server::push()
{
    bool any = false;
    for (const Client &c : std::as_const(m_clients)) any |= c.websocket;
    if (!any) return;
    tick();
    const std::vector<Control> controls = m_backend->controls();
    const quint64 revision = m_backend->revision();
    QByteArray list;
    for (Client &c : m_clients) {
        if (!c.websocket) continue;
        c.budget = std::min(300.0, c.budget + 15); // 300 a second
        if (c.ownPages) continue;
        if (c.controlsSeen != revision) {
            if (list.isEmpty()) list = websocket::encode(websocket::Text, controlsMessage(controls));
            send(c.socket, list);
            c.controlsSeen = revision;
            c.sent.clear();
            for (const Control &control : controls) c.sent.insert(control.id, control.numbers());
            continue;
        }
        QJsonObject changed;
        for (const Control &control : controls) {
            const std::vector<double> now = control.numbers();
            auto found = c.sent.find(control.id);
            if (found != c.sent.end() && found.value() == now) continue;
            c.sent.insert(control.id, now);
            changed[QString::number(control.id)] = jsonOf(now);
        }
        if (!changed.isEmpty())
            send(c.socket, websocket::encode(websocket::Text, QJsonDocument(QJsonObject{{"t", "values"}, {"v", changed}}).toJson(QJsonDocument::Compact)));
        if (c.watching) { // the filters of the tile whose inspector is open: what each one made of the last value
            const std::optional<FilterTrace> trace = m_backend->filterTrace(c.watching);
            if (trace) {
                QJsonArray v;
                for (double x : trace->stages) v.append(x);
                const QByteArray json = QJsonDocument(QJsonObject{{"t", "stages"}, {"id", QString::number(c.watching)}, {"in", trace->in}, {"v", v}}).toJson(QJsonDocument::Compact);
                if (json != c.traceSeen) {
                    c.traceSeen = json;
                    send(c.socket, websocket::encode(websocket::Text, json));
                }
            }
        }
    }
}

} // namespace pxs::controls
