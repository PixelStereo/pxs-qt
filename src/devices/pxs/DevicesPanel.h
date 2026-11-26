#pragma once
// The devices of pxs::devices::Devices on screen, the same in every application: the list (the OSCQuery devices found on the network or
// added by their address, and the OSC, DMX and MIDI devices made here) and the selected one. An OSCQuery device shows what was read of
// it; a device made here is edited in place: where it is (a host and a port, a universe and a start address, a MIDI port and channel)
// and the addresses it takes (OSC: a path, a type and a range; DMX: a channel on 8 or 16 bits and a range; MIDI: a message, its number
// and a range). Show it as a window of its own or put it in a page.
#include <QWidget>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace pxs::devices {

class Devices;

class DevicesPanel : public QWidget
{
    Q_OBJECT
public:
    // `hint`: what the devices are for in this application ("What the tracks of the timelines can drive.")
    explicit DevicesPanel(Devices *devices, const QString &hint = {}, QWidget *parent = nullptr);

    quint64 selected() const;
    QWidget *inspector() const;
    void setCompactList();
    void select(quint64 id);

signals:
    void deviceSelected(quint64 id);

private:
    void refresh();      // the list, from the devices
    void showSelected(); // the selected one on the right
    void fillTable();    // its addresses, as its kind shows them
    void store();        // the device as edited

    Devices *m_d;
    QListWidget *m_list;
    QStackedWidget *m_pages;
    // An OSCQuery device
    QLabel *m_queryTitle, *m_queryInfo;
    // A device made here
    QLabel *m_kindTitle, *m_status;
    QFormLayout *m_form;
    QLineEdit *m_name, *m_host;
    QSpinBox *m_port, *m_universe, *m_start, *m_channel;
    QComboBox *m_midiPort;
    QTableWidget *m_addresses;
    bool m_filling = false;
    quint64 m_tableFor = 0; // the device the table shows
    int m_tableKind = -1;
};

} // namespace pxs::devices
