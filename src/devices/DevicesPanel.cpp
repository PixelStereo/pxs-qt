#include <pxs/DevicesPanel.h>

#include <pxs/Devices.h>
#include <pxs/Midi.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace pxs::devices {

namespace core = pxs::core::device;

namespace {

enum Page { Empty = 0, OscQueryPage, MadePage };

std::string utf8(const QString &s) { return s.toUtf8().toStdString(); }
QString text(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

// The columns of the table of addresses, by kind: the path first, the range last
QStringList columns(Kind kind)
{
    switch (kind) {
    case Kind::Osc: return {QStringLiteral("Address"), QStringLiteral("Type"), QStringLiteral("Min"), QStringLiteral("Max")};
    case Kind::Dmx: return {QStringLiteral("Address"), QStringLiteral("Channel"), QStringLiteral("16 bits"), QStringLiteral("Input min"), QStringLiteral("Input max")};
    case Kind::Midi: return {QStringLiteral("Address"), QStringLiteral("Message"), QStringLiteral("Number"), QStringLiteral("Input min"), QStringLiteral("Input max")};
    case Kind::OscQuery: break;
    }
    return {};
}

class MidiPortBox : public QComboBox
{
public:
    MidiPortBox()
    {
        setEditable(true);
        setInsertPolicy(QComboBox::NoInsert);
        lineEdit()->setPlaceholderText(QStringLiteral("First available output"));
    }

    void refreshPorts(const QString &selected)
    {
        const QSignalBlocker comboBlock(this);
        const QSignalBlocker editBlock(lineEdit());
        const auto outputs = pxs::midi::outputNames();
        clear();
        addItem(QString());
        addItems(outputs);
        if (!selected.isEmpty() && findText(selected) < 0) addItem(selected);
        setCurrentIndex(findText(selected));
        lineEdit()->setPlaceholderText(outputs.isEmpty() ? QStringLiteral("No MIDI output detected")
                                                         : QStringLiteral("First available output"));
    }

    void showPopup() override
    {
        refreshPorts(currentText());
        QComboBox::showPopup();
    }
};

QComboBox *typeBox(char type)
{
    auto *box = new QComboBox;
    box->addItem(QStringLiteral("number (f)"), QStringLiteral("f"));
    box->addItem(QStringLiteral("whole number (i)"), QStringLiteral("i"));
    box->addItem(QStringLiteral("on / off (T, F)"), QStringLiteral("T"));
    box->addItem(QStringLiteral("double (d)"), QStringLiteral("d"));
    box->setCurrentIndex(std::max(0, box->findData(QString(QChar(type)))));
    return box;
}

QComboBox *messageBox(pxs::core::midi::Kind message)
{
    auto *box = new QComboBox;
    box->addItem(QStringLiteral("Control change"), int(pxs::core::midi::Kind::ControlChange));
    box->addItem(QStringLiteral("Note"), int(pxs::core::midi::Kind::Note));
    box->addItem(QStringLiteral("Pitch bend"), int(pxs::core::midi::Kind::PitchBend));
    box->addItem(QStringLiteral("Program change"), int(pxs::core::midi::Kind::ProgramChange));
    box->setCurrentIndex(std::max(0, box->findData(int(message))));
    return box;
}

QDoubleSpinBox *numberBox(double v)
{
    auto *box = new QDoubleSpinBox;
    box->setRange(-1e6, 1e6);
    box->setDecimals(3);
    box->setValue(v);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return box;
}

QSpinBox *wholeBox(int lo, int hi, int v)
{
    auto *box = new QSpinBox;
    box->setRange(lo, hi);
    box->setValue(v);
    return box;
}

// A new device of a kind, with one address to start from
Device starter(Kind kind)
{
    Device d;
    d.kind = kind;
    Address a;
    switch (kind) {
    case Kind::Osc:
        d.name = "OSC device";
        d.host = "127.0.0.1";
        d.port = 9001;
        a.path = "/value";
        break;
    case Kind::Dmx:
        d.name = "DMX fixture";
        a.path = "/dimmer";
        break;
    case Kind::Midi:
        d.name = "MIDI device";
        a.path = "/cc/1";
        a.number = 1;
        break;
    case Kind::OscQuery: break;
    }
    a.max = core::nativeMaximum(kind, a);
    if (!a.path.empty()) d.addresses.push_back(a);
    return d;
}

} // namespace

DevicesPanel::DevicesPanel(Devices *devices, const QString &hint, QWidget *parent) : QWidget(parent), m_d(devices)
{
    setWindowTitle(QStringLiteral("Devices"));
    resize(860, 500);
    auto *root = new QHBoxLayout(this);
    root->setSpacing(12);

    // Left: the devices, and how to make one
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(QStringLiteral("<b>Devices</b>")));
    auto *caption = new QLabel(hint.isEmpty() ? QStringLiteral("The OSCQuery devices of the network appear by themselves.")
                                              : hint + QStringLiteral(" The OSCQuery devices of the network appear by themselves."));
    caption->setWordWrap(true);
    caption->setObjectName(QStringLiteral("caption"));
    left->addWidget(caption);
    m_list = new QListWidget;
    m_list->setMinimumWidth(260);
    left->addWidget(m_list, 1);
    auto *buttons = new QHBoxLayout;
    auto *add = new QToolButton;
    add->setText(QStringLiteral("+ Device"));
    add->setObjectName(QStringLiteral("accent"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    menu->addAction(QStringLiteral("OSC"), this, [this] { select(m_d->add(starter(Kind::Osc))); m_name->setFocus(); m_name->selectAll(); })
        ->setToolTip(QStringLiteral("A plain OSC receiver: a host, a UDP port, and the addresses it takes"));
    menu->addAction(QStringLiteral("OSCQuery…"), this, [this] {
        bool ok = false;
        const QString t = QInputDialog::getText(this, QStringLiteral("An OSCQuery device"), QStringLiteral("Its address and HTTP port (host:port)"),
                                                QLineEdit::Normal, QStringLiteral("127.0.0.1:5678"), &ok);
        if (!ok) return;
        const QString host = t.section(':', 0, -2).trimmed();
        const int port = t.section(':', -1).toInt();
        if (host.isEmpty() || port <= 0 || port > 65535) return;
        select(m_d->addOscQuery(host, quint16(port)));
    })->setToolTip(QStringLiteral("An OSCQuery device that is not announced: its address and its HTTP port"));
    menu->addAction(QStringLiteral("DMX fixture"), this, [this] { select(m_d->add(starter(Kind::Dmx))); m_name->setFocus(); m_name->selectAll(); })
        ->setToolTip(QStringLiteral("A fixture in a universe: its start address and its channels, on 8 or 16 bits"));
    menu->addAction(QStringLiteral("MIDI"), this, [this] { select(m_d->add(starter(Kind::Midi))); m_name->setFocus(); m_name->selectAll(); })
        ->setToolTip(QStringLiteral("An instrument on a MIDI port and channel: control changes, notes, pitch bend, program changes"));
    menu->setToolTipsVisible(true);
    add->setMenu(menu);
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    buttons->addWidget(add);
    buttons->addStretch(1);
    buttons->addWidget(remove);
    left->addLayout(buttons);
    root->addLayout(left);

    // Right: the selected one
    m_pages = new QStackedWidget;
    auto *empty = new QLabel(QStringLiteral("Select a device, or make one."));
    empty->setAlignment(Qt::AlignCenter);
    empty->setObjectName(QStringLiteral("caption"));
    m_pages->addWidget(empty);

    auto *query = new QWidget;
    auto *qv = new QVBoxLayout(query);
    m_queryTitle = new QLabel;
    m_queryInfo = new QLabel;
    m_queryInfo->setWordWrap(true);
    m_queryInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    qv->addWidget(m_queryTitle);
    qv->addWidget(m_queryInfo);
    qv->addStretch(1);
    m_pages->addWidget(query);

    auto *made = new QWidget;
    auto *mv = new QVBoxLayout(made);
    m_kindTitle = new QLabel;
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("caption"));
    m_status->setWordWrap(true);
    mv->addWidget(m_kindTitle);
    mv->addWidget(m_status);
    m_form = new QFormLayout;
    m_name = new QLineEdit;
    m_host = new QLineEdit;
    m_host->setPlaceholderText(QStringLiteral("127.0.0.1, or a name"));
    m_port = wholeBox(1, 65535, 9001);
    m_universe = wholeBox(0, 63999, 1);
    m_universe->setToolTip(QStringLiteral("The application's universe: its DMX outputs say where it goes on the network"));
    m_start = wholeBox(1, 512, 1);
    m_start->setToolTip(QStringLiteral("The fixture's first channel in its universe"));
    m_midiPort = new MidiPortBox;
    m_midiPort->setToolTip(QStringLiteral("The MIDI output (empty: the first one)"));
    m_channel = wholeBox(1, 16, 1);
    m_form->addRow(QStringLiteral("Name"), m_name);
    m_form->addRow(QStringLiteral("Host"), m_host);
    m_form->addRow(QStringLiteral("UDP port"), m_port);
    m_form->addRow(QStringLiteral("Universe"), m_universe);
    m_form->addRow(QStringLiteral("Start address"), m_start);
    m_form->addRow(QStringLiteral("MIDI port"), m_midiPort);
    m_form->addRow(QStringLiteral("MIDI channel"), m_channel);
    mv->addLayout(m_form);
    mv->addWidget(new QLabel(QStringLiteral("<b>Addresses</b> — what it takes")));
    m_addresses = new QTableWidget(0, 0);
    m_addresses->verticalHeader()->hide();
    m_addresses->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_addresses->horizontalHeader()->setStretchLastSection(false);
    m_addresses->horizontalHeader()->setCascadingSectionResizes(false);
    m_addresses->setSelectionBehavior(QAbstractItemView::SelectRows);
    mv->addWidget(m_addresses, 1);
    auto *rows = new QHBoxLayout;
    auto *addAddress = new QPushButton(QStringLiteral("+ Address"));
    auto *removeAddress = new QPushButton(QStringLiteral("− Address"));
    rows->addWidget(addAddress);
    rows->addWidget(removeAddress);
    rows->addStretch(1);
    mv->addLayout(rows);
    m_pages->addWidget(made);
    root->addWidget(m_pages, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] { showSelected(); });
    connect(m_list, &QListWidget::itemClicked, this, [this] { emit deviceSelected(selected()); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (const quint64 id = selected()) m_d->remove(id);
    });
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        const auto *item = m_list->itemAt(point);
        if (!item) return;
        const auto id = item->data(Qt::UserRole).toULongLong();
        const auto *device = m_d->find(id);
        if (!device || device->kind != Kind::OscQuery) return;
        QMenu context(this);
        context.addAction(QStringLiteral("Read namespace again"), this, [this, id] { m_d->refresh(id); });
        context.exec(m_list->viewport()->mapToGlobal(point));
    });
    for (QLineEdit *e : {m_name, m_host}) connect(e, &QLineEdit::editingFinished, this, [this] { store(); });
    for (QSpinBox *b : {m_port, m_universe, m_start, m_channel}) connect(b, &QSpinBox::editingFinished, this, [this] { store(); });
    connect(m_midiPort, &QComboBox::activated, this, [this] { store(); });
    connect(m_midiPort->lineEdit(), &QLineEdit::editingFinished, this, [this] { store(); });
    connect(addAddress, &QPushButton::clicked, this, [this] {
        const Device *d = m_d->find(selected());
        if (!d || d->kind == Kind::OscQuery) return;
        Device edited = *d;
        Address a;
        a.max = core::nativeMaximum(d->kind, a);
        const int n = int(edited.addresses.size()) + 1;
        a.path = utf8(QStringLiteral("/value%1").arg(n));
        if (d->kind == Kind::Dmx) a.channel = std::min(512, std::max(1, d->footprint() + 1));
        if (d->kind == Kind::Midi) a.number = std::min(127, n);
        edited.addresses.push_back(a);
        m_d->set(edited);
        m_tableFor = 0; // drawn again
        showSelected();
        m_addresses->editItem(m_addresses->item(m_addresses->rowCount() - 1, 0));
    });
    connect(removeAddress, &QPushButton::clicked, this, [this] {
        const int row = m_addresses->currentRow();
        if (row < 0) return;
        m_addresses->removeRow(row);
        store();
    });
    connect(m_addresses, &QTableWidget::itemChanged, this, [this] {
        if (!m_filling) store();
    });
    connect(m_d, &Devices::changed, this, [this] { refresh(); });
    refresh();
}

QWidget *DevicesPanel::inspector() const { return m_pages; }

void DevicesPanel::setCompactList()
{
    m_list->setMinimumWidth(0);
}

quint64 DevicesPanel::selected() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toULongLong() : 0;
}

void DevicesPanel::select(quint64 id)
{
    refresh();
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toULongLong() == id) m_list->setCurrentRow(i);
}

void DevicesPanel::refresh()
{
    const quint64 keep = selected();
    {
        const QSignalBlocker block(m_list);
        m_list->clear();
        for (const Device *d : m_d->all()) {
            const bool on = m_d->online(d->id);
            QString where;
            switch (d->kind) {
            case Kind::OscQuery:
            case Kind::Osc: where = QStringLiteral("%1:%2").arg(text(d->host)).arg(d->port); break;
            case Kind::Dmx: where = QStringLiteral("U%1 @%2").arg(d->universe).arg(d->start); break;
            case Kind::Midi: where = QStringLiteral("%1 ch %2").arg(d->midiPort.empty() ? QStringLiteral("first port") : text(d->midiPort)).arg(d->midiChannel); break;
            }
            auto *item = new QListWidgetItem(QStringLiteral("%1\n%2 %3   %4").arg(text(d->name), on ? QStringLiteral("●") : QStringLiteral("○"),
                                                                                  QString::fromLatin1(core::name(d->kind)), where));
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(d->id));
            item->setToolTip(m_d->status(d->id));
            if (!on) item->setForeground(QColor(0x8a, 0x8a, 0x90));
            m_list->addItem(item);
            if (d->id == keep) m_list->setCurrentItem(item);
        }
    }
    showSelected();
}

void DevicesPanel::showSelected()
{
    const Device *d = m_d->find(selected());
    if (!d) {
        m_pages->setCurrentIndex(Empty);
        return;
    }
    m_filling = true;
    if (d->kind == Kind::OscQuery) {
        m_pages->setCurrentIndex(OscQueryPage);
        m_queryTitle->setText(QStringLiteral("<b>%1</b>").arg(text(d->name).toHtmlEscaped()));
        m_queryInfo->setText(QStringLiteral("OSCQuery at http://%1:%2/ — %3\n%4 number(s) can be driven.%5")
                                 .arg(text(d->host))
                                 .arg(d->port)
                                 .arg(m_d->online(d->id) ? QStringLiteral("its namespace is read") : m_d->status(d->id))
                                 .arg(m_d->params(d->id).size())
                                 .arg(d->manual ? QStringLiteral("\nAdded by hand: kept with the devices.") : QStringLiteral("\nFound on the network: found again each time.")));
        m_filling = false;
        return;
    }
    m_pages->setCurrentIndex(MadePage);
    m_kindTitle->setText(QStringLiteral("<b>%1</b>").arg(QString::fromLatin1(core::name(d->kind))));
    m_status->setText(m_d->status(d->id));
    const bool net = d->kind == Kind::Osc, dmx = d->kind == Kind::Dmx, midi = d->kind == Kind::Midi;
    m_form->setRowVisible(m_host, net);
    m_form->setRowVisible(m_port, net);
    m_form->setRowVisible(m_universe, dmx);
    m_form->setRowVisible(m_start, dmx);
    m_form->setRowVisible(m_midiPort, midi);
    m_form->setRowVisible(m_channel, midi);
    if (!m_name->hasFocus()) m_name->setText(text(d->name));
    if (!m_host->hasFocus()) m_host->setText(text(d->host));
    if (!m_port->hasFocus()) m_port->setValue(d->port);
    if (!m_universe->hasFocus()) m_universe->setValue(d->universe);
    if (!m_start->hasFocus()) m_start->setValue(d->start);
    if (!m_channel->hasFocus()) m_channel->setValue(d->midiChannel);
    if (midi && !m_midiPort->hasFocus()) {
        static_cast<MidiPortBox *>(m_midiPort)->refreshPorts(text(d->midiPort));
    }
    if (m_tableFor != d->id || m_tableKind != int(d->kind) || m_addresses->rowCount() != int(d->addresses.size())) fillTable();
    m_filling = false;
}

void DevicesPanel::fillTable()
{
    const Device *d = m_d->find(selected());
    if (!d) return;
    const bool was = m_filling;
    m_filling = true;
    m_tableFor = d->id;
    m_tableKind = int(d->kind);
    const QStringList heads = columns(d->kind);
    m_addresses->setRowCount(0);
    m_addresses->setColumnCount(int(heads.size()));
    m_addresses->setHorizontalHeaderLabels(heads);
    auto stored = [this](QWidget *w) {
        if (auto *c = qobject_cast<QComboBox *>(w)) connect(c, &QComboBox::currentIndexChanged, this, [this] { if (!m_filling) store(); });
        if (auto *n = qobject_cast<QDoubleSpinBox *>(w)) connect(n, &QDoubleSpinBox::editingFinished, this, [this] { store(); });
        if (auto *n = qobject_cast<QSpinBox *>(w)) connect(n, &QSpinBox::editingFinished, this, [this] { store(); });
        if (auto *b = qobject_cast<QCheckBox *>(w)) connect(b, &QCheckBox::toggled, this, [this] { if (!m_filling) store(); });
        return w;
    };
    for (const Address &a : d->addresses) {
        const int row = m_addresses->rowCount();
        m_addresses->insertRow(row);
        m_addresses->setItem(row, 0, new QTableWidgetItem(text(a.path)));
        int c = 1;
        if (d->kind == Kind::Osc) m_addresses->setCellWidget(row, c++, stored(typeBox(a.type)));
        if (d->kind == Kind::Dmx) {
            m_addresses->setCellWidget(row, c++, stored(wholeBox(1, 512, a.channel)));
            auto *fine = new QCheckBox;
            fine->setChecked(a.fine);
            fine->setToolTip(QStringLiteral("Two channels: this one (coarse) and the next (fine)"));
            m_addresses->setCellWidget(row, c++, stored(fine));
        }
        if (d->kind == Kind::Midi) {
            m_addresses->setCellWidget(row, c++, stored(messageBox(a.message)));
            m_addresses->setCellWidget(row, c++, stored(wholeBox(0, 127, a.number)));
        }
        auto *minimum = numberBox(a.min);
        auto *maximum = numberBox(a.max);
        m_addresses->setCellWidget(row, c++, stored(minimum));
        m_addresses->setCellWidget(row, c, stored(maximum));
        if (d->kind == Kind::Dmx || d->kind == Kind::Midi) {
            auto adjust = [minimum, maximum, kind = d->kind, previous = a](Address next) mutable {
                if (minimum->value() == 0 && maximum->value() == core::nativeMaximum(kind, previous))
                    maximum->setValue(core::nativeMaximum(kind, next));
                previous = next;
            };
            // Run this before storing the new message type or bit depth.
            QWidget *encoding = m_addresses->cellWidget(row, d->kind == Kind::Dmx ? 2 : 1);
            disconnect(encoding, nullptr, this, nullptr);
            if (auto *fine = qobject_cast<QCheckBox *>(encoding)) {
                connect(fine, &QCheckBox::toggled, this, [this, adjust, a](bool on) mutable {
                    auto next = a;
                    next.fine = on;
                    adjust(next);
                    store();
                });
            } else if (auto *message = qobject_cast<QComboBox *>(encoding)) {
                connect(message, &QComboBox::currentIndexChanged, this, [this, message, adjust, a]() mutable {
                    auto next = a;
                    next.message = pxs::core::midi::Kind(message->currentData().toInt());
                    adjust(next);
                    store();
                });
            }
        }
    }
    m_filling = was;
}

void DevicesPanel::store()
{
    const Device *d = m_d->find(selected());
    if (!d || m_filling || d->kind == Kind::OscQuery) return;
    Device edited = *d;
    edited.name = utf8(m_name->text().trimmed().isEmpty() ? text(d->name) : m_name->text().trimmed());
    if (d->kind == Kind::Osc) {
        edited.host = utf8(m_host->text().trimmed());
        edited.port = quint16(m_port->value());
    }
    if (d->kind == Kind::Dmx) {
        edited.universe = m_universe->value();
        edited.start = m_start->value();
    }
    if (d->kind == Kind::Midi) {
        edited.midiPort = utf8(m_midiPort->currentText().trimmed());
        edited.midiChannel = m_channel->value();
    }
    edited.addresses.clear();
    for (int row = 0; row < m_addresses->rowCount(); ++row) {
        const QTableWidgetItem *path = m_addresses->item(row, 0);
        QString p = path ? path->text().trimmed() : QString();
        if (p.isEmpty()) continue;
        if (!p.startsWith('/')) p.prepend('/');
        Address a;
        a.path = utf8(p);
        int c = 1;
        if (d->kind == Kind::Osc)
            if (auto *type = qobject_cast<QComboBox *>(m_addresses->cellWidget(row, c++))) a.type = type->currentData().toString().at(0).toLatin1();
        if (d->kind == Kind::Dmx) {
            if (auto *ch = qobject_cast<QSpinBox *>(m_addresses->cellWidget(row, c++))) a.channel = ch->value();
            if (auto *fine = qobject_cast<QCheckBox *>(m_addresses->cellWidget(row, c++))) a.fine = fine->isChecked();
        }
        if (d->kind == Kind::Midi) {
            if (auto *m = qobject_cast<QComboBox *>(m_addresses->cellWidget(row, c++))) a.message = pxs::core::midi::Kind(m->currentData().toInt());
            if (auto *n = qobject_cast<QSpinBox *>(m_addresses->cellWidget(row, c++))) a.number = n->value();
        }
        if (auto *lo = qobject_cast<QDoubleSpinBox *>(m_addresses->cellWidget(row, c++))) a.min = lo->value();
        if (auto *hi = qobject_cast<QDoubleSpinBox *>(m_addresses->cellWidget(row, c))) a.max = hi->value();
        edited.addresses.push_back(a);
    }
    if (edited == *d) return;
    m_d->set(edited);
}

} // namespace pxs::devices
