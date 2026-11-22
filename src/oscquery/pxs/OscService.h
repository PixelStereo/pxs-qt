#pragma once

// What an application needs to be reachable like every other Pixel Stereo application: an OSC port (UDP) that applies
// what it receives to the tree, the OSCQuery HTTP side, and the announcement on the local network (mDNS). What the
// application says goes back over UDP, see core::tree::Tree::setOutput:
//   a parameter that changed (the feedback) -> the senders that wrote to it, and the targets
//   a signal fired                          -> the targets only (a stream: nobody gets it by merely writing a parameter)
// And the WebSocket of the OSCQuery port: a client that LISTENs to an address is streamed its values (a parameter written
// or changed, a signal fired, from any source), what it sends is applied like a UDP packet, and every client is told
// PATH_ADDED / PATH_REMOVED when the address space changes (a node made or removed), coalesced over a short while; PATH_CHANGED
// for a tree cleared or many changes at once (libossia's client follows PATH_ADDED and PATH_REMOVED, and ignores PATH_CHANGED).

#include <pxs/MdnsResponder.h>
#include <pxs/QueryServer.h>
#include <pxs/core/osc/codec.h>
#include <pxs/core/tree/tree.h>

#include <OscCodec.h>

#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>
#include <atomic>

namespace pxs {

class OscService : public QObject {
    Q_OBJECT
public:
    OscService(core::tree::Tree *tree, const QString &name, QObject *parent = nullptr);
    ~OscService() override;

    // Reachable from this computer only (before start()): the UDP port and the HTTP / WebSocket port are bound to the loopback, nothing
    // is announced on the network, and no page of another site may read the answers. Off by default: an application on a show network.
    void setLocalOnly(bool on) { m_localOnly = on; }
    // oscPort/httpPort 0: any free port. False (with a log message) when one cannot be opened.
    bool start(quint16 oscPort, quint16 httpPort, bool announce = true);
    void stop();
    bool isRunning() const { return m_udp.state() == QAbstractSocket::BoundState; }
    quint16 oscPort() const { return m_udp.localPort(); }
    quint16 httpPort() const { return m_http.port(); }

    // Where the signals go (and the feedback too): the application's own destinations
    void addTarget(const QHostAddress &address, quint16 port);
    void setTargets(const QList<QPair<QHostAddress, quint16>> &targets) { m_targets = targets; }
    void clearTargets();
    // The feedback also goes to whoever wrote to the application, on the port it wrote from (on by default)
    void setLearnSenders(bool on) { m_learn = on; if (!on) m_learned.clear(); }

    QueryServer &query() { return m_http; }

signals:
    void messageReceived(const osc::Message &message, int accepted); // after it was applied to the tree
    void logMessage(const QString &text);

private:
    void onReadyRead();
    void send(const core::osc::Message &message);
    void apply(const QByteArray &packet, const QHostAddress *from, quint16 port);
    void onChanged(const core::tree::Node &node, const core::ValueList &args); // from any thread
    void onStructure(const std::string &path, core::tree::Tree::Change change); // from any thread
    void onPathTimer();

    core::tree::Tree *m_tree;
    QString m_name;
    QUdpSocket m_udp;
    QueryServer m_http;
    MdnsResponder m_mdns;
    QList<QPair<QHostAddress, quint16>> m_targets, m_learned;
    bool m_learn = true;
    bool m_localOnly = false;
    int m_observer = 0;
    std::atomic<int> m_webSockets{0}; // read by the thread that writes: nothing is built when nobody listens
    QTimer m_pathTimer;
    QList<QPair<QString, core::tree::Tree::Change>> m_pending; // the structure changes since the last message to the clients
};

} // namespace pxs
