#include "pxs/Discovery.h"

#include <pxs/MdnsResponder.h>

#include <QtEndian>

namespace pxs::oscquery {

namespace {
const QHostAddress kGroup(QStringLiteral("224.0.0.251"));
constexpr quint16 kMdnsPort = 5353;
constexpr quint16 TypeA = 1, TypePTR = 12, TypeSRV = 33;

quint16 u16(const QByteArray &p, int at) { return quint16((quint8(p[at]) << 8) | quint8(p[at + 1])); }

// A name at `at`, with its compression pointers; `next`: where the record continues. False when malformed
bool readName(const QByteArray &p, int at, QString *out, int *next)
{
    QString name;
    int pos = at, end = -1, jumps = 0;
    for (;;) {
        if (pos >= p.size()) return false;
        const quint8 len = quint8(p[pos]);
        if (len == 0) {
            if (end < 0) end = pos + 1;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= p.size() || ++jumps > 16) return false;
            if (end < 0) end = pos + 2;
            pos = ((len & 0x3F) << 8) | quint8(p[pos + 1]);
            continue;
        }
        if (len > 63 || pos + 1 + len > p.size()) return false;
        if (!name.isEmpty()) name += QLatin1Char('.');
        name += QString::fromUtf8(p.mid(pos + 1, len));
        pos += 1 + len;
    }
    *out = name;
    *next = end;
    return true;
}
} // namespace

Discovery::Discovery(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<pxs::oscquery::Device>();
    connect(&m_socket, &QUdpSocket::readyRead, this, &Discovery::onReadyRead);
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &Discovery::ask);
}

Discovery::~Discovery() { stop(); }

bool Discovery::start(const QString &serviceType)
{
    stop();
    m_type = serviceType + QStringLiteral(".local");
    if (m_socket.bind(QHostAddress::AnyIPv4, kMdnsPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
            const auto flags = iface.flags();
            if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::CanMulticast)) continue;
            if (m_socket.joinMulticastGroup(kGroup, iface)) m_interfaces << iface;
        }
        if (m_interfaces.isEmpty() && !m_socket.joinMulticastGroup(kGroup)) {
            qWarning("OSCQuery discovery: cannot join mDNS on UDP %d: %s", kMdnsPort, qPrintable(m_socket.errorString()));
            m_socket.close();
        }
    } else {
        qWarning("OSCQuery discovery: cannot bind UDP %d: %s", kMdnsPort, qPrintable(m_socket.errorString()));
    }
    if (m_socket.state() != QAbstractSocket::BoundState) {
        if (!m_socket.bind(QHostAddress::AnyIPv4, 0, QUdpSocket::DontShareAddress)) {
            qWarning("OSCQuery discovery: cannot open a fallback UDP port: %s", qPrintable(m_socket.errorString()));
            return false;
        }
        qWarning("OSCQuery discovery: UDP %d for unicast replies; queries still go to UDP %d", m_socket.localPort(), kMdnsPort);
        for (const auto &iface : QNetworkInterface::allInterfaces())
            if (iface.flags().testFlag(QNetworkInterface::IsUp) && iface.flags().testFlag(QNetworkInterface::CanMulticast))
                m_interfaces << iface;
    }
    m_running = true;
    ask();
    m_timer.start();
    return true;
}

void Discovery::stop()
{
    m_timer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) m_socket.close();
    m_interfaces.clear();
    m_running = false;
}

QByteArray Discovery::query(const QString &serviceType)
{
    QByteArray q(12, '\0');
    q[5] = 1; // one question
    q += MdnsResponder::encodeName(serviceType + QStringLiteral(".local"));
    q += char(0);
    q += char(TypePTR);
    q += char(0);
    q += char(1); // class IN
    return q;
}

void Discovery::ask()
{
    QByteArray q = query(m_type.chopped(6));
    if (m_socket.localPort() != kMdnsPort) {
        // Legacy unicast mDNS queries use an ephemeral source port and a nonzero transaction ID.
        q[0] = 0;
        q[1] = 1;
    }
    if (m_interfaces.isEmpty()) {
        m_socket.writeDatagram(q, kGroup, kMdnsPort);
        return;
    }
    for (const QNetworkInterface &iface : std::as_const(m_interfaces)) {
        m_socket.setMulticastInterface(iface);
        m_socket.writeDatagram(q, kGroup, kMdnsPort);
    }
}

QList<DnsRecord> Discovery::parse(const QByteArray &p)
{
    QList<DnsRecord> out;
    if (p.size() < 12 || !(quint8(p[2]) & 0x80)) return out; // only the answers
    const int questions = u16(p, 4), records = u16(p, 6) + u16(p, 8) + u16(p, 10);
    int at = 12;
    for (int i = 0; i < questions; ++i) {
        QString name;
        if (!readName(p, at, &name, &at) || at + 4 > p.size()) return out;
        at += 4;
    }
    for (int i = 0; i < records && out.size() < 256; ++i) {
        DnsRecord r;
        if (!readName(p, at, &r.name, &at) || at + 10 > p.size()) return out;
        r.type = u16(p, at);
        r.ttl = (quint32(u16(p, at + 4)) << 16) | u16(p, at + 6);
        const int len = u16(p, at + 8);
        const int data = at + 10;
        if (data + len > p.size()) return out;
        at = data + len;
        if (r.type == TypePTR) {
            int n;
            if (!readName(p, data, &r.target, &n)) continue;
        } else if (r.type == TypeSRV) {
            int n;
            if (len < 7 || !readName(p, data + 6, &r.target, &n)) continue;
            r.port = u16(p, data + 4);
        } else if (r.type == TypeA) {
            if (len != 4) continue;
            r.address = QHostAddress(qFromBigEndian<quint32>(p.constData() + data));
        } else {
            continue;
        }
        out.append(r);
    }
    return out;
}

void Discovery::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QByteArray data;
        data.resize(int(m_socket.pendingDatagramSize()));
        m_socket.readDatagram(data.data(), data.size());
        handle(data);
    }
}

QHostAddress Discovery::bestAddress(const QList<QHostAddress> &heard)
{
    // 169.254.0.0/16 is what an interface with no network gives itself (USB links, an unplugged cable): the last resort
    for (const QHostAddress &a : heard)
        if (!a.isLinkLocal() && !a.isLoopback()) return a;
    return heard.isEmpty() ? QHostAddress() : heard.first();
}

void Discovery::handle(const QByteArray &packet)
{
    const QString type = m_type.isEmpty() ? QStringLiteral("_oscjson._tcp.local") : m_type;
    const QList<DnsRecord> records = parse(packet);
    for (const DnsRecord &r : records) {
        if (r.type == TypePTR && r.name.compare(type, Qt::CaseInsensitive) == 0) {
            const QString key = r.target.toLower();
            if (r.ttl == 0) { // goodbye
                m_srv.remove(key);
                const Device gone = m_devices.take(key);
                if (!gone.name.isEmpty()) emit deviceLost(gone);
                continue;
            }
            m_names.insert(key, r.target);
        } else if (r.type == TypeSRV && r.ttl > 0) {
            m_srv.insert(r.name.toLower(), r);
            m_names.insert(r.name.toLower(), r.name);
        } else if (r.type == TypeA && r.ttl > 0) {
            // A host answers on each interface with that interface's address (a phone plugged in by USB adds a 169.254.x.x one):
            // all are kept, the best is chosen below, so the device does not flip between them as the answers come.
            QList<QHostAddress> &heard = m_hosts[r.name.toLower()];
            if (!heard.contains(r.address)) heard.append(r.address);
        }
    }
    for (auto it = m_srv.cbegin(); it != m_srv.cend(); ++it) {
        if (!it.key().endsWith(type.toLower())) continue;
        const auto host = m_hosts.constFind(it.value().target.toLower());
        if (host == m_hosts.cend() || host.value().isEmpty()) continue;
        Device d;
        QString instance = m_names.value(it.key(), it.key());
        d.name = instance.chopped(qMin<qsizetype>(instance.size(), type.size() + 1));
        d.host = it.value().target;
        d.address = bestAddress(host.value());
        d.port = it.value().port;
        const auto known = m_devices.constFind(it.key());
        if (known != m_devices.cend() && *known == d) continue;
        m_devices.insert(it.key(), d);
        emit deviceFound(d);
    }
}

} // namespace pxs::oscquery
