#pragma once

// The HTTP side of OSCQuery, answered from a pxs::core::tree::Tree: GET <osc path>[?VALUE|?TYPE|...] and GET /?HOST_INFO, in JSON.
// Status: 200, 204, 400, 404, 405. Reading is open to a web page (CORS).
//
// The same port takes a WebSocket (the proposal's optional bi-directional side). A client that sends {"COMMAND": "LISTEN",
// "DATA": "/path"} is streamed that method's values as binary OSC packets until it sends IGNORE; its binary frames are OSC
// packets for the application (packetReceived). The server tells every client {"COMMAND": "PATH_CHANGED", "DATA": "/path"}
// when the address space changes (broadcast). A web page may not open one unless it comes from this machine: a socket writes.

#include <pxs/core/tree/tree.h>

#include <QJsonObject>

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>

namespace pxs {

class QueryServer : public QObject {
    Q_OBJECT
public:
    explicit QueryServer(const core::tree::Tree *tree, QObject *parent = nullptr);

    bool start(quint16 port); // 0: any free port
    void stop();
    bool isListening() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }

    void setName(const QString &name) { m_name = name; }
    void setOscPort(quint16 port) { m_oscPort = port; }
    void setExtensions(const QJsonObject &extensions) { m_extensions = extensions; }
    void setHostInfoMore(const QJsonObject &more) { m_more = more; } // other HOST_INFO members (WS_PORT)
    void setBindAddress(const QHostAddress &address) { m_bind = address; } // before start(): any interface (the default), or one
    // Whether a web page of another site may read what this answers (Access-Control-Allow-Origin: *, on by default: a browser
    // client of the proposal, oscqueryhtml, is such a page). Off: only a page served by this server, and native clients, read it.
    void setCorsOpen(bool open) { m_cors = open; }

    // The HTTP answer to a request line target, public for tests
    struct Response {
        int status = 200;
        QByteArray body;
    };
    Response answer(const QString &method, const QByteArray &target) const;

    int webSocketCount() const;
    // A binary OSC packet to the WebSocket clients that listen to this exact address (a slow client misses it)
    void stream(const QString &address, const QByteArray &packet);
    // A JSON text to every WebSocket client (PATH_CHANGED)
    void broadcast(const QByteArray &json);
    // Whether a page of this Origin may open a WebSocket: none (a native client), or this machine's own
    static bool originAllowed(const QByteArray &origin);

signals:
    void logMessage(const QString &text);
    void packetReceived(const QByteArray &packet); // a binary frame of a WebSocket client
    void webSocketsChanged(int count);             // a WebSocket client came or went

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    bool upgrade(QTcpSocket *socket, const QList<QByteArray> &request, const QHash<QByteArray, QByteArray> &headers);
    void command(QTcpSocket *socket, const QByteArray &json);

    const core::tree::Tree *m_tree;
    QTcpServer m_server;
    struct Client {
        QByteArray buffer;
        bool websocket = false;
        QSet<QString> listening;
    };
    QHash<QTcpSocket *, Client> m_clients;
    QString m_name = QStringLiteral("pxs");
    quint16 m_oscPort = 0;
    QJsonObject m_extensions;
    QJsonObject m_more;
    QHostAddress m_bind = QHostAddress::Any;
    bool m_cors = true;
};

} // namespace pxs
