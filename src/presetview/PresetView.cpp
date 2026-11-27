#include "pxs/PresetView.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMimeData>
#include <QRandomGenerator>
#include <QTimer>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <cmath>

namespace pxs::presetview {

namespace {

class PresetList : public QListWidget {
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        auto *data = new QMimeData;
        if (!items.empty()) data->setData("application/x-pxs-preset", items.front()->data(Qt::UserRole).toByteArray());
        return data;
    }
};

std::string newPresetId()
{
    return std::to_string(QRandomGenerator::global()->generate64() | (quint64(1) << 63));
}

QString formatJsonValue(const std::string &jsonText)
{
    if (jsonText.empty() || jsonText == "[]") return QString();
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonText));
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (arr.size() == 1) {
            const QJsonValue v = arr.first();
            if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            if (v.isDouble()) return QString::number(v.toDouble(), 'g', 4);
            if (v.isString()) return v.toString();
            return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        }
        QStringList parts;
        for (const QJsonValue &v : arr) {
            if (v.isDouble()) parts << QString::number(v.toDouble(), 'g', 4);
            else if (v.isString()) parts << v.toString();
            else if (v.isBool()) parts << (v.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            else parts << QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        }
        return parts.join(QStringLiteral(", "));
    }
    return QString::fromStdString(jsonText);
}

QString labelFor(const pxs::core::tree::Tree &tree, const std::string &path)
{
    if (const auto *node = tree.find(path); node && !node->label.empty())
        return QString::fromStdString(node->label);
    auto text = QString::fromStdString(path.substr(path.rfind('/') + 1)).replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!text.isEmpty()) text[0] = text[0].toUpper();
    return text;
}

} // namespace

void addParameterEditors(QTreeWidget &tree, QTreeWidgetItem &item, const std::string &path,
                         const pxs::core::tree::Preset &base, pxs::core::tree::Preset &edits,
                         double fadeOverride, bool step, const std::function<void()> &changed)
{
    const bool group = !base.values.count(path);
    for (int column = group ? 2 : 1; column <= 3; ++column) {
        auto *edit = new QLineEdit;
        auto &numbers = column == 2 ? edits.times : edits.delays;
        const double inherited = column == 2 ? base.timeAt(path, fadeOverride) : base.delayAt(path);
        const auto value = edits.values.find(path);
        const auto number = numbers.find(path);
        QJsonValue scalar;
        if (column == 1) {
            const auto stored = value == edits.values.end() ? QString() : QString::fromStdString(value->second);
            const auto args = QJsonDocument::fromJson(stored.toUtf8()).array();
            if (!step && args.size() == 1 && (args[0].isBool() || args[0].isDouble() || args[0].isString())) scalar = args[0];
            edit->setText(scalar.isString() ? scalar.toString() : scalar.isDouble() ? QString::number(scalar.toDouble(), 'g', 17)
                : scalar.isBool() ? (scalar.toBool() ? QStringLiteral("true") : QStringLiteral("false")) : stored);
            edit->setPlaceholderText(QString::fromStdString(base.values.at(path)));
            if (step) edit->setToolTip(QStringLiteral("Values as JSON arguments. Clear to inherit."));
        } else {
            if (number != numbers.end()) edit->setText(QString::number(number->second));
            edit->setPlaceholderText(QStringLiteral("%1 · inherited").arg(inherited));
            edit->setToolTip(QStringLiteral("Seconds. Clear to inherit; zero is explicit."));
        }
        tree.setItemWidget(&item, column, edit);
        QObject::connect(edit, &QLineEdit::editingFinished, edit, [edit, path, column, step, scalar, &edits, &numbers, changed] {
            const QString text = edit->text().trimmed();
            if (column == 1) {
                if (text.isEmpty() && step) edits.values.erase(path);
                else {
                    QJsonArray args;
                    if (scalar.isString()) args.append(edit->text());
                    else if (scalar.isDouble()) {
                        bool ok = false;
                        const double number = text.toDouble(&ok);
                        if (!ok || !std::isfinite(number)) return;
                        args.append(number);
                    } else if (scalar.isBool()) {
                        if (text != QStringLiteral("true") && text != QStringLiteral("false")) return;
                        args.append(text == QStringLiteral("true"));
                    } else args = QJsonDocument::fromJson(text.toUtf8()).array();
                    if (args.isEmpty()) return;
                    edits.values[path] = QJsonDocument(args).toJson(QJsonDocument::Compact).toStdString();
                }
            } else if (text.isEmpty()) numbers.erase(path);
            else {
                bool ok = false;
                const double seconds = text.toDouble(&ok);
                if (!ok || !(seconds >= 0 && seconds <= 3600)) return;
                numbers[path] = seconds;
            }
            changed();
        });
    }
    auto *curve = new QComboBox;
    const QStringList curves{QStringLiteral("Linear"), QStringLiteral("Ease in"), QStringLiteral("Ease out"),
                             QStringLiteral("Ease in/out"), QStringLiteral("Cubic in"), QStringLiteral("Cubic out")};
    curve->addItem(QStringLiteral("Inherited · %1").arg(curves.value(base.curveAt(path))));
    curve->addItems(curves);
    if (const auto found = edits.curves.find(path); found != edits.curves.end()) curve->setCurrentIndex(found->second + 1);
    tree.setItemWidget(&item, 4, curve);
    QObject::connect(curve, &QComboBox::currentIndexChanged, curve, [path, &edits, changed](int index) {
        if (index <= 0) edits.curves.erase(path);
        else edits.curves[path] = index - 1;
        changed();
    });
}

PresetView::PresetView(pxs::core::tree::Tree &tree, QUndoStack *undo, PresetHost *host, QWidget *parent)
    : QWidget(parent), m_tree(tree), m_undo(undo), m_host(host)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // ================= Left Pane (Presets Library) =================
    auto *leftWidget = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto *scopeLayout = new QHBoxLayout;
    auto *scopeLabel = new QLabel(QStringLiteral("Scope:"));
    scopeLabel->setObjectName(QStringLiteral("caption"));
    m_scopeCombo = new QComboBox;
    m_scopeCombo->setObjectName(QStringLiteral("presetScope"));
    m_scopeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    scopeLayout->addWidget(scopeLabel);
    scopeLayout->addWidget(m_scopeCombo, 1);
    leftLayout->addLayout(scopeLayout);

    auto *leftToolbar = new QHBoxLayout;
    m_captureBtn = new QPushButton(QStringLiteral("+ Capture"));
    m_captureBtn->setObjectName(QStringLiteral("accent"));
    m_captureBtn->setToolTip(QStringLiteral("Capture the state of the selected scope into a new preset"));

    m_duplicateBtn = new QPushButton(QStringLiteral("⧉"));
    m_duplicateBtn->setFixedWidth(32);
    m_duplicateBtn->setToolTip(QStringLiteral("Duplicate selected preset"));

    m_deleteBtn = new QPushButton(QStringLiteral("−"));
    m_deleteBtn->setFixedWidth(32);
    m_deleteBtn->setToolTip(QStringLiteral("Delete selected preset"));

    auto *empty = new QPushButton(QStringLiteral("New Empty"));
    empty->setObjectName(QStringLiteral("emptyPreset"));
    empty->setToolTip(QStringLiteral("Create a preset and choose individual parameters"));
    leftToolbar->addWidget(m_captureBtn);
    leftToolbar->addWidget(empty);
    leftToolbar->addWidget(m_duplicateBtn);
    leftToolbar->addWidget(m_deleteBtn);
    leftLayout->addLayout(leftToolbar);

    m_presetList = new PresetList;
    auto *timer = new QTimer(this);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        const double seconds = m_recallClock.restart() / 1000.0;
        m_playback.advance(m_tree, seconds);
        if (!m_playback.active()) timer->stop();
    });
    m_playbackTimer = timer;
    m_presetList->setDragDropMode(QAbstractItemView::DragOnly);
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_presetList, 1);

    splitter->addWidget(leftWidget);

    // ================= Right Pane (Preset & Scope Inspector) =================
    auto *rightWidget = m_inspector = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto *rightHeader = new QHBoxLayout;
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(QStringLiteral("Preset name..."));
    m_nameEdit->setEnabled(false);

    m_updateBtn = new QPushButton(QStringLiteral("Update Preset"));
    m_updateBtn->setObjectName(QStringLiteral("updatePreset"));
    m_updateBtn->setToolTip(QStringLiteral("Read the saved parameter rows again from the current show"));
    m_updateBtn->setEnabled(false);

    m_recallBtn = new QPushButton(QStringLiteral("▶ Recall (GO)"));
    m_recallBtn->setObjectName(QStringLiteral("go"));
    m_recallBtn->setToolTip(QStringLiteral("Recall this preset onto the tree"));
    m_recallBtn->setEnabled(false);

    rightHeader->addWidget(m_nameEdit, 1);
    rightHeader->addWidget(m_updateBtn);
    rightLayout->addLayout(rightHeader);
    auto *timingHeader = new QHBoxLayout;
    m_fade = new QDoubleSpinBox;
    m_fade->setRange(0, 3600);
    m_fade->setDecimals(3);
    m_fade->setSuffix(QStringLiteral(" s"));
    timingHeader->addWidget(new QLabel(QStringLiteral("Fade")));
    timingHeader->addWidget(m_fade);
    timingHeader->addStretch();
    connect(m_fade, &QDoubleSpinBox::editingFinished, this, [this] {
        const int row = currentPresetIndex();
        if (row < 0 || row >= int(m_presets.size()) || m_presets[row].fade == m_fade->value()) return;
        m_presets[row].fade = m_fade->value();
        emit presetsChanged();
    });
    timingHeader->addWidget(m_recallBtn);
    rightLayout->addLayout(timingHeader);

    m_scopeSummary = new QLabel;
    m_scopeSummary->setWordWrap(true);
    rightLayout->addWidget(m_scopeSummary);

    m_paramsTree = new QTreeWidget;
    m_paramsTree->setHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value"), QStringLiteral("Time (s)"), QStringLiteral("Delay (s)"), QStringLiteral("Curve")});
    m_paramsTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_paramsTree->header()->setStretchLastSection(false);
    m_paramsTree->header()->setCascadingSectionResizes(false);
    m_paramsTree->header()->resizeSection(0, 140);
    for (int i = 1; i <= 3; ++i) m_paramsTree->header()->resizeSection(i, 72);
    rightLayout->addWidget(m_paramsTree, 1);

    auto *bottomRight = new QHBoxLayout;
    m_editBtn = new QPushButton(QStringLiteral("Edit"));
    m_editBtn->setObjectName(QStringLiteral("editPreset"));
    m_editBtn->setCheckable(true);
    m_addParameter = new QPushButton(QStringLiteral("Add Parameter"));
    m_addParameter->setObjectName(QStringLiteral("addPresetParameter"));
    m_removeParameter = new QPushButton(QStringLiteral("Remove Parameter"));
    m_removeParameter->setObjectName(QStringLiteral("removePresetParameter"));
    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("caption"));

    bottomRight->addWidget(m_editBtn);
    bottomRight->addWidget(m_addParameter);
    bottomRight->addWidget(m_removeParameter);
    bottomRight->addSpacing(10);
    bottomRight->addWidget(m_statusLabel, 1);
    rightLayout->addLayout(bottomRight);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    outer->addWidget(splitter, 1);

    // Context menu on presets list
    m_presetList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_presetList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_presetList->currentRow();
        if (row < 0 || row >= int(m_presets.size())) return;
        QMenu menu(this);
        menu.addAction(QStringLiteral("Recall (GO)"), this, [this, row] { recallPreset(row); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Duplicate"), this, [this, row] {
            pxs::core::tree::Preset copy = m_presets[size_t(row)];
            copy.id = newPresetId();
            copy.name += " (Copy)";
            m_presets.insert(m_presets.begin() + row + 1, copy);
            refreshPresetsList();
            selectPreset(row + 1);
            emit presetsChanged();
        });
        menu.addAction(QStringLiteral("Delete"), this, [this, row] {
            m_presets.erase(m_presets.begin() + row);
            refreshPresetsList();
            emit presetsChanged();
        });
        menu.exec(m_presetList->viewport()->mapToGlobal(pos));
    });

    // Connections
    rebuildScopes();

    connect(m_captureBtn, &QPushButton::clicked, this, [this] { captureCurrent(); });
    connect(empty, &QPushButton::clicked, this, [this] {
        if (m_scopeCombo->currentIndex() < 0) return;
        pxs::core::tree::Preset preset;
        preset.scope = m_scopeCombo->currentData().toString().toStdString();
        createPreset(std::move(preset));
        m_editBtn->setChecked(true);
    });
    connect(m_duplicateBtn, &QPushButton::clicked, this, [this] {
        const int row = m_presetList->currentRow();
        if (row >= 0 && row < int(m_presets.size())) {
            pxs::core::tree::Preset copy = m_presets[size_t(row)];
            copy.id = newPresetId();
            copy.name += " (Copy)";
            m_presets.insert(m_presets.begin() + row + 1, copy);
            refreshPresetsList();
            selectPreset(row + 1);
            emit presetsChanged();
        }
    });
    connect(m_deleteBtn, &QPushButton::clicked, this, [this] {
        const int row = m_presetList->currentRow();
        if (row >= 0 && row < int(m_presets.size())) {
            m_presets.erase(m_presets.begin() + row);
            refreshPresetsList();
            emit presetsChanged();
        }
    });

    connect(m_presetList, &QListWidget::currentRowChanged, this, [this](int row) {
        const QSignalBlocker block(m_editBtn);
        m_editBtn->setChecked(false);
        showPresetDetails(row);
    });
    connect(m_presetList, &QListWidget::itemClicked, this, [this] { emit presetSelected(); });
    connect(m_presetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        recallPreset(m_presetList->currentRow());
    });

    connect(m_nameEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
        const int row = m_presetList->currentRow();
        if (row >= 0 && row < int(m_presets.size())) {
            m_presets[size_t(row)].name = text.toStdString();
            if (auto *item = m_presetList->item(row)) item->setText(text);
            emit presetsChanged();
        }
    });

    connect(m_updateBtn, &QPushButton::clicked, this, &PresetView::updateActivePresetFromLive);
    connect(m_recallBtn, &QPushButton::clicked, this, [this] { recallPreset(m_presetList->currentRow()); });

    connect(m_editBtn, &QPushButton::toggled, this, [this](bool edit) {
        m_editBtn->setText(edit ? QStringLiteral("Done") : QStringLiteral("Edit"));
        showPresetDetails(currentPresetIndex());
    });
    connect(m_addParameter, &QPushButton::clicked, this, &PresetView::chooseParameter);
    connect(m_removeParameter, &QPushButton::clicked, this, &PresetView::removeSelectedParameter);
    connect(m_paramsTree, &QTreeWidget::currentItemChanged, this, [this](const QTreeWidgetItem *item) {
        m_removeParameter->setEnabled(item && item->data(0, Qt::UserRole).isValid());
    });
    showPresetDetails(-1);
}

PresetView::~PresetView() = default;

void PresetView::rebuildScopes()
{
    const auto previous = m_scopeCombo->currentData();
    m_scopeCombo->clear();
    const auto scopes = m_host ? m_host->scopes() : std::vector<std::pair<QString, QString>>{{{}, QStringLiteral("All parameters")}};
    for (const auto &[prefix, label] : scopes) m_scopeCombo->addItem(label, prefix);
    const int index = m_scopeCombo->findData(previous);
    if (index >= 0) m_scopeCombo->setCurrentIndex(index);
    m_captureBtn->setEnabled(!scopes.empty());
    const int row = currentPresetIndex();
    if (row >= 0 && row < int(m_presets.size())) {
        const int selected = m_scopeCombo->findData(QString::fromStdString(m_presets[row].scope));
        if (selected >= 0) m_scopeSummary->setText(m_scopeCombo->itemText(selected));
    }
}

void PresetView::setPresets(const std::vector<pxs::core::tree::Preset> &presets)
{
    const int selected = currentPresetIndex();
    m_paramsTree->clear();
    m_presets = presets;
    refreshPresetsList();
    if (!m_presets.empty()) selectPreset(std::clamp(selected, 0, int(m_presets.size()) - 1));
}

int PresetView::currentPresetIndex() const
{
    return m_presetList->currentRow();
}

void PresetView::selectPreset(int index)
{
    if (index >= 0 && index < m_presetList->count()) m_presetList->setCurrentRow(index);
}

void PresetView::refreshPresetsList()
{
    const int current = m_presetList->currentRow();
    m_presetList->clear();
    for (size_t i = 0; i < m_presets.size(); ++i) {
        const auto &p = m_presets[i];
        const QString name = p.name.empty() ? QStringLiteral("Preset %1").arg(i + 1) : QString::fromStdString(p.name);
        auto *item = new QListWidgetItem(name);
        item->setToolTip(QStringLiteral("%1 parameters").arg(p.values.size()));
        item->setData(Qt::UserRole, QByteArray::fromStdString(p.id));
        m_presetList->addItem(item);
    }
    if (current >= 0 && current < m_presetList->count()) m_presetList->setCurrentRow(current);
    else if (m_presetList->count() > 0) m_presetList->setCurrentRow(0);
    else showPresetDetails(-1);
}

void PresetView::captureCurrent(const QString &customName)
{
    if (m_scopeCombo->currentIndex() < 0) return;
    const auto scope = m_scopeCombo->currentData().toString().toStdString();
    const auto paths = pxs::core::tree::presetParameters(m_tree, scope);
    if (paths.empty()) {
        m_statusLabel->setText(QStringLiteral("No parameters in this scope"));
        return;
    }
    auto preset = pxs::core::tree::capture(m_tree, paths);
    if (preset.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No values available in this scope"));
        return;
    }
    preset.scope = scope;
    createPreset(std::move(preset), customName);
}

void PresetView::createPreset(pxs::core::tree::Preset p, const QString &customName)
{
    QString name = customName;
    if (name.isEmpty()) {
        const QString scopeText = m_scopeCombo->currentText();
        name = QStringLiteral("Preset %1 (%2)").arg(m_presets.size() + 1).arg(scopeText);
    }
    p.name = name.toStdString();
    p.id = newPresetId();
    m_presets.push_back(p);
    refreshPresetsList();
    selectPreset(int(m_presets.size()) - 1);
    m_statusLabel->setText(QStringLiteral("Captured %1").arg(name));
    emit presetsChanged();
    emit presetSelected();
}

bool PresetView::recallPreset(int index)
{
    if (index < 0 || index >= int(m_presets.size())) return false;
    const auto &p = m_presets[size_t(index)];
    const auto name = QString::fromStdString(p.name);
    if (m_recall) {
        if (!m_recall(index)) {
            m_statusLabel->setText(QStringLiteral("Recall failed"));
            return false;
        }
    } else {
        m_playback.start(m_tree, p);
        if (m_playback.active() && !m_playbackTimer->isActive()) {
            m_recallClock.start();
            m_playbackTimer->start();
        }
    }
    m_statusLabel->setText(QStringLiteral("Recall: %1").arg(name));
    emit presetRecalled(index, name);
    return true;
}

void PresetView::showPresetDetails(int index)
{
    m_paramsTree->clear();
    const bool selected = index >= 0 && index < int(m_presets.size());
    const bool editing = selected && m_editBtn->isChecked();
    m_nameEdit->setEnabled(selected);
    m_fade->setEnabled(selected);
    m_updateBtn->setEnabled(selected);
    m_recallBtn->setEnabled(selected);
    m_editBtn->setEnabled(selected);
    m_editBtn->setText(editing ? QStringLiteral("Done") : QStringLiteral("Edit"));
    m_addParameter->setVisible(editing);
    m_removeParameter->setVisible(editing);
    m_removeParameter->setEnabled(false);
    if (!selected) {
        m_nameEdit->clear();
        m_scopeSummary->clear();
        return;
    }
    auto &preset = m_presets[size_t(index)];
    m_fade->setValue(preset.fade);
    m_nameEdit->setText(QString::fromStdString(preset.name));
    const int scope = m_scopeCombo->findData(QString::fromStdString(preset.scope));
    m_scopeSummary->setText(scope >= 0 ? m_scopeCombo->itemText(scope) : QString::fromStdString(preset.scope));
    m_addParameter->setEnabled(scope >= 0);

    std::map<std::string, QTreeWidgetItem *> groups;
    for (const auto &[path, value] : preset.values) {
        QTreeWidgetItem *parent = m_paramsTree->invisibleRootItem();
        for (size_t end = path.find('/', 1); end != std::string::npos; end = path.find('/', end + 1)) {
            const auto prefix = path.substr(0, end);
            if (prefix.size() <= preset.scope.size()) continue;
            auto &branch = groups[prefix];
            if (!branch) {
                branch = new QTreeWidgetItem(parent, {labelFor(m_tree, prefix)});
                branch->setExpanded(parent == m_paramsTree->invisibleRootItem());
                if (editing)
                    addParameterEditors(*m_paramsTree, *branch, prefix, preset, preset, -1, false, [this] { emit presetsChanged(); });
            }
            parent = branch;
        }
        auto *leaf = new QTreeWidgetItem(parent, {labelFor(m_tree, path), formatJsonValue(value)});
        leaf->setData(0, Qt::UserRole, QString::fromStdString(path));
        leaf->setToolTip(0, QString::fromStdString(path));
        if (editing) {
            addParameterEditors(*m_paramsTree, *leaf, path, preset, preset, -1, false, [this] { emit presetsChanged(); });
        } else {
            leaf->setText(2, QString::number(preset.timeAt(path)));
            leaf->setText(3, QString::number(preset.delayAt(path)));
        }
    }
}

void PresetView::updateActivePresetFromLive()
{
    const int row = currentPresetIndex();
    if (row < 0 || row >= int(m_presets.size())) return;
    auto &preset = m_presets[size_t(row)];
    std::vector<std::string> paths;
    for (const auto &entry : preset.values) paths.push_back(entry.first);
    if (paths.empty()) return;
    const auto captured = pxs::core::tree::capture(m_tree, paths);
    if (captured.values.empty()) {
        m_statusLabel->setText(QStringLiteral("No captured targets are available"));
        return;
    }
    for (const auto &[path, value] : captured.values) preset.values[path] = value;
    showPresetDetails(row);
    m_statusLabel->setText(QStringLiteral("Updated %1").arg(QString::fromStdString(preset.name)));
    emit presetsChanged();
}

void PresetView::chooseParameter()
{
    const int row = currentPresetIndex();
    if (row < 0 || !m_editBtn->isChecked()) return;
    const auto &preset = m_presets[size_t(row)];
    QMenu menu(this);
    std::map<std::string, QMenu *> groups;
    for (const auto &path : pxs::core::tree::presetParameters(m_tree, preset.scope)) {
        if (preset.values.count(path)) continue;
        auto *parent = &menu;
        for (size_t end = path.find('/', 1); end != std::string::npos; end = path.find('/', end + 1)) {
            const auto prefix = path.substr(0, end);
            if (prefix.size() <= preset.scope.size()) continue;
            auto &branch = groups[prefix];
            if (!branch) branch = parent->addMenu(labelFor(m_tree, prefix));
            parent = branch;
        }
        auto *action = parent->addAction(labelFor(m_tree, path));
        action->setData(QString::fromStdString(path));
    }
    if (menu.isEmpty()) menu.addAction(QStringLiteral("No more parameters in this scope"))->setEnabled(false);
    const auto *chosen = menu.exec(m_addParameter->mapToGlobal(QPoint(0, m_addParameter->height())));
    if (!chosen || row != currentPresetIndex()) return;
    const auto path = chosen->data().toString().toStdString();
    if (path.empty()) return;
    const auto capture = pxs::core::tree::capture(m_tree, {path});
    if (capture.values.empty()) return;
    m_presets[size_t(row)].values.insert(capture.values.begin(), capture.values.end());
    showPresetDetails(row);
    emit presetsChanged();
}

void PresetView::removeSelectedParameter()
{
    const int row = currentPresetIndex();
    const auto *item = m_paramsTree->currentItem();
    if (row < 0 || !m_editBtn->isChecked() || !item) return;
    const auto path = item->data(0, Qt::UserRole).toString().toStdString();
    if (path.empty()) return;
    m_presets[size_t(row)].removeParameter(path);
    showPresetDetails(row);
    emit presetsChanged();
}

} // namespace pxs::presetview
