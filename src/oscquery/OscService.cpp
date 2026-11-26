#include <pxs/OscService.h>
#include <pxs/Convert.h>
#include <pxs/core/tree/oscquery.h>
#include <pxs/core/tree/osc_bridge.h>

#include <QJsonDocument>
#include <QThread>

namespace pxs {

OscService::OscService(core::tree::Tree *tree, const QString &name, QObject *parent)
    : QObject(parent), m_tree(tree), m_name(name), m_http(tree, this), m_mdns(this)
{
    m_http.setName(name);
    m_pathTimer.setSingleShot(true);
    m_pathTimer.setInterval(50);
    connect(&m_pathTimer, &QTimer::timeout, this, &OscService::onPathTimer);
    connect(&m_http, &QueryServer::packetReceived, this, [this](const QByteArray &packet) { apply(packet, nullptr, 0); });
    connect(&m_http, &QueryServer::webSocketsChanged, this, [this](int count) { m_webSockets = count; });
    connect(&m_udp, &QUdpSocket::readyRead, this, &OscService::onReadyRead);
    connect(&m_http, &QueryServer::logMessage, this, &OscService::logMessage);
    connect(&m_mdns, &MdnsResponder::logMessage, this, &OscService::logMessage);
}

OscService::~OscService() { stop(); }

bool OscService::start(quint16 oscPort, quint16 httpPort, bool announce)
{
    stop();
    const QHostAddress where = m_localOnly ? QHostAddress(QHostAddress::LocalHost) : QHostAddress(QHostAddress::Any);
    m_http.setBindAddress(where);
    m_http.setCorsOpen(!m_localOnly);
    if (m_localOnly) announce = false;
    if (!m_udp.bind(where, oscPort, QUdpSocket::DontShareAddress)) {
        emit logMessage(QStringLiteral("OSC: cannot listen on UDP port %1: %2").arg(oscPort).arg(m_udp.errorString()));
        if (m_udp.error() != QAbstractSocket::AddressInUseError ||
            !m_udp.bind(where, 0, QUdpSocket::DontShareAddress)) return false;
        emit logMessage(QStringLiteral("OSC: using UDP %1 instead").arg(m_udp.localPort()));
    }
    m_http.setOscPort(m_udp.localPort());
    if (!m_http.start(httpPort)) {
        if (!httpPort || m_http.listenError() != QAbstractSocket::AddressInUseError || !m_http.start(0)) {
            m_udp.close();
            return false;
        }
        emit logMessage(QStringLiteral("OSCQuery: using TCP %1 instead of TCP %2").arg(m_http.port()).arg(httpPort));
    }
    QJsonObject extensions = convert::jsonObjectFromText(core::tree::defaultExtensionsJson());
    extensions["LISTEN"] = true;       // the WebSocket of the same port (IGNORE comes with it)
    extensions["PATH_CHANGED"] = true;
    extensions["PATH_ADDED"] = true;
    extensions["PATH_REMOVED"] = true;
    m_http.setExtensions(extensions);
    m_http.setHostInfoMore(QJsonObject{{"WS_PORT", int(m_http.port())}});
    m_tree->setOutput([this](const core::osc::Message &m) { send(m); });
    m_observer = m_tree->addObserver([this](const core::tree::Node &n, const core::ValueList &args) { onChanged(n, args); });
    m_tree->setStructureObserver([this](const std::string &path, core::tree::Tree::Change change) { onStructure(path, change); });
    if (announce) {
        const QString type = QStringLiteral("_oscjson._tcp");
        m_mdns.start(m_name, {{type, m_http.port()}, {QStringLiteral("_osc._udp"), m_udp.localPort()}});
    }
    emit logMessage(QStringLiteral("OSC on UDP %1, OSCQuery on HTTP %2").arg(m_udp.localPort()).arg(m_http.port()));
    return true;
}

void OscService::stop()
{
    m_mdns.stop();
    m_http.stop();
    m_udp.close();
    m_tree->setOutput(nullptr);
    if (m_observer) m_tree->removeObserver(m_observer);
    m_observer = 0;
    m_tree->setStructureObserver(nullptr);
    m_pathTimer.stop();
    m_pending.clear();
}

void OscService::addTarget(const QHostAddress &address, quint16 port)
{
    for (const auto &t : m_targets)
        if (t.first == address && t.second == port) return;
    m_targets.append({address, port});
}

void OscService::clearTargets() { m_targets.clear(); }

void OscService::onReadyRead()
{
    while (m_udp.hasPendingDatagrams()) {
        QHostAddress from;
        quint16 port = 0;
        QByteArray packet(int(m_udp.pendingDatagramSize()), Qt::Uninitialized);
        m_udp.readDatagram(packet.data(), packet.size(), &from, &port);
        apply(packet, &from, port);
    }
}

void OscService::apply(const QByteArray &packet, const QHostAddress *from, quint16 port)
{
    std::vector<core::osc::Message> messages;
    if (!core::osc::decode(reinterpret_cast<const std::uint8_t *>(packet.constData()), std::size_t(packet.size()), messages)) return;
    if (from && m_learn && !m_learned.contains({*from, port})) m_learned.append({*from, port}); // hears the feedback
    for (const core::osc::Message &m : messages) emit messageReceived(convert::fromCore(m), m_tree->apply(m));
}

// What a client that listens to an address is sent: the message a parameter says it holds, or the signal as it was fired
void OscService::onChanged(const core::tree::Node &node, const core::ValueList &args)
{
    if (m_webSockets.load() == 0 || node.isContainer()) return;
    core::osc::Message m;
    if (node.kind() == core::tree::Kind::Parameter) {
        m = core::tree::Tree::message(node);
    } else if (node.kind() == core::tree::Kind::Signal) {
        m.address = node.path();
        if (node.type != "N") {
            m.types = node.type;
            m.args = core::tree::toOscArguments(node.type, args);
        }
    } else {
        return; // an action is received, it does not say anything
    }
    const QString address = convert::fromUtf8(m.address);
    const core::osc::Bytes bytes = core::osc::encode(m);
    const QByteArray packet(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
    const auto stream = [this, address, packet] { m_http.stream(address, packet); };
    if (QThread::currentThread() == thread()) stream();
    else QMetaObject::invokeMethod(this, stream, Qt::QueuedConnection); // a socket is written by its own thread
}

static QString commonPath(const QString &a, const QString &b)
{
    if (a.isEmpty()) return b;
    const QStringList x = a.split('/', Qt::SkipEmptyParts), y = b.split('/', Qt::SkipEmptyParts);
    QString out;
    for (int k = 0; k < x.size() && k < y.size() && x[k] == y[k]; ++k) out += '/' + x[k];
    return out.isEmpty() ? QStringLiteral("/") : out;
}

static bool isUnder(const QString &path, const QString &parent) { return path.startsWith(parent == QLatin1String("/") ? parent : parent + '/'); }

void OscService::onStructure(const std::string &where, core::tree::Tree::Change change)
{
    if (m_webSockets.load() == 0) return;
    const QString path = convert::fromUtf8(where);
    const auto note = [this, path, change] {
        m_pending.append({path, change});
        if (!m_pathTimer.isActive()) m_pathTimer.start(); // a layer made of dozens of nodes is one message
    };
    if (QThread::currentThread() == thread()) note();
    else QMetaObject::invokeMethod(this, note, Qt::QueuedConnection);
}

// What the clients are told of the changes of the last moments: each node made (with what it holds, the node's own description
// merged into the message: a client reads the node from it) and each node removed; PATH_CHANGED when the tree was cleared or
// too much changed to say it one by one
void OscService::onPathTimer()
{
    const auto pending = m_pending;
    m_pending.clear();
    constexpr int kMostMessages = 64;
    bool all = pending.size() > kMostMessages;
    QString common;
    for (const auto &c : pending) {
        all |= c.second == core::tree::Tree::Change::Cleared;
        common = commonPath(common, c.first);
    }
    const auto send = [this](const QJsonObject &o) { m_http.broadcast(QJsonDocument(o).toJson(QJsonDocument::Compact)); };
    if (all) {
        if (!pending.isEmpty()) send(QJsonObject{{"COMMAND", "PATH_CHANGED"}, {"DATA", common}});
        return;
    }
    for (int k = 0; k < pending.size(); ++k) {
        const QString &path = pending[k].first;
        bool covered = false; // a node inside one made (or removed) in the same moment is said with it
        for (int j = 0; j < pending.size() && !covered; ++j)
            covered = j != k && pending[j].second == pending[k].second && isUnder(path, pending[j].first);
        if (covered) continue;
        if (pending[k].second == core::tree::Tree::Change::Added) {
            QJsonObject node = convert::jsonObjectFromText(core::tree::describeJson(*m_tree, convert::toUtf8(path)));
            if (node.isEmpty()) continue; // gone again before it was said
            node["COMMAND"] = "PATH_ADDED";
            node["DATA"] = path;
            send(node);
        } else {
            send(QJsonObject{{"COMMAND", "PATH_REMOVED"}, {"DATA", path}});
        }
    }
}

void OscService::send(const core::osc::Message &message)
{
    if (m_udp.state() != QAbstractSocket::BoundState) return;
    const core::osc::Bytes bytes = core::osc::encode(message);
    const QByteArray packet(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
    for (const auto &t : m_targets) m_udp.writeDatagram(packet, t.first, t.second);
    const core::tree::Node *node = m_tree->find(message.address);
    if (node && node->kind() == core::tree::Kind::Signal) return; // a stream goes to the targets only
    for (const auto &t : m_learned)
        if (!m_targets.contains(t)) m_udp.writeDatagram(packet, t.first, t.second);
}

} // namespace pxs
