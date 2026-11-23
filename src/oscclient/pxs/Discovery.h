#pragma once
// Finds the OSCQuery devices of the local network (multicast DNS / DNS-SD, "_oscjson._tcp"): asks for them now and every few seconds,
// and tells when one appears, or says goodbye. The counterpart of MdnsResponder, which announces.
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

namespace pxs::oscquery {

struct Device {
    QString name;         // the instance: "NyxMap (studio-mac)"
    QString host;         // its host name ("studio-mac.local")
    QHostAddress address; // where to reach it
    quint16 port = 0;     // its OSCQuery HTTP port
    bool operator==(const Device &o) const { return name == o.name && address == o.address && port == o.port; }
};

// One record of a DNS packet, as Discovery reads them (public for tests)
struct DnsRecord {
    QString name;
    quint16 type = 0;
    quint32 ttl = 0;
    QString target;       // PTR: the instance; SRV: the host
    quint16 port = 0;     // SRV
    QHostAddress address; // A
};

class Discovery : public QObject {
    Q_OBJECT
public:
    explicit Discovery(QObject *parent = nullptr);
    ~Discovery() override;

    bool start(const QString &serviceType = QStringLiteral("_oscjson._tcp")); // false: the multicast port cannot be used
    void stop();
    bool isRunning() const { return m_running; }
    QList<Device> devices() const { return m_devices.values(); }

    // The answers in a packet, and what to do with them (tests feed packets here)
    static QList<DnsRecord> parse(const QByteArray &packet);
    static QByteArray query(const QString &serviceType);
    void handle(const QByteArray &packet);

signals:
    void deviceFound(const pxs::oscquery::Device &device);
    void deviceLost(const pxs::oscquery::Device &device);

private:
    void ask();
    void onReadyRead();
    void resolve(const QString &instance);

    QUdpSocket m_socket;
    QTimer m_timer;
    QString m_type; // "_oscjson._tcp.local"
    bool m_running = false;
    QHash<QString, Device> m_devices;       // by instance name (lower case)
    QHash<QString, DnsRecord> m_srv;        // instance -> its SRV
    QHash<QString, QList<QHostAddress>> m_hosts; // host -> every address heard, in the order they came
    static QHostAddress bestAddress(const QList<QHostAddress> &heard); // routable before link-local, stable among equals
    QHash<QString, QString> m_names;        // instance (lower case) -> the name as announced
    QList<QNetworkInterface> m_interfaces;
};

} // namespace pxs::oscquery

Q_DECLARE_METATYPE(pxs::oscquery::Device)
