// Devices: an OSC device sends its addresses (as their type says, again only when changed) and an open one any address; a DMX fixture
// writes its universe on 8 and 16 bits; what was made is read back with its ids; the panel shows each kind
#include <pxs/Devices.h>
#include <pxs/DeviceTargetMenu.h>
#include <pxs/DevicesPanel.h>
#include <pxs/Discovery.h>
#include <pxs/MdnsResponder.h>

#include <OscCodec.h>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUdpSocket>

#include <cstdio>
#include <functional>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs::devices;

static void pump(const std::function<bool()> &until, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// The messages a socket received, decoded
static std::vector<osc::Message> received(QUdpSocket &socket, size_t count)
{
    std::vector<osc::Message> out;
    pump([&] {
        while (socket.hasPendingDatagrams()) {
            QByteArray packet(qsizetype(socket.pendingDatagramSize()), '\0');
            socket.readDatagram(packet.data(), packet.size());
            osc::decode(packet, out);
        }
        return out.size() >= count;
    });
    return out;
}

static void parameterMenu()
{
    Devices devices;
    Device device;
    device.kind = Kind::Dmx;
    device.name = "Fixture";
    Address branch, leaf;
    branch.path = "/color";
    leaf.path = "/color/red";
    leaf.channel = 2;
    device.addresses = {branch, leaf};
    const quint64 id = devices.add(device);
    QMenu menu;
    quint64 selected = 0;
    QString path;
    addDeviceTargets(&menu, devices, &menu, [&](quint64 deviceId, const ParamInfo &p) {
        selected = deviceId;
        path = p.path;
    }, id, "/color/red");

    CHECK(menu.actions().size() == 1);
    if (menu.actions().size() != 1 || !menu.actions().front()->menu()) return;
    const auto branches = menu.actions().front()->menu()->actions();
    CHECK(branches.size() == 1 && branches.front()->text() == "color");
    if (branches.size() != 1 || !branches.front()->menu()) return;
    const auto actions = branches.front()->menu()->actions();
    CHECK(actions.size() == 2);
    if (actions.size() != 2) return;
    CHECK(actions.front()->text() == "/color" && !actions.front()->isChecked());
    CHECK(actions.back()->text() == "red" && actions.back()->isChecked());
    actions.back()->trigger();
    CHECK(selected == id && path == "/color/red");
}

static void oscDevices()
{
    QUdpSocket receiver;
    CHECK(receiver.bind(QHostAddress::LocalHost, 0));
    Devices devices(QStringLiteral("Test"));
    Device d;
    d.kind = Kind::Osc;
    d.name = "Mixer";
    d.host = "127.0.0.1";
    d.port = receiver.localPort();
    Address fader;
    fader.path = "/fader";
    Address mute;
    mute.path = "/mute";
    mute.type = 'T';
    Address scene;
    scene.path = "/scene";
    scene.type = 'i';
    scene.max = 10;
    d.addresses = {fader, mute, scene};
    const quint64 id = devices.add(d);
    CHECK(id != 0 && devices.online(id));
    CHECK(devices.nameOf(0) == QStringLiteral("Test") && devices.nameOf(id) == QStringLiteral("Mixer"));
    CHECK(devices.params(id).size() == 3);

    CHECK(devices.send(id, "/fader", 0.5));
    CHECK(devices.send(id, "/fader", 0.5)); // the same: not sent again
    CHECK(devices.send(id, "/mute", 1));
    CHECK(devices.send(id, "/scene", 3.4));
    CHECK(!devices.send(id, "/nothing", 1));
    const auto got = received(receiver, 3);
    CHECK(got.size() == 3);
    if (got.size() == 3) {
        CHECK(got[0].address == "/fader" && got[0].types == "f" && std::abs(got[0].args[0].toDouble() - 0.5) < 1e-6);
        CHECK(got[1].address == "/mute" && got[1].types == "T");
        CHECK(got[2].address == "/scene" && got[2].types == "i" && got[2].args[0].toInt() == 3);
    }
    CHECK(devices.sent() == 3);
    double v = 0;
    CHECK(devices.value(id, "/fader", &v) && v == 0.5);

    // Open: no address declared, any path, every value
    Device open;
    open.kind = Kind::Osc;
    open.name = "Mixer"; // taken: "Mixer 2"
    open.host = "127.0.0.1";
    open.port = receiver.localPort();
    const quint64 any = devices.add(open);
    CHECK(devices.nameOf(any) == QStringLiteral("Mixer 2"));
    CHECK(devices.status(any) == QStringLiteral("Any address"));
    CHECK(devices.sendTo(any, "/face/smile", 'f', 0.25) && devices.sendTo(any, "/face/smile", 'f', 0.25));
    CHECK(devices.sendTo(any, "/hands/count", 'i', 2));
    const auto stream = received(receiver, 3);
    CHECK(stream.size() == 3 && stream[2].types == "i");
    CHECK(!devices.sendTo(12345, "/x", 'f', 1));
}

static void dmxDevices()
{
    Devices devices;
    int told = 0;
    QObject::connect(&devices, &Devices::dmxChanged, [&] { ++told; });
    Device spot;
    spot.kind = Kind::Dmx;
    spot.name = "Spot";
    spot.universe = 2;
    spot.start = 101;
    Address dimmer;
    dimmer.path = "/dimmer";
    Address pan;
    pan.path = "/pan";
    pan.channel = 2;
    pan.fine = true;
    spot.addresses = {dimmer, pan};
    const quint64 id = devices.add(spot);
    CHECK(devices.online(id));
    CHECK(devices.status(id) == QStringLiteral("Universe 2, channels 101–103"));
    CHECK(!devices.dmxLevel(id, "/dimmer"));
    CHECK(devices.send(id, "/dimmer", 255) && devices.send(id, "/pan", 32768));
    CHECK(devices.dmxLevel(id, "/dimmer") == 255 && devices.dmxLevel(id, "/pan") == 32768);
    const auto universes = devices.dmxUniverses();
    CHECK(universes.count(2) && universes.at(2).size() == 512);
    if (universes.count(2)) {
        const QByteArray u = universes.at(2);
        CHECK(quint8(u[100]) == 255 && quint8(u[101]) == 128 && quint8(u[102]) == 0 && quint8(u[99]) == 0);
    }
    CHECK(told == 2);
    CHECK(devices.send(id, "/dimmer", 1) && devices.dmxLevel(id, "/dimmer") == 1);


    // Moved to another start address: what it took is let go
    Device moved = *devices.find(id);
    moved.start = 1;
    CHECK(devices.set(moved));
    CHECK(quint8(devices.dmxUniverses().at(2)[100]) == 0);
    devices.remove(id);
    CHECK(!devices.find(id));

    // Renamed, then a new one under the old name: two devices
    Device fixture;
    fixture.kind = Kind::Dmx;
    fixture.name = "Fixture";
    const quint64 first = devices.add(fixture);
    Device renamed = *devices.find(first);
    renamed.name = "Wash";
    CHECK(devices.set(renamed));
    const quint64 second = devices.add(fixture);
    CHECK(second != first && devices.find(first) && devices.find(first)->name == "Wash" && devices.nameOf(second) == QStringLiteral("Fixture"));
}

static void saved()
{
    Devices one;
    Device midi;
    midi.kind = Kind::Midi;
    midi.name = "Synth";
    midi.midiChannel = 3;
    Address note;
    note.path = "/kick";
    note.message = pxs::core::midi::Kind::Note;
    note.number = 36;
    midi.addresses = {note};
    Device osc;
    osc.kind = Kind::Osc;
    osc.name = "Video";
    osc.host = "127.0.0.1";
    osc.port = 9000;
    const quint64 a = one.add(osc), b = one.add(midi);
    const QJsonObject json = one.toJson();

    Devices two;
    CHECK(two.fromJson(json));
    CHECK(two.find(a) && two.find(a)->name == "Video" && two.find(b) && two.find(b)->addresses.size() == 1);
    CHECK(two.find(b) && two.find(b)->addresses[0].number == 36 && two.find(b)->midiChannel == 3);
    if (!two.find(b)) return;
    Device moved = *two.find(b);
    moved.midiPort = "Replacement MIDI controller";
    moved.midiChannel = 5;
    CHECK(two.set(moved));
    Devices repatched;
    CHECK(repatched.fromJson(two.toJson()));
    const auto *reopened = repatched.find(b);
    CHECK(reopened && reopened->midiPort == moved.midiPort && reopened->midiChannel == 5 &&
          reopened->addresses == midi.addresses);
}

static void panel()
{
    Devices devices;
    Device osc;
    osc.kind = Kind::Osc;
    osc.name = "Mixer";
    osc.host = "127.0.0.1";
    osc.port = 9000;
    Address a;
    a.path = "/fader";
    osc.addresses = {a};
    Device dmx;
    dmx.kind = Kind::Dmx;
    dmx.name = "Par";
    dmx.addresses = {a};
    const quint64 o = devices.add(osc), d = devices.add(dmx);
    DevicesPanel panel(&devices, QStringLiteral("What the signals are sent to."));
    auto *list = panel.findChild<QListWidget *>();
    CHECK(list && list->count() == 2);
    auto *table = panel.findChild<QTableWidget *>();

    panel.select(o);
    CHECK(panel.selected() == o && table && table->columnCount() == 4 && table->rowCount() == 1);
    panel.select(d);
    CHECK(table && table->columnCount() == 4); // Address, Channel, 16 bits, Range
    auto *range = table ? qobject_cast<QLabel *>(table->cellWidget(0, 3)) : nullptr;
    auto *fine = table ? qobject_cast<QCheckBox *>(table->cellWidget(0, 2)) : nullptr;
    CHECK(range && range->text() == QStringLiteral("0–255"));
    CHECK(fine);
    if (fine) fine->setChecked(true);
    CHECK(range && range->text() == QStringLiteral("0–65535"));

    // An address renamed in the table is the device's
    if (table && table->item(0, 0)) table->item(0, 0)->setText(QStringLiteral("dimmer"));
    CHECK(devices.find(d) && devices.find(d)->addresses[0].path == "/dimmer");
}

static void repatchFixture()
{
    Devices devices;
    Device fixture;
    fixture.kind = Kind::Dmx;
    fixture.name = "Fixture";
    Address dimmer;
    dimmer.path = "/light/dimmer";
    fixture.addresses = {dimmer};
    fixture.start = 10;
    const auto id = devices.add(fixture);
    CHECK(devices.send(id, "/light/dimmer", 64));

    DevicesPanel panel(&devices);
    panel.select(id);
    auto *patch = panel.findChild<QPushButton *>("patchDevice");
    auto *remove = panel.findChild<QPushButton *>("removeDevice");
    CHECK(patch && remove && !remove->isHidden());
    if (!patch) return;
    QTimer::singleShot(0, &panel, [&] {
        auto *dialog = panel.findChild<QDialog *>();
        CHECK(dialog);
        if (!dialog) return;
        const auto fields = dialog->findChildren<QSpinBox *>();
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        CHECK(fields.size() == 2 && buttons);
        if (fields.size() != 2 || !buttons) { dialog->reject(); return; }
        fields[0]->setValue(3);
        fields[1]->setValue(24);
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    patch->click();
    const auto *patched = devices.find(id);
    CHECK(patched && patched->universe == 3 && patched->start == 24 && patched->addresses == fixture.addresses);
    CHECK(devices.dmxUniverses().at(1).at(9) == 0);
    CHECK(devices.send(id, "/light/dimmer", 128));
    CHECK(static_cast<unsigned char>(devices.dmxUniverses().at(3).at(23)) == 128);
}

static void discoveryLifecycle()
{
    Devices devices;
    DevicesPanel panel(&devices);
    auto *list = panel.findChild<QListWidget *>();
    devices.startDiscovery();
    auto *discovery = devices.findChild<pxs::oscquery::Discovery *>();
    CHECK(list && discovery);
    if (!list || !discovery) return;
    discovery->stop(); // use only the packets below
    const auto announcement = [](quint32 ttl) {
        return pxs::MdnsResponder::buildResponse(QStringLiteral("PXS discovery lifecycle"), QStringLiteral("lifecycle-test.local"),
            {{QStringLiteral("_oscjson._tcp"), 12345}}, {QHostAddress::LocalHost}, ttl);
    };
    discovery->handle(announcement(120));
    CHECK(list->count() == 1);
    const auto visible = devices.all();
    if (visible.empty()) return;
    const auto id = visible.front()->id;
    panel.select(id);
    auto *remove = panel.findChild<QPushButton *>("removeDevice");
    CHECK(remove && remove->isHidden());
    devices.remove(id);
    CHECK(list->count() == 1 && devices.find(id));

    discovery->handle(announcement(0));
    CHECK(list->count() == 0);
    discovery->handle(announcement(120));
    CHECK(list->count() == 1 && devices.all().front()->id == id);
    devices.setReferencedDevices({id});
    discovery->handle(announcement(0));
    CHECK(list->count() == 1 && !devices.online(id) && devices.status(id) == QStringLiteral("Gone from the network"));

    Devices reopened;
    CHECK(reopened.fromJson(devices.toJson()));
    reopened.setReferencedDevices({id});
    CHECK(reopened.all().size() == 1 && reopened.nameOf(id) == QStringLiteral("PXS discovery lifecycle") && !reopened.online(id));
    devices.setReferencedDevices({});
    CHECK(list->count() == 0);

    discovery->handle(announcement(1));
    CHECK(list->count() == 1);
    QEventLoop wait;
    QTimer::singleShot(1100, Qt::PreciseTimer, &wait, &QEventLoop::quit);
    wait.exec();
    discovery->handle({});
    CHECK(list->count() == 0);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    parameterMenu();
    oscDevices();
    dmxDevices();
    saved();
    panel();
    repatchFixture();
    discoveryLifecycle();
    if (failures) std::fprintf(stderr, "devices: %d check(s) failed\n", failures);
    else std::printf("devices: ok\n");
    return failures ? 1 : 0;
}
