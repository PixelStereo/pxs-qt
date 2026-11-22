#pragma once
// The server of a controls page: a small web server an application starts so that a phone, a tablet, a browser or one of its own windows
// can lay out and play its controls (ControlsPage.h), live both ways. HTTP and WebSocket by hand, nothing else installed.
//
// What it is, and is not: it moves the controls the application gives it (Backend) and nothing else. It asks a code (the "PIN": 4 to 32
// letters or digits, chosen by the operator, or none), compared in constant time; a machine that gets it wrong eight times is refused for a
// minute. A WebSocket is accepted only from the page this server served (same origin); the page's script and style are the only code its
// Content-Security-Policy allows. No encryption: the PIN and the values cross the network in clear, as OSC does.
//
// Protocol (text frames, JSON), for those who want their own client:
//   GET /       the application's own front page when it has one (servePage), else /control
//   GET /info          {"pin":true|false}: whether a code is asked
//   GET /ws?pin=<PIN>  upgrade to a WebSocket (no pin= when none is asked)
//   server -> {"t":"controls","title":..,"show":true|false,"list":[Control::wireJson()…]}   at connection, and when a control is added, removed or shaped
//   server -> {"t":"values","v":{"<id>":<number>|[numbers]}}              what changed
//   client -> {"t":"set","id":"<id>","v":<number>}   {"t":"color","id":..,"rgb":[r,g,b]}   {"t":"point","id":..,"xy":[x,y]}
//             {"t":"points","id":..,"v":[x1,y1,x2,y2…]}   {"t":"multi","id":..,"v":[…]}   {"t":"adsr","id":..,"v":[a,d,s,r]}
//   client -> {"t":"physicsWidget","id":"..","key":"friction"}   a slider for a physics setting of that control (a ball: "2.friction")
//   client -> {"t":"reset","id":".."}   the control's reset button
//   client -> {"t":"undo"} / {"t":"redo"}   the page's last change, back and forth (Backend::restore)
//   client -> {"t":"watch","id":".."}   the filters of this tile are watched (0: none)   server -> {"t":"stages","id":..,"in":x,"v":[..]}
//   client -> {"t":"rel","id":"..."}   the hand let go of a control: its physics (Control::physics) takes over
//   client -> {"t":"drop","data":"<text>","onto":"<id>","page":"<id>","x":..,"y":..}   something dragged onto the page from outside (onto: the macro it fell on) (the application's own message: `message()`)
//   client -> {"t":"layout","items":{"<id>":{"x","y","w","h"}}}   {"t":"edit","id":..,"props":{…}} (Control::applyProps)   {"t":"remove","id":..}
//   client -> {"t":"remove","ids":["..",".."]}   several at once (one step of undo)
//   client -> {"t":"copy","ids":[..]} / {"t":"cut","ids":[..]} / {"t":"paste","page":"<id>"}   the clipboard is the server's, for every client
//   client <-> {"t":"showPage","id":".."} / server -> {"t":"page","id":".."}   with setSharedPage: the current page, the same for every client
// An application adds its own pages and messages through the hooks (servePage, message, tick).
#include "Control.h"
#include "Physics.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <array>
#include <optional>
#include <vector>

class QTcpServer;
class QTcpSocket;

namespace pxs::controls {

// What the server asks of the application: its controls, as they are now, and what the page sets. Every setter says whether something
// changed. Called on the server's thread: an application with another thread guards its own state.
class Backend
{
public:
    virtual ~Backend() = default;
    virtual std::vector<Control> controls() const = 0; // with their values as they are now
    virtual std::optional<Control> control(quint64 id) const = 0;
    virtual quint64 revision() const = 0;              // changes with the list or a definition, not with a value
    virtual QString title() const = 0;
    virtual bool setValue(quint64 id, double value) = 0;
    virtual bool setPoint(quint64 id, double x, double y) = 0;
    virtual bool setPoints(quint64 id, const std::vector<double> &xy) = 0;
    virtual bool setMulti(quint64 id, const std::vector<double> &values) = 0;
    virtual bool setAdsr(quint64 id, const std::array<double, 4> &values) = 0;
    virtual bool setColor(quint64 id, double r, double g, double b) = 0;
    virtual bool setLayouts(const QHash<quint64, std::array<int, 4>> &places) = 0; // id -> x, y, w, h
    virtual bool update(quint64 id, const Control &changed) = 0;                    // a control's settings changed on the page
    virtual bool remove(quint64 id) = 0;
    virtual quint64 add(const Control &control) { Q_UNUSED(control) return 0; } // a control made on the page (a widget for a physics setting): its id, 0: not added
    // The value after each filter of a tile, as it is now (the page watches the tile whose inspector is open). None: no filter, or nothing set yet
    virtual std::optional<FilterTrace> filterTrace(quint64 id) const { Q_UNUSED(id) return std::nullopt; }
    virtual bool reset(quint64 id) { Q_UNUSED(id) return false; }               // the control's reset button: the parameters it shows go back to their defaults
    // The whole list as it was (undo, redo): its controls, their places and settings. False: this application does not undo the page
    virtual bool restore(const std::vector<Control> &controls) { Q_UNUSED(controls) return false; }
};

class Server : public QObject
{
    Q_OBJECT
public:
    explicit Server(Backend *backend = nullptr, QObject *parent = nullptr);
    ~Server() override;

    // Port 0: chosen by the system. `pin` empty: no code asked. `listen`: the interface's address ("": all). False with status() when it cannot
    bool start(quint16 port, const QString &pin, const QString &listen = QString());
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString status() const { return m_status; }
    int clientCount() const;
    virtual QStringList urls() const; // where to point a phone: one address per network interface
    static QString makePin(int digits = 6);
    static bool pinValid(const QString &pin); // 4 to 32 letters or digits (an empty one is "no code": start() takes it)
    void setName(const QString &name) { m_name = name; }           // what the status calls it ("Remote")
    void setPageTitle(const QString &title) { m_pageTitle = title; } // the page's title in a browser's tab
    // The controls page may be framed by a page of this same server (an application's front page with the page beside its own panel). Call before start()
    void setFrameable(bool on) { m_frameable = on; }
    // The size {w, h} in grid cells a control is given when it has no place yet, by widget ({"vfader": [2, 5], ...}); the page's own sizes for the others
    void setDefaultSizes(const QJsonObject &sizes);
    // How the page behaves: "inspector" ("always": in edit mode the inspector is always there; "onDemand": it opens from a tile) and "physics" (the widget
    // a physics setting is given when it is added, an encoder by default)
    // How many words of an address (between slashes) a tile shows as its name: the last two by default, 1 to 4
    void setNameParts(int parts);
    void setPageOptions(const QString &inspector, const QString &physicsWidget);
    // One current page for every client: a tab clicked on one page shows on all of them (off: each client has its own). The page is a top-level
    // Group's id; setCurrentPage tells everybody (an application's own command), pageShown says a client chose one
    void setSharedPage(bool on);
    bool sharedPage() const { return m_sharedPage; }
    quint64 currentPage() const { return m_currentPage; }
    void setCurrentPage(quint64 id);
    // The largest message a client may send (4 KB: a control's settings). An application that takes a whole document raises it
    void setMaxFrame(quint64 bytes) { m_maxFrame = bytes; }

    // The controls page as served, and the policy that lets only its code run
    static QByteArray page(const QString &title);
    // `extra`: more directives (an application's page that frames this one: "frame-src 'self'")
    static QByteArray policy(const QByteArray &script, const QByteArray &style, const QByteArray &extra = {}, bool frameable = false);

signals:
    void clientsChanged(int count);
    void layoutEdited(); // a client laid the page out, set a tile or removed a control: the application is modified
    void controlMoved(); // a control was set from a client
    void pageShown(quint64 id); // the current page changed (a client chose it, or setCurrentPage)

protected:
    // Hooks: another page of the application (true: served as `html` under `csp`), another message of a client (true: taken), what to do
    // every 50 ms before the controls, a client gone
    virtual bool hasShowPage() const { return false; } // the application serves a /show page: the page links to it
    virtual bool servePage(const QString &path, QByteArray *html, QByteArray *csp);
    virtual bool message(QTcpSocket *client, const QString &type, const QJsonObject &o);
    virtual void tick();
    virtual void clientGone(QTcpSocket *client);
    void setBackend(Backend *backend) { m_backend = backend; } // for an application that makes its backend after the server
    void sendText(QTcpSocket *client, const QByteArray &json) const;
    QList<QTcpSocket *> websockets() const;
    void checkpoint(const QString &what = QString()); // before a change of the page: what undo goes back to (the same `what` in a row: one step)
    bool undo();
    bool redo();
    void leaveControls(QTcpSocket *client); // this client has pages of its own: the controls are not pushed to it
    static QByteArray response(int status, const QByteArray &body, const QByteArray &type, const QByteArray &extra = {}, bool frameable = false);

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        bool websocket = false;
        bool ownPages = false;                 // see leaveControls
        quint64 controlsSeen = ~0ull;
        QHash<quint64, std::vector<double>> sent; // the values the client has
        double budget = 300;                   // messages it may still send this second
        quint64 watching = 0;                  // the tile whose filters it watches (its inspector is open)
        QByteArray traceSeen;
    };
    struct Attempts {
        int failures = 0;
        qint64 until = 0; // ms since epoch
    };
    void onConnection();
    void onData(QTcpSocket *s);
    void onFrame(Client &c, int opcode, const QByteArray &payload);
    void push();
    bool intercept(quint64 id, const QString &type, const QJsonObject &o); // a control with a physics: the hand sets where it goes, not where it is
    void released(quint64 id, int ball = -1);                                          // the hand let go: its physics goes on by itself
    void stepPhysics();
    void send(QTcpSocket *s, const QByteArray &frame) const;
    QByteArray controlsMessage(const std::vector<Control> &controls) const;
    bool authorized(const QByteArray &presented) const;

    Backend *m_backend;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Client> m_clients;
    QTimer m_timer, m_physics;
    QElapsedTimer m_clock;
    QHash<quint64, physics::Body> m_bodies;
    std::vector<std::vector<Control>> m_undo, m_redo;
    QString m_lastWhat;
    qint64 m_lastAt = 0;
    QString m_pin, m_status, m_name = QStringLiteral("Remote"), m_pageTitle = QStringLiteral("Controls");
    QByteArray m_csp;
    bool m_frameable = false;
    QJsonObject m_sizes;
    int m_nameParts = 2;
    QString m_inspector = QStringLiteral("always"), m_physicsWidget = QStringLiteral("knob");
    bool m_sharedPage = false;
    quint64 m_currentPage = 0;
    std::vector<Control> m_clipboard; // copied or cut tiles (with what a section holds), for every client
    void pasteClipboard(quint64 page);
    quint64 m_maxFrame = 4096;
    QHash<QString, Attempts> m_attempts; // by address
};

} // namespace pxs::controls
