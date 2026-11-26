#pragma once
// The devices an application drives, the same in every application: pxs-core's device model (pxs/core/device/device.h) with its transport.
//   OSCQuery  found on the network (mDNS) or added by its address: its namespace is read (pxs::oscquery::Client), and its numbers are
//             written over OSC to the port its HOST_INFO says
//   OSC       a plain OSC receiver made by hand: a host, a UDP port, and the addresses it takes (or none: it takes any address, sent as the
//             sender says, sendTo)
//   DMX       a fixture patched in one of the application's universes from a start address, its channels on 8 or 16 bits: the
//             universes are kept here (dmxUniverses) and the application's DMX outputs send them (pxs::dmx::route, Sender)
//   MIDI      an instrument on an output port and a channel: control changes, notes, pitch bends, program changes
// Each device has an id kept across launches by what it is, so that what drives it still does the next time. Id 0 is the application
// itself. Where the devices are saved (the settings, a show file) is the application's: toJson / fromJson.
#include <pxs/Parameter.h>
#include <pxs/core/device/device.h>

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUdpSocket>

#include <map>
#include <memory>
#include <vector>

namespace pxs::oscquery {
class Client;
class Discovery;
struct Device;
} // namespace pxs::oscquery

namespace pxs::midi {
class Output;
}

namespace pxs::devices {

using Device = pxs::core::device::Device;
using Address = pxs::core::device::Address;
using Kind = pxs::core::device::Kind;

class Devices : public QObject
{
    Q_OBJECT
public:
    // `selfName`, `selfPort`: the application's own OSCQuery announcement, never offered as a device
    explicit Devices(const QString &selfName = {}, quint16 selfPort = 0, QObject *parent = nullptr);
    ~Devices() override;

    void startDiscovery(); // the OSCQuery devices of the network come and go

    std::vector<const Device *> all() const; // OSCQuery, OSC, DMX, MIDI, each by name
    const Device *find(quint64 id) const;
    QString nameOf(quint64 id) const; // the application's name for 0; "" for none
    // It can be sent to: an OSCQuery device whose namespace is read, an OSC device whose host is resolved, a MIDI device whose port is
    // open; a DMX device always (its outputs are the application's)
    bool online(quint64 id) const;
    QString status(quint64 id) const; // why it is not online, or what it is ("12 addresses", "Universe 1, 1–6")

    // Made by hand (any kind; an OSCQuery device by its host and HTTP port). A name already taken gets " 2", " 3"… Its id
    quint64 add(Device device);
    quint64 addOscQuery(const QString &host, quint16 httpPort);
    // Edits a device made by hand (by its id): what it is, where it is, its addresses. False: no such device, or not made by hand
    bool set(const Device &device);
    void remove(quint64 id); // an OSCQuery device found on the network comes back when it is found again
    void refresh(quint64 id); // read again (OSCQuery), resolved again (OSC), its port opened again (MIDI)

    // The numbers of a device that can be driven: an OSCQuery device's leaves of one number that can be written, the others' addresses
    std::vector<ParamInfo> params(quint64 id) const;
    bool value(quint64 id, const QString &path, double *v) const; // the last one known (read from the device, or sent)
    // Sends a value in the target protocol's units. Sent again only when it changed (DMX: written in its
    // universe, sent by the outputs). False: no such device or address, or it cannot be reached now
    bool send(quint64 id, const QString &path, double value);
    // To an OSC device, at any path, as `type` says (f, d, i, h, T): the address does not need to be declared. False: not an OSC device,
    // or not reachable now. Every value is sent (a stream of signals)
    bool sendTo(quint64 id, const QString &path, char type, double value);
    int sent() const { return m_sent; } // messages sent since the start (shown)

    // Encoded 8- or 16-bit level in the current DMX universe buffer, when available.
    std::optional<int> dmxLevel(quint64 id, const QString &path) const;
    pxs::core::midi::Bytes midiOutput(quint64 id, const QString &path) const;

    // The DMX universes, by the application's numbering, 512 bytes each: what its DMX devices were last sent
    std::map<int, QByteArray> dmxUniverses() const { return m_universes; }

    // The devices made by hand and the id of every device; and back (they replace the devices made by hand; the ones found on the
    // network stay, with the ids the file keeps for them)
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);

signals:
    void changed();    // a device came, went, was read or edited
    void dmxChanged(); // a universe changed: the outputs have something new to send

private:
    struct Link;
    Link &link(quint64 id);
    void connectDevice(quint64 id); // its transport, as it is now
    void openClient(quint64 id);
    void resolve(quint64 id);
    void found(const pxs::oscquery::Device &device);
    QString uniqueName(const Device &device) const;

    QString m_selfName;
    quint16 m_selfPort;
    pxs::core::device::Registry m_registry;
    std::map<quint64, std::unique_ptr<Link>> m_links;
    std::map<QString, std::unique_ptr<pxs::midi::Output>> m_midi; // by port name
    std::map<int, QByteArray> m_universes;
    pxs::oscquery::Discovery *m_discovery = nullptr;
    QUdpSocket m_udp;
    int m_sent = 0;
};

} // namespace pxs::devices
