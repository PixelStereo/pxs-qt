#include "pxs/OutputsPanel.h"

#include <QApplication>
#include <QtGlobal>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <cstdio>

using namespace pxs::dmx;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    OutputsPanel panel;
    panel.setDefaultSourceName("Test console");
    panel.resize(500, 600);
    panel.show();
    auto *table = panel.findChild<QTableWidget *>("dmxOutputsTable");
    auto *add = panel.findChild<QPushButton *>("addDmxOutput");
    auto *remove = panel.findChild<QPushButton *>("removeDmxOutput");
    auto *off = panel.findChild<QPushButton *>("stopDmxOutputs");
    auto *protocol = panel.findChild<QComboBox *>("dmxOutputProtocol");
    auto *mode = panel.findChild<QComboBox *>("dmxOutputMode");
    auto *addresses = panel.findChild<QLineEdit *>("dmxOutputAddresses");
    auto *port = panel.findChild<QSpinBox *>("dmxOutputPort");
    auto *rate = panel.findChild<QSpinBox *>("dmxOutputRate");
    auto *priority = panel.findChild<QSpinBox *>("dmxOutputPriority");
    auto *sourceName = panel.findChild<QLineEdit *>("dmxOutputSourceName");
    auto *transmission = panel.findChild<QComboBox *>("dmxOutputTransmission");
    auto *patchFirst = panel.findChild<QSpinBox *>("dmxOutputPatchFirst");
    auto *wireFirst = panel.findChild<QSpinBox *>("dmxOutputUniverse");
    auto *problem = panel.findChild<QLabel *>("dmxOutputProblem");
    CHECK(table && add && remove && off && protocol && mode && addresses && port && rate && priority && sourceName && transmission && problem);
    QSignalSpy changed(&panel, &OutputsPanel::outputsChanged);

    // Nothing to edit yet
    CHECK(table->rowCount() == 0 && !panel.findChild<QWidget *>("dmxOutputProtocol")->isVisibleTo(&panel));

    // A new output: off, Art-Net, its own id and the application's source name, told to the application
    add->click();
    CHECK(changed.size() == 1 && table->rowCount() == 1);
    QList<Output> list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list.size() == 1 && !list[0].enabled && list[0].protocol == Protocol::ArtNet && list[0].id == 1 &&
          list[0].sourceName == "Test console" && list[0].firstUniverse == 0);
    CHECK(changed.last().at(1).toString() == "New DMX Output");
    CHECK(protocol->isVisibleTo(&panel) && !addresses->isVisibleTo(&panel) && !priority->isVisibleTo(&panel) && !sourceName->isVisibleTo(&panel));
    CHECK(!patchFirst->isVisibleTo(&panel)); // the map of universes only when the application routes

    // Unicast shows its addresses, and says it needs one
    mode->setCurrentIndex(1);
    emit mode->activated(1);
    CHECK(addresses->isVisibleTo(&panel) && problem->isVisibleTo(&panel) && problem->text().contains("Unicast"));
    addresses->setText("2.0.0.10, 2.0.0.11 ; 2.0.0.12");
    emit addresses->editingFinished();
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].delivery == Delivery::Unicast && list[0].destinations == QStringList({"2.0.0.10", "2.0.0.11", "2.0.0.12"}));
    CHECK(!problem->isVisibleTo(&panel));
    addresses->setText("not an address");
    emit addresses->editingFinished();
    CHECK(problem->isVisibleTo(&panel) && problem->text().contains("IPv4"));

    // sACN: its own settings, the first universe from 1, the rate limit of the protocol
    protocol->setCurrentIndex(1);
    emit protocol->activated(1);
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].protocol == Protocol::Sacn && list[0].firstUniverse == 1);
    CHECK(priority->isVisibleTo(&panel) && sourceName->isVisibleTo(&panel) && rate->maximum() == 120);
    priority->setValue(150);
    rate->setValue(100);
    port->setValue(5570);
    transmission->setCurrentIndex(1);
    emit transmission->activated(1);
    sourceName->setText("Theatre A");
    emit sourceName->editingFinished();
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].priority == 150 && list[0].rate == 100 && list[0].port == 5570 && list[0].transmission == Transmission::Partial &&
          list[0].sourceName == "Theatre A");
    // Back to Art-Net: the rate is held to its limit
    protocol->setCurrentIndex(0);
    emit protocol->activated(0);
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].protocol == Protocol::ArtNet && list[0].rate == 44 && rate->maximum() == 44 && list[0].firstUniverse == 1);

    // On, and a name, from the table; a run of edits of one setting carries its field (an undo step merges them)
    table->item(0, 0)->setCheckState(Qt::Checked);
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].enabled);
    table->item(0, 1)->setText("Stage nodes");
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].name == "Stage nodes");
    rate->setValue(30);
    CHECK(changed.last().at(2).toString() == "rate");

    // What the output is doing, as the application's sender says it
    quint64 packets = 0;
    panel.setStateSource([&](quint64) {
        OutputState s;
        s.level = OutputState::Ok;
        s.packets = packets;
        return s;
    });
    packets = 42;
    panel.setOutputs(list); // the application hands them back
    CHECK(table->item(0, 3)->text() == "Sending · 42 packets");
    panel.setStateSource([](quint64) {
        OutputState s;
        s.level = OutputState::Error;
        s.text = "Cannot send from 10.0.0.9";
        return s;
    });
    panel.setOutputs(list);
    CHECK(table->item(0, 3)->text().startsWith("Error: Cannot send"));

    // The map of universes, for an application that routes
    panel.setMappingShown(true);
    CHECK(patchFirst->isVisibleTo(&panel) && wireFirst->isVisibleTo(&panel));
    patchFirst->setValue(3);
    wireFirst->setValue(7);
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].firstSourceUniverse == 3 && list[0].firstUniverse == 7);
    const auto outputId = list[0].id;
    protocol->setCurrentIndex(1);
    emit protocol->activated(1);
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list[0].id == outputId && list[0].protocol == Protocol::Sacn &&
          list[0].firstSourceUniverse == 3 && list[0].firstUniverse == 7);

    // Several outputs; all off; delete
    add->click();
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list.size() == 2 && list[1].id == 2 && !list[1].enabled);
    off->click();
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(!list[0].enabled && changed.last().at(1).toString() == "DMX Outputs Off");
    remove->click();
    list = qvariant_cast<QList<Output>>(changed.last().at(0));
    CHECK(list.size() == 1 && list[0].id == 1 && changed.last().at(1).toString() == "Delete DMX Output");

    if (!qEnvironmentVariable("PXS_SHOT").isEmpty()) { // a picture of it, to look at
        Output a = Output::defaults(Protocol::ArtNet), b = Output::defaults(Protocol::Sacn);
        a.id = 1; a.name = "Stage nodes"; a.delivery = Delivery::Unicast; a.destinations = {"2.0.0.10", "2.0.0.11"}; a.interfaceAddress = "127.0.0.1";
        b.id = 2; b.name = "sACN backup"; b.sourceName = "Theatre A"; b.priority = 150;
        panel.setOutputs({a, b});
        panel.setStateSource([](quint64 id) { OutputState s; s.level = id == 1 ? OutputState::Ok : OutputState::Error; s.packets = 1234; s.text = "Cannot send from 10.0.0.9"; return s; });
        panel.setOutputs({a, b});
        panel.grab().save(qEnvironmentVariable("PXS_SHOT"));
    }

    // setOutputs does not tell the application back
    const int told = int(changed.size());
    panel.setOutputs({});
    CHECK(changed.size() == told && table->rowCount() == 0);

    if (failures) return 1;
    std::printf("dmxoutview passed\n");
    return 0;
}
