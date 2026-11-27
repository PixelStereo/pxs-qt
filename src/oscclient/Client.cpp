#include "pxs/Client.h"

#include <QHostInfo>
#include <memory>
#include <cmath>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrl>

#ifdef PXS_HAS_WEBSOCKETS
#include <QWebSocket>
#endif

namespace pxs::oscquery {

namespace {
constexpr int kMaxListened = 256;
constexpr int kMaxReply = 8 << 20;
} // namespace

Client::Client(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, &Client::poll);
}

Client::~Client() { close(); }

void Client::close()
{
    ++m_generation;
    m_poll.stop();
#ifdef PXS_HAS_WEBSOCKETS
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
#endif
    m_udp.close();
    m_listening.clear();
    m_refreshing = false;
    m_tree.clear();
    m_pushes = false;
    m_state = State::Idle;
}

void Client::fail(const QString &reason)
{
    if (m_state == State::Failed) return;
    const QString host = m_host;
    close();
    m_host = host;
    m_state = State::Failed;
    emit failed(reason);
}

void Client::get(const QString &target, std::function<void(const QJsonDocument &)> done, std::function<void()> missing)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_httpPort);
    url.setPath(target.section(QLatin1Char('?'), 0, 0));
    if (target.contains(QLatin1Char('?'))) url.setQuery(target.section(QLatin1Char('?'), 1));
    QNetworkRequest request(url);
    request.setTransferTimeout(5000);
    QNetworkReply *reply = m_net.get(request);
    reply->setReadBufferSize(kMaxReply);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        if (generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 204) {
            if (missing && status) missing();
            else fail(reply->errorString());
            return;
        }
        if (status == 204) {
            if (missing) missing();
            return;
        }
        const QByteArray body = reply->read(kMaxReply);
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError) {
            fail(QStringLiteral("Not OSCQuery: %1").arg(error.errorString()));
            return;
        }
        done(doc);
    });
}

void Client::open(const QString &host, quint16 httpPort)
{
    close();
    m_host = host;
    m_httpPort = httpPort;
    m_state = State::Connecting;
    m_name.clear();
    m_oscPort = 0;
    get(QStringLiteral("/?HOST_INFO"), [this](const QJsonDocument &doc) {
        const QJsonObject info = doc.object();
        m_name = info.value("NAME").toString();
        m_oscPort = quint16(info.value("OSC_PORT").toInt());
        const bool listen = info.value("EXTENSIONS").toObject().value("LISTEN").toBool();
        const int wsPort = info.contains("WS_PORT") ? info.value("WS_PORT").toInt() : m_httpPort;
        // The address: a number is taken as it is (a device found on the network gives one), a name is looked up
        const auto proceed = [this, listen, wsPort](const QHostAddress &address) {
            m_address = address;
            if (listen) openSocket(quint16(wsPort));
            get(QStringLiteral("/"), [this](const QJsonDocument &tree) {
                if (!m_tree.load(tree.object())) return fail(QStringLiteral("Not an OSCQuery tree"));
                m_state = State::Ready;
                m_poll.start(); // also with a WebSocket, slowly: a device that says it pushes does not always (see poll())
                emit ready();
            });
        };
        QHostAddress literal;
        if (literal.setAddress(m_host)) return proceed(literal);
        auto done = std::make_shared<bool>(false);
        const int generation = m_generation;
        QTimer::singleShot(4000, this, [this, done, generation] { // a lookup that never answers is a failure, not a wait
            if (*done || generation != m_generation) return;
            *done = true;
            fail(QStringLiteral("Cannot find %1").arg(m_host));
        });
        QHostInfo::lookupHost(m_host, this, [this, proceed, done, generation](const QHostInfo &found) {
            if (*done || generation != m_generation) return;
            *done = true;
            if (found.addresses().isEmpty()) return fail(QStringLiteral("Unknown host %1").arg(m_host));
            QHostAddress address = found.addresses().first();
            for (const QHostAddress &a : found.addresses())
                if (a.protocol() == QAbstractSocket::IPv4Protocol) { address = a; break; }
            proceed(address);
        });
    });
}

void Client::openSocket(quint16 port)
{
#ifdef PXS_HAS_WEBSOCKETS
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_pushes = true;
        for (const QString &path : std::as_const(m_listening))
            m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", "LISTEN"}, {"DATA", path}}).toJson(QJsonDocument::Compact)));
    });
    connect(m_socket, &QWebSocket::binaryMessageReceived, this, &Client::onPacket);
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
        const QString command = o.value("COMMAND").toString();
        if (command == QLatin1String("PATH_ADDED") || command == QLatin1String("PATH_REMOVED") || command == QLatin1String("PATH_RENAMED")
            || command == QLatin1String("PATH_CHANGED"))
            refresh();
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        m_pushes = false;
    });
    m_socket->open(QUrl(QStringLiteral("ws://%1:%2/").arg(m_address.toString()).arg(port)));
#else
    Q_UNUSED(port);
#endif
}

void Client::refresh()
{
    if (m_state != State::Ready || m_refreshing) return;
    m_refreshing = true;
    get(QStringLiteral("/"), [this](const QJsonDocument &doc) {
        m_refreshing = false;
        RemoteTree fresh;
        if (!fresh.load(doc.object()) || fresh.signature() == m_tree.signature()) return; // the same tree: nothing to say
        m_tree = std::move(fresh);
        emit treeChanged();
        for (const QString &path : std::as_const(m_listening)) { // its values again
            get(path + QStringLiteral("?VALUE"), [this, path](const QJsonDocument &v) {
                const QJsonArray value = v.object().value("VALUE").toArray();
                if (!value.isEmpty() && m_tree.setValue(path, value)) emit valueChanged(path, value);
            }, [] {});
        }
    }, [this] { m_refreshing = false; });
}

void Client::listen(const QString &path)
{
    if (m_listening.size() >= kMaxListened || m_listening.contains(path)) return;
    m_listening.insert(path);
#ifdef PXS_HAS_WEBSOCKETS
    if (m_socket && m_pushes)
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", "LISTEN"}, {"DATA", path}}).toJson(QJsonDocument::Compact)));
#endif
    // Its value now (a listener only hears the changes)
    get(path + QStringLiteral("?VALUE"), [this, path](const QJsonDocument &doc) {
        const QJsonArray value = doc.object().value("VALUE").toArray();
        if (!value.isEmpty() && m_tree.setValue(path, value)) emit valueChanged(path, value);
    }, [] {});
}

void Client::ignore(const QString &path)
{
    if (!m_listening.remove(path)) return;
#ifdef PXS_HAS_WEBSOCKETS
    if (m_socket && m_pushes)
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", "IGNORE"}, {"DATA", path}}).toJson(QJsonDocument::Compact)));
#endif
}

void Client::poll()
{
    // A device that pushes is asked once a second all the same: what it did not push, or a push that did not reach us, is found
    if (++m_ticks % 12 == 0) refresh(); // the device's own changes (a layer added): its tree, every three seconds
    if (m_pushes && ++m_slow % 4 != 0) return;
    for (const QString &path : std::as_const(m_listening)) {
        get(path + QStringLiteral("?VALUE"), [this, path](const QJsonDocument &doc) {
            const QJsonArray value = doc.object().value("VALUE").toArray();
            const RemoteNode *n = m_tree.find(path);
            if (value.isEmpty() || !n || n->value == value) return;
            m_tree.setValue(path, value);
            emit valueChanged(path, value);
        }, [] {});
    }
}

QJsonArray Client::toJson(const osc::Message &m)
{
    QJsonArray out;
    int k = 0;
    for (const QChar tag : m.types) {
        if (tag == QLatin1Char('T')) out.append(true);
        else if (tag == QLatin1Char('F')) out.append(false);
        else if (tag == QLatin1Char('N') || tag == QLatin1Char('I')) out.append(QJsonValue::Null);
        else if (k < m.args.size()) {
            const QVariant &v = m.args[k++];
            if (tag == QLatin1Char('s')) out.append(v.toString());
            else if (tag == QLatin1Char('i') || tag == QLatin1Char('h')) out.append(v.toLongLong());
            else out.append(v.toDouble());
        }
    }
    return out;
}

osc::Message Client::toMessage(const RemoteNode &node, const QJsonArray &values)
{
    osc::Message m;
    m.address = node.path;
    m.types = node.type;
    if (node.isMomentaryAction()) return m;
    for (int k = 0; k < node.type.size(); ++k) {
        const QChar tag = node.type[k];
        const QJsonValue v = k < values.size() ? values[k] : QJsonValue();
        if (tag == QLatin1Char('T') || tag == QLatin1Char('F')) {
            const bool on = v.isBool() ? v.toBool() : v.toDouble() != 0;
            m.types[k] = on ? QLatin1Char('T') : QLatin1Char('F');
            m.args << on;
        } else if (tag == QLatin1Char('s')) {
            m.args << v.toString();
        } else if (tag == QLatin1Char('i') || tag == QLatin1Char('h')) {
            m.args << int(std::lround(v.toDouble()));
        } else {
            m.args << v.toDouble();
        }
    }
    return m;
}

bool Client::write(const QString &path, const QJsonArray &values)
{
    const RemoteNode *n = m_tree.find(path);
    if (m_state != State::Ready || !n || n->type.isEmpty() || !(n->access & 2) || !m_oscPort) return false;
    const bool action = n->isMomentaryAction();
    if (!action && values.size() != n->type.size()) return false;
    if (m_udp.state() == QAbstractSocket::UnconnectedState) m_udp.bind(QHostAddress::AnyIPv4, 0);
    const bool sent = m_udp.writeDatagram(osc::encode(toMessage(*n, values)), m_address, m_oscPort) >= 0;
    if (sent && !action && (n->access & 1)) { // what it holds now, as far as this client knows (a device that pushes will confirm)
        m_tree.setValue(path, values);
    }
    return sent;
}

void Client::onPacket(const QByteArray &packet)
{
    std::vector<osc::Message> messages;
    if (!osc::decode(packet, messages)) return;
    for (const osc::Message &m : messages) {
        const QJsonArray value = toJson(m);
        if (m_tree.setValue(m.address, value)) emit valueChanged(m.address, value);
    }
}

} // namespace pxs::oscquery
