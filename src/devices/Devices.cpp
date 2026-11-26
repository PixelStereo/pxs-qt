#include <pxs/Devices.h>

#include <pxs/Client.h>
#include <pxs/Discovery.h>
#include <pxs/Midi.h>
#include <pxs/core/dmx/frame.h>

#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>

#include <algorithm>
#include <cmath>

namespace pxs::devices {

namespace core = pxs::core::device;
using pxs::oscquery::Client;
using pxs::oscquery::RemoteNode;

namespace {

std::string utf8(const QString &s) { return s.toUtf8().toStdString(); }
QString text(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

QByteArray bytes(const std::vector<std::uint8_t> &b) { return QByteArray(reinterpret_cast<const char *>(b.data()), qsizetype(b.size())); }

// The leaves of one number that can be written, as an editor names them ("Mixer › Fader 1"), by their path in the device
void collect(const RemoteNode &node, const QString &category, std::vector<ParamInfo> &out)
{
    for (const auto &[name, child] : node.children) {
        pxs::core::tree::ParamInfo core;
        if (child.isLeaf() && (child.access & 2) && child.kind != QLatin1String("signal") && child.toParamInfo(&core) &&
            (core.type == pxs::core::tree::ParamInfo::Type::Float || core.type == pxs::core::tree::ParamInfo::Type::Int ||
             core.type == pxs::core::tree::ParamInfo::Type::Bool)) {
            ParamInfo p = ParamInfo::fromCore(core);
            p.path = child.path;
            p.label = category.isEmpty() ? child.name : category + QStringLiteral(" › ") + child.name;
            p.animatable = true;
            if (p.type == ParamInfo::Type::Bool) { // driven as 0..1: on from the middle
                p.type = ParamInfo::Type::Float;
                p.min = p.lo = 0;
                p.max = p.hi = 1;
            }
            out.push_back(p);
        }
        collect(child, category.isEmpty() ? child.name : category + QStringLiteral(" › ") + child.name, out);
    }
}

} // namespace

// What a device is connected through, as it is now
struct Devices::Link {
    std::unique_ptr<Client> client; // OSCQuery
    QHostAddress address;           // OSC: resolved
    bool ready = false;             // OSCQuery: its namespace is read
    QString error;                  // why it cannot be sent to
};

Devices::Devices(const QString &selfName, quint16 selfPort, QObject *parent) : QObject(parent), m_selfName(selfName), m_selfPort(selfPort)
{
    m_udp.bind(QHostAddress::AnyIPv4, 0);
}

Devices::~Devices() = default;

Devices::Link &Devices::link(quint64 id)
{
    auto &l = m_links[id];
    if (!l) l = std::make_unique<Link>();
    return *l;
}

void Devices::startDiscovery()
{
    if (m_discovery) return;
    m_discovery = new pxs::oscquery::Discovery(this);
    connect(m_discovery, &pxs::oscquery::Discovery::deviceFound, this, [this](const pxs::oscquery::Device &d) { found(d); });
    connect(m_discovery, &pxs::oscquery::Discovery::deviceLost, this, [this](const pxs::oscquery::Device &lost) {
        for (const Device *d : m_registry.all())
            if (d->kind == Kind::OscQuery && !d->manual && text(d->name) == lost.name) {
                link(d->id).ready = false;
                link(d->id).error = QStringLiteral("Gone from the network");
                emit changed();
            }
    });
    m_discovery->start();
}

void Devices::found(const pxs::oscquery::Device &device)
{
    // Not the application itself: its own name, or its own port on one of this computer's addresses (a loopback, or one the
    // interfaces hold). A host name that starts like this one's is not enough: "studio" also starts "studio-rehearsal".
    if (device.name == m_selfName ||
        (m_selfPort && device.port == m_selfPort && (device.address.isLoopback() || QNetworkInterface::allAddresses().contains(device.address))))
        return;
    const QString host = device.address.isNull() ? device.host : device.address.toString();
    for (const Device *d : m_registry.all())
        if (d->kind == Kind::OscQuery && !d->manual && text(d->name) == device.name) {
            if (text(d->host) != host || d->port != device.port || !link(d->id).client || !link(d->id).ready) {
                Device moved = *d;
                moved.host = utf8(host);
                moved.port = device.port;
                const auto id = m_registry.put(moved);
                openClient(id);
            }
            return;
        }
    Device d;
    d.kind = Kind::OscQuery;
    d.manual = false;
    d.name = utf8(device.name);
    d.host = utf8(host);
    d.port = device.port;
    const quint64 id = m_registry.put(d);
    openClient(id);
    emit changed();
}

void Devices::openClient(quint64 id)
{
    const Device *d = m_registry.find(id);
    if (!d || d->kind != Kind::OscQuery) return;
    Link &l = link(id);
    if (!l.client) {
        l.client = std::make_unique<Client>();
        Client *c = l.client.get();
        connect(c, &Client::ready, this, [this, id, c] {
            Link &x = link(id);
            x.ready = true;
            x.error.clear();
            if (const Device *dev = m_registry.find(id); dev && dev->manual && !c->name().isEmpty() && text(dev->name) != c->name()) {
                Device named = *dev;
                named.name = utf8(c->name()); // its id stays: it is known by its host and port
                m_registry.put(named);
            }
            emit changed();
        });
        connect(c, &Client::treeChanged, this, &Devices::changed);
        connect(c, &Client::failed, this, [this, id](const QString &reason) {
            Link &x = link(id);
            x.ready = false;
            x.error = reason;
            emit changed();
        });
    }
    l.ready = false;
    l.error = QStringLiteral("Reading its namespace…");
    m_registry.forgetSent(id);
    l.client->open(text(d->host), d->port);
}

void Devices::resolve(quint64 id)
{
    const Device *d = m_registry.find(id);
    if (!d) return;
    Link &l = link(id);
    l.address = QHostAddress();
    const QString host = text(d->host);
    if (host.isEmpty()) {
        l.error = QStringLiteral("No host");
        return;
    }
    QHostAddress direct;
    if (direct.setAddress(host)) {
        l.address = direct;
        l.error.clear();
        return;
    }
    l.error = QStringLiteral("Looking for ") + host + QStringLiteral("…");
    QHostInfo::lookupHost(host, this, [this, id, host](const QHostInfo &info) {
        const Device *x = m_registry.find(id);
        if (!x || text(x->host) != host) return;
        Link &l = link(id);
        for (const QHostAddress &a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) {
                l.address = a;
                break;
            }
        l.error = l.address.isNull() ? QStringLiteral("Cannot find ") + host : QString();
        emit changed();
    });
}

void Devices::connectDevice(quint64 id)
{
    const Device *d = m_registry.find(id);
    if (!d) return;
    m_registry.forgetSent(id);
    Link &l = link(id);
    l.error = text(d->validate());
    switch (d->kind) {
    case Kind::OscQuery: openClient(id); break;
    case Kind::Osc:
        if (l.error.isEmpty()) resolve(id);
        break;
    case Kind::Dmx: break;
    case Kind::Midi: {
        if (!l.error.isEmpty()) break;
        if (!pxs::midi::available()) {
            l.error = QStringLiteral("Built without MIDI");
            break;
        }
        const QString port = text(d->midiPort);
        auto &out = m_midi[port];
        if (!out) {
            out = std::make_unique<pxs::midi::Output>();
            QString err;
            if (!out->open(port, &err)) {
                out.reset();
                l.error = err.isEmpty() ? QStringLiteral("Cannot open the MIDI port") : err;
            }
        }
        break;
    }
    }
}

std::vector<const Device *> Devices::all() const { return m_registry.all(); }

const Device *Devices::find(quint64 id) const { return m_registry.find(id); }

QString Devices::nameOf(quint64 id) const
{
    if (id == 0) return m_selfName;
    const Device *d = m_registry.find(id);
    return d ? text(d->name) : QString();
}

bool Devices::online(quint64 id) const
{
    const Device *d = m_registry.find(id);
    if (!d) return false;
    const auto it = m_links.find(id);
    const Link *l = it == m_links.end() ? nullptr : it->second.get();
    switch (d->kind) {
    case Kind::OscQuery: return l && l->ready;
    case Kind::Osc: return l && !l->address.isNull() && l->error.isEmpty();
    case Kind::Dmx: return d->validate().empty();
    case Kind::Midi: return l && l->error.isEmpty() && m_midi.count(text(d->midiPort)) && m_midi.at(text(d->midiPort));
    }
    return false;
}

QString Devices::status(quint64 id) const
{
    const Device *d = m_registry.find(id);
    if (!d) return {};
    const auto it = m_links.find(id);
    if (!online(id) && it != m_links.end() && !it->second->error.isEmpty()) return it->second->error;
    if (!online(id)) return d->kind == Kind::OscQuery ? QStringLiteral("Not read yet") : QString::fromStdString(d->validate());
    const auto n = d->kind == Kind::OscQuery ? params(id).size() : d->addresses.size();
    const QString count = d->kind == Kind::Osc && n == 0 ? QStringLiteral("Any address") : QStringLiteral("%1 address%2").arg(n).arg(n == 1 ? "" : "es");
    if (d->kind == Kind::Dmx)
        return QStringLiteral("Universe %1, channels %2–%3").arg(d->universe).arg(d->start).arg(d->start + std::max(1, d->footprint()) - 1);
    return count;
}

QString Devices::uniqueName(const Device &device) const
{
    const QString base = text(device.name).trimmed().isEmpty() ? QString::fromLatin1(core::name(device.kind)) : text(device.name).trimmed();
    auto taken = [&](const QString &name) {
        for (const Device *d : m_registry.all())
            if (d->id != device.id && d->kind == device.kind && text(d->name) == name) return true;
        return false;
    };
    QString name = base;
    for (int n = 2; taken(name); ++n) name = QStringLiteral("%1 %2").arg(base).arg(n);
    return name;
}

quint64 Devices::add(Device device)
{
    device.id = 0;
    device.manual = true;
    if (device.kind == Kind::OscQuery) return addOscQuery(text(device.host), device.port);
    device.name = utf8(uniqueName(device));
    const quint64 id = m_registry.put(device);
    connectDevice(id);
    emit changed();
    return id;
}

quint64 Devices::addOscQuery(const QString &host, quint16 httpPort)
{
    Device d;
    d.kind = Kind::OscQuery;
    d.manual = true;
    d.host = utf8(host.trimmed());
    d.port = httpPort;
    d.name = utf8(QStringLiteral("%1:%2").arg(host.trimmed()).arg(httpPort));
    for (const Device *x : m_registry.all())
        if (x->key() == d.key()) return x->id;
    const quint64 id = m_registry.put(d);
    connectDevice(id);
    emit changed();
    return id;
}

bool Devices::set(const Device &device)
{
    const Device *was = m_registry.find(device.id);
    if (!was || !was->manual || was->kind != device.kind) return false;
    Device d = device;
    d.manual = true;
    if (d.kind != Kind::OscQuery) d.name = utf8(uniqueName(d));
    const bool moved = d.host != was->host || d.port != was->port || d.midiPort != was->midiPort;
    const bool dmxMoved = d.kind == Kind::Dmx && (d.universe != was->universe || d.start != was->start || d.addresses != was->addresses);
    if (dmxMoved) { // what it took of its universe is let go
        auto u = m_universes.find(was->universe);
        if (u != m_universes.end())
            for (const Address &a : was->addresses) {
                const auto [first, width] = core::dmxChannels(*was, a);
                for (int c = 0; c < width && first; ++c) u->second[first - 1 + c] = 0;
            }
    }
    m_registry.put(d);
    if (moved || d.kind == Kind::Osc || d.kind == Kind::Midi) connectDevice(d.id);
    else m_registry.forgetSent(d.id);
    emit changed();
    if (dmxMoved) emit dmxChanged();
    return true;
}

void Devices::remove(quint64 id)
{
    const Device *d = m_registry.find(id);
    if (!d) return;
    const bool dmx = d->kind == Kind::Dmx;
    if (dmx) {
        auto u = m_universes.find(d->universe);
        if (u != m_universes.end())
            for (const Address &a : d->addresses) {
                const auto [first, width] = core::dmxChannels(*d, a);
                for (int c = 0; c < width && first; ++c) u->second[first - 1 + c] = 0;
            }
    }
    m_links.erase(id);
    m_registry.remove(id);
    emit changed();
    if (dmx) emit dmxChanged();
}

void Devices::refresh(quint64 id)
{
    if (const Device *d = m_registry.find(id); d && d->kind == Kind::Midi) m_midi.erase(text(d->midiPort)); // opened again
    connectDevice(id);
    emit changed();
}

std::vector<ParamInfo> Devices::params(quint64 id) const
{
    std::vector<ParamInfo> out;
    const Device *d = m_registry.find(id);
    if (!d) return out;
    if (d->kind == Kind::OscQuery) {
        const auto it = m_links.find(id);
        if (it != m_links.end() && it->second->client && it->second->client->tree().loaded()) collect(it->second->client->tree().root(), QString(), out);
        return out;
    }
    for (const Address &a : d->addresses) {
        ParamInfo p;
        p.path = text(a.path);
        p.label = p.path;
        const bool onOff = d->kind == Kind::Osc && a.type == 'T';
        const bool whole = (d->kind == Kind::Osc && (a.type == 'i' || a.type == 'h')) || (d->kind == Kind::Midi && a.message == pxs::core::midi::Kind::ProgramChange);
        p.type = whole ? ParamInfo::Type::Int : ParamInfo::Type::Float;
        p.min = p.lo = a.min;
        p.max = p.hi = a.max;
        p.step = whole || onOff ? 1 : 0.01;
        p.animatable = true;
        out.push_back(p);
    }
    return out;
}

bool Devices::value(quint64 id, const QString &path, double *v) const
{
    const Device *d = m_registry.find(id);
    if (!d || !v) return false;
    const auto it = m_links.find(id);
    if (it != m_links.end() && it->second->client) {
        if (const RemoteNode *n = it->second->client->tree().find(path); n && !n->value.isEmpty()) {
            const QJsonValue x = n->value.first();
            *v = x.isBool() ? (x.toBool() ? 1 : 0) : x.toDouble();
            return true;
        }
    }
    if (const auto last = m_registry.last(id, utf8(path))) {
        *v = *last;
        return true;
    }
    if (const Address *a = d->address(utf8(path))) {
        *v = a->min;
        return true;
    }
    return false;
}

std::optional<int> Devices::dmxLevel(quint64 id, const QString &path) const
{
    const Device *device = m_registry.find(id);
    if (!device || device->kind != Kind::Dmx) return std::nullopt;
    const auto *address = device->address(utf8(path));
    if (!address) return std::nullopt;
    const auto [channel, width] = core::dmxChannels(*device, *address);
    const auto universe = m_universes.find(device->universe);
    if (!channel || universe == m_universes.end() || universe->second.size() < channel - 1 + width) return std::nullopt;
    const auto coarse = static_cast<unsigned char>(universe->second.at(channel - 1));
    return width == 2 ? (int(coarse) << 8) | static_cast<unsigned char>(universe->second.at(channel)) : int(coarse);
}

bool Devices::send(quint64 id, const QString &path, double value)
{
    const Device *d = m_registry.find(id);
    if (!d || !std::isfinite(value)) return false;
    const std::string p = utf8(path);
    if (d->kind == Kind::Dmx) {
        const Address *a = d->address(p);
        if (!a) return false;
        QByteArray &u = m_universes[d->universe];
        if (u.size() != pxs::core::dmx::kChannelCount) u = QByteArray(pxs::core::dmx::kChannelCount, '\0');
        const QByteArray before = u;
        if (!core::writeDmx(*d, *a, value, reinterpret_cast<std::uint8_t *>(u.data()), std::size_t(u.size()))) return false;
        m_registry.changed(id, p, value);
        if (u != before) emit dmxChanged();
        return true;
    }
    if (const auto last = m_registry.last(id, p); last && std::abs(*last - value) < 1e-9) return true; // the same: not again
    if (!online(id)) return false;
    bool done = false;
    if (d->kind == Kind::OscQuery) {
        Client *c = m_links.at(id)->client.get();
        const RemoteNode *n = c->tree().find(path);
        if (!n) return false;
        const QJsonValue arg = n->type == QLatin1String("T") || n->type == QLatin1String("F") ? QJsonValue(value >= 0.5) : QJsonValue(value);
        done = c->write(path, QJsonArray{arg});
    } else if (d->kind == Kind::Osc) {
        const Address *a = d->address(p);
        if (!a) return false;
        done = m_udp.writeDatagram(bytes(pxs::core::osc::encode(core::oscMessage(*a, value))), m_links.at(id)->address, d->port) > 0;
    } else if (d->kind == Kind::Midi) {
        const Address *a = d->address(p);
        if (!a) return false;
        const auto message = core::midiBytes(*d, *a, value);
        if (!message.empty()) m_midi.at(text(d->midiPort))->send(message);
        done = true;
    }
    if (done) {
        m_registry.changed(id, p, value);
        ++m_sent;
    }
    return done;
}

bool Devices::sendTo(quint64 id, const QString &path, char type, double value)
{
    const Device *d = m_registry.find(id);
    if (!d || d->kind != Kind::Osc || !online(id) || !std::isfinite(value)) return false;
    if (m_udp.writeDatagram(bytes(pxs::core::osc::encode(core::oscMessage(utf8(path), type, value))), m_links.at(id)->address, d->port) <= 0) return false;
    ++m_sent;
    return true;
}

QJsonObject Devices::toJson() const { return QJsonDocument::fromJson(QByteArray::fromStdString(m_registry.toJson())).object(); }

bool Devices::fromJson(const QJsonObject &json)
{
    if (!m_registry.fromJson(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString())) return false;
    // The links follow the devices: the ids of the ones found on the network may have changed
    m_links.clear();
    m_midi.clear();
    m_universes.clear();
    for (const Device *d : m_registry.all()) {
        connectDevice(d->id);
    }
    emit changed();
    emit dmxChanged();
    return true;
}

} // namespace pxs::devices
