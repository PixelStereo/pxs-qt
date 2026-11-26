#include "pxs/OutputsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace pxs::dmx {

namespace {
enum Column { On, Name, ProtocolColumn, StateColumn };

QString protocolName(Protocol p) { return p == Protocol::ArtNet ? QStringLiteral("Art-Net") : QStringLiteral("sACN"); }

QStringList parseAddresses(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
}
} // namespace

OutputsPanel::OutputsPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dmxOutputsPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    auto *tools = new QHBoxLayout;
    m_add = new QPushButton(QStringLiteral("+ Output"));
    m_add->setObjectName(QStringLiteral("addDmxOutput"));
    m_remove = new QPushButton(QStringLiteral("− Output"));
    m_remove->setObjectName(QStringLiteral("removeDmxOutput"));
    m_off = new QPushButton(QStringLiteral("All off"));
    m_off->setObjectName(QStringLiteral("stopDmxOutputs"));
    m_off->setToolTip(QStringLiteral("Switch every output off: the sACN streams are ended"));
    tools->addWidget(m_add);
    tools->addWidget(m_remove);
    tools->addStretch();
    tools->addWidget(m_off);
    column->addLayout(tools);

    m_table = new QTableWidget(0, 4);
    m_table->setObjectName(QStringLiteral("dmxOutputsTable"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("On"), QStringLiteral("Output"), QStringLiteral("Protocol"), QStringLiteral("State")});
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(28);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setCascadingSectionResizes(false);
    m_table->setColumnWidth(On, 40);
    m_table->setColumnWidth(Name, 135);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setMinimumHeight(96);
    m_table->setMaximumHeight(150);
    column->addWidget(m_table);

    m_editor = new QWidget;
    m_form = new QFormLayout(m_editor);
    m_form->setContentsMargins(0, 4, 0, 0);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto spin = [this](int low, int high, const char *name, const QString &tip) {
        auto *s = new QSpinBox;
        s->setRange(low, high);
        s->setKeyboardTracking(false);
        s->setObjectName(QString::fromLatin1(name));
        s->setToolTip(tip);
        return s;
    };
    auto combo = [](const char *name, const QString &tip) {
        auto *c = new QComboBox;
        c->setObjectName(QString::fromLatin1(name));
        c->setToolTip(tip);
        return c;
    };
    m_protocol = combo("dmxOutputProtocol", QStringLiteral("Art-Net (UDP 6454) or sACN, E1.31 (UDP 5568)"));
    m_protocol->addItems({QStringLiteral("Art-Net"), QStringLiteral("sACN")});
    m_mode = combo("dmxOutputMode", QString());
    m_interface = combo("dmxOutputInterface", QStringLiteral("The network card to send from: it picks the network of a broadcast and the group of a multicast"));
    m_addresses = new QLineEdit;
    m_addresses->setObjectName(QStringLiteral("dmxOutputAddresses"));
    m_addresses->setPlaceholderText(QStringLiteral("2.0.0.10, 2.0.0.11"));
    m_addresses->setToolTip(QStringLiteral("One or more IPv4 addresses, separated by commas"));
    m_port = spin(0, 65535, "dmxOutputPort", QStringLiteral("The UDP port to send to: the protocol's (Art-Net 6454, sACN 5568) unless a receiver listens elsewhere"));
    m_port->setSpecialValueText(QStringLiteral("Protocol default"));
    m_rate = spin(1, 44, "dmxOutputRate", QStringLiteral("Frames per second and universe, whatever the application's own frame rate. Art-Net stops at 44, sACN at 120"));
    m_rate->setSuffix(QStringLiteral(" Hz"));
    m_priority = spin(0, 200, "dmxOutputPriority", QStringLiteral("sACN priority: a receiver takes the highest of the sources of a universe (100 by default)"));
    m_sourceNameEdit = new QLineEdit;
    m_sourceNameEdit->setObjectName(QStringLiteral("dmxOutputSourceName"));
    m_sourceNameEdit->setToolTip(QStringLiteral("The name receivers show for this sACN source"));
    m_transmission = combo("dmxOutputTransmission", QStringLiteral("Partial sends each universe only up to its last used channel: less traffic, for the receivers that take a short frame"));
    m_transmission->addItems({QStringLiteral("Full (512 channels)"), QStringLiteral("Partial (used channels)")});
    m_patchFirst = spin(0, 65535, "dmxOutputPatchFirst", QStringLiteral("The first universe of the application that this output sends"));
    m_wireFirst = spin(0, 63999, "dmxOutputUniverse", QStringLiteral("The network universe it goes out as (Art-Net from 0, sACN from 1)"));
    m_count = spin(0, 65535, "dmxOutputCount", QStringLiteral("How many universes it sends: all from the first when 0"));
    m_count->setSpecialValueText(QStringLiteral("All"));
    m_form->addRow(QStringLiteral("Protocol"), m_protocol);
    m_form->addRow(QStringLiteral("Mode"), m_mode);
    m_form->addRow(QStringLiteral("Network card"), m_interface);
    m_form->addRow(QStringLiteral("Addresses"), m_addresses);
    m_form->addRow(QStringLiteral("Port"), m_port);
    m_form->addRow(QStringLiteral("Rate"), m_rate);
    m_form->addRow(QStringLiteral("Priority"), m_priority);
    m_form->addRow(QStringLiteral("Source name"), m_sourceNameEdit);
    m_form->addRow(QStringLiteral("Frames"), m_transmission);
    m_form->addRow(QStringLiteral("From universe"), m_patchFirst);
    m_form->addRow(QStringLiteral("To network universe"), m_wireFirst);
    m_form->addRow(QStringLiteral("Universe count"), m_count);
    m_problem = new QLabel;
    m_problem->setObjectName(QStringLiteral("dmxOutputProblem"));
    m_problem->setWordWrap(true);
    m_problem->setStyleSheet(QStringLiteral("color:#e0b25c;"));
    m_form->addRow(m_problem);
    column->addWidget(m_editor);
    column->addStretch(1);
    m_editor->hide();

    connect(m_add, &QPushButton::clicked, this, [this] {
        Output o = Output::defaults(Protocol::ArtNet);
        o.enabled = false; // a new output is off until it is set up and switched on
        o.sourceName = m_sourceName;
        quint64 next = 1;
        for (const Output &x : m_outputs) next = std::max(next, x.id + 1);
        o.id = next;
        o.name = QStringLiteral("Art-Net %1").arg(m_outputs.size() + 1);
        m_outputs.append(o);
        refreshTable();
        m_table->selectRow(int(m_outputs.size()) - 1);
        emit outputsChanged(m_outputs, QStringLiteral("New DMX Output"), {});
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const int row = selectedRow();
        if (row < 0) return;
        m_outputs.removeAt(row);
        refreshTable();
        if (!m_outputs.isEmpty()) m_table->selectRow(std::min(row, int(m_outputs.size()) - 1));
        refreshEditor();
        emit outputsChanged(m_outputs, QStringLiteral("Delete DMX Output"), {});
    });
    connect(m_off, &QPushButton::clicked, this, [this] {
        bool any = false;
        for (Output &o : m_outputs) {
            any |= o.enabled;
            o.enabled = false;
        }
        refreshTable();
        if (any) emit outputsChanged(m_outputs, QStringLiteral("DMX Outputs Off"), {});
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &OutputsPanel::refreshEditor);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_updating || item->row() < 0 || item->row() >= m_outputs.size()) return;
        const int row = item->row();
        if (item->column() == On) {
            const bool on = item->checkState() == Qt::Checked;
            edit(QStringLiteral("Edit DMX Output"), {}, [on](Output &o) { o.enabled = on; });
        } else if (item->column() == Name) {
            const QString name = item->text().trimmed();
            if (name.isEmpty()) {
                refreshTable();
                return;
            }
            m_table->selectRow(row);
            edit(QStringLiteral("Rename DMX Output"), {}, [name](Output &o) { o.name = name; });
        }
    });
    connect(m_protocol, &QComboBox::activated, this, [this](int i) {
        edit(QStringLiteral("Output Protocol"), {}, [i](Output &o) {
            const Protocol p = i == 1 ? Protocol::Sacn : Protocol::ArtNet;
            if (p == o.protocol) return;
            o.protocol = p;
            o.firstUniverse = p == Protocol::ArtNet ? 0 : 1; // Art-Net counts from 0, sACN from 1
            o.rate = std::min(o.rate, o.maxRate());
        });
    });
    connect(m_mode, &QComboBox::activated, this, [this](int i) {
        edit(QStringLiteral("Output Mode"), {}, [i](Output &o) { o.delivery = i == 1 ? Delivery::Unicast : Delivery::Group; });
    });
    connect(m_interface, &QComboBox::activated, this, [this] {
        const QString ip = m_interface->currentData().toString();
        edit(QStringLiteral("Output Network Card"), {}, [ip](Output &o) { o.interfaceAddress = ip; });
    });
    connect(m_addresses, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        const QStringList ips = parseAddresses(m_addresses->text());
        edit(QStringLiteral("Output Addresses"), {}, [ips](Output &o) { o.destinations = ips; });
    });
    connect(m_port, &QSpinBox::valueChanged, this, [this](int v) {
        edit(QStringLiteral("Output Port"), QStringLiteral("port"), [v](Output &o) { o.port = v; });
    });
    connect(m_rate, &QSpinBox::valueChanged, this, [this](int v) {
        edit(QStringLiteral("Output Rate"), QStringLiteral("rate"), [v](Output &o) { o.rate = v; });
    });
    connect(m_priority, &QSpinBox::valueChanged, this, [this](int v) {
        edit(QStringLiteral("Output Priority"), QStringLiteral("priority"), [v](Output &o) { o.priority = v; });
    });
    connect(m_sourceNameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        const QString name = m_sourceNameEdit->text().trimmed();
        edit(QStringLiteral("Output Source Name"), {}, [this, name](Output &o) { o.sourceName = name.isEmpty() ? m_sourceName : name; });
    });
    connect(m_transmission, &QComboBox::activated, this, [this](int i) {
        edit(QStringLiteral("Output Frames"), {}, [i](Output &o) { o.transmission = i == 1 ? Transmission::Partial : Transmission::Full; });
    });
    connect(m_patchFirst, &QSpinBox::valueChanged, this, [this](int v) {
        edit(QStringLiteral("Output Universes"), QStringLiteral("from"), [v](Output &o) { o.firstSourceUniverse = v; });
    });
    connect(m_wireFirst, &QSpinBox::valueChanged, this, [this](int v) {
        edit(QStringLiteral("Output Universes"), QStringLiteral("to"), [v](Output &o) { o.firstUniverse = v; });
    });
    connect(m_count, &QSpinBox::valueChanged, this, [this](int v) {
        edit(QStringLiteral("Output Universes"), QStringLiteral("count"), [v](Output &o) { o.universeCount = v; });
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (isVisible()) refreshStates();
    });
    m_timer->start();
    setMappingShown(false);
}

void OutputsPanel::setMappingShown(bool shown)
{
    m_mapping = shown;
    m_form->setRowVisible(m_patchFirst, shown);
    m_form->setRowVisible(m_wireFirst, shown);
    m_form->setRowVisible(m_count, shown);
}

int OutputsPanel::selectedRow() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void OutputsPanel::setOutputs(const QList<Output> &outputs)
{
    const int row = selectedRow();
    m_outputs = outputs;
    refreshTable();
    if (!m_outputs.isEmpty()) m_table->selectRow(std::clamp(row, 0, int(m_outputs.size()) - 1));
    refreshEditor();
}

void OutputsPanel::edit(const QString &what, const QString &field, const std::function<void(Output &)> &change)
{
    if (m_updating) return;
    const int row = selectedRow();
    if (row < 0 || row >= m_outputs.size()) return;
    Output changed = m_outputs[row];
    change(changed);
    if (changed == m_outputs[row]) return;
    m_outputs[row] = changed;
    refreshTable();
    refreshEditor();
    emit outputsChanged(m_outputs, what, field);
}

void OutputsPanel::refreshTable()
{
    const int keep = selectedRow();
    m_updating = true;
    QSignalBlocker block(m_table);
    m_table->setRowCount(int(m_outputs.size()));
    for (int i = 0; i < m_outputs.size(); ++i) {
        const Output &o = m_outputs[i];
        auto *on = new QTableWidgetItem;
        on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        on->setCheckState(o.enabled ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, On, on);
        auto *name = new QTableWidgetItem(o.name.isEmpty() ? QStringLiteral("Output %1").arg(o.id) : o.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        m_table->setItem(i, Name, name);
        auto *protocol = new QTableWidgetItem(protocolName(o.protocol));
        protocol->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, ProtocolColumn, protocol);
        auto *state = new QTableWidgetItem;
        state->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(i, StateColumn, state);
    }
    if (keep >= 0 && keep < m_outputs.size()) m_table->selectRow(keep);
    m_updating = false;
    refreshStates();
}

void OutputsPanel::refreshStates()
{
    QSignalBlocker block(m_table);
    for (int i = 0; i < m_table->rowCount() && i < m_outputs.size(); ++i) {
        const Output &o = m_outputs[i];
        QString text;
        bool error = false;
        if (!o.enabled) {
            text = QStringLiteral("Off");
        } else if (!m_state) {
            text = QStringLiteral("On");
        } else {
            const OutputState s = m_state(o.id);
            error = s.level == OutputState::Error;
            text = error ? QStringLiteral("Error: %1").arg(s.text)
                         : s.level == OutputState::Off ? QStringLiteral("Starting")
                         : s.packets ? QStringLiteral("Sending · %1 packets").arg(s.packets) : QStringLiteral("Waiting for a frame");
        }
        if (auto *item = m_table->item(i, StateColumn)) {
            item->setText(text);
            item->setToolTip(text);
            item->setForeground(error ? QBrush(QColor(0xe0, 0xb2, 0x5c)) : QBrush());
        }
    }
}

void OutputsPanel::refreshEditor()
{
    const int row = selectedRow();
    m_editor->setVisible(row >= 0 && row < m_outputs.size());
    m_remove->setEnabled(row >= 0);
    if (row < 0 || row >= m_outputs.size()) return;
    const Output &o = m_outputs[row];
    const bool art = o.protocol == Protocol::ArtNet, unicast = o.delivery == Delivery::Unicast;
    m_updating = true;
    QSignalBlocker b1(m_protocol), b2(m_mode), b3(m_interface), b4(m_port), b5(m_rate), b6(m_priority), b7(m_transmission),
        b8(m_patchFirst), b9(m_wireFirst), b10(m_count);
    m_protocol->setCurrentIndex(art ? 0 : 1);
    m_mode->clear();
    m_mode->addItems({art ? QStringLiteral("Broadcast") : QStringLiteral("Multicast"), QStringLiteral("Unicast")});
    m_mode->setCurrentIndex(unicast ? 1 : 0);
    m_mode->setToolTip(art ? QStringLiteral("Broadcast: to every node of the card's network; unicast: to the addresses given")
                           : QStringLiteral("Multicast: each universe to its group (239.255.x.y); unicast: to the addresses given"));
    // This machine's network cards (up, with an IPv4 address), and the one chosen if it is not here
    m_interface->clear();
    m_interface->addItem(QStringLiteral("Any (the system's choice)"), QString());
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp)) continue;
        for (const QNetworkAddressEntry &e : i.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                m_interface->addItem(QStringLiteral("%1 · %2").arg(i.humanReadableName(), e.ip().toString()), e.ip().toString());
    }
    int at = m_interface->findData(o.interfaceAddress);
    if (at < 0) {
        m_interface->addItem(QStringLiteral("%1 (not on this machine)").arg(o.interfaceAddress), o.interfaceAddress);
        at = m_interface->count() - 1;
    }
    m_interface->setCurrentIndex(at);
    if (!m_addresses->hasFocus()) m_addresses->setText(o.destinations.join(QStringLiteral(", ")));
    m_port->setValue(o.port);
    m_rate->setRange(1, int(o.maxRate()));
    m_rate->setValue(int(std::lround(o.rate)));
    m_priority->setValue(o.priority);
    if (!m_sourceNameEdit->hasFocus()) m_sourceNameEdit->setText(o.sourceName);
    m_transmission->setCurrentIndex(o.transmission == Transmission::Partial ? 1 : 0);
    m_wireFirst->setRange(art ? 0 : 1, o.highestUniverse());
    m_patchFirst->setValue(o.firstSourceUniverse);
    m_wireFirst->setValue(o.firstUniverse);
    m_count->setValue(o.universeCount);
    m_form->setRowVisible(m_addresses, unicast);
    m_form->setRowVisible(m_priority, !art);
    m_form->setRowVisible(m_sourceNameEdit, !art);
    m_form->setRowVisible(m_patchFirst, m_mapping);
    m_form->setRowVisible(m_wireFirst, m_mapping);
    m_form->setRowVisible(m_count, m_mapping);
    QString problem = o.validate();
    if (problem.isEmpty() && m_mapping) problem = o.validateMapping();
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    m_updating = false;
}

} // namespace pxs::dmx
