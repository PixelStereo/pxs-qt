#include "pxs/PresetView.h"
#include <Interactions.h>

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

void collectHoldablePaths(const pxs::core::tree::Node &n, const std::string &prefix, std::vector<std::string> &out)
{
    if (n.kind() == pxs::core::tree::Kind::Parameter && n.access == 3 && n.get && n.set) {
        if (!n.parameter() || n.parameter()->isStored()) {
            if (prefix.empty() || n.path() == prefix || n.path().rfind(prefix + "/", 0) == 0) out.push_back(n.path());
        }
    }
    for (const auto &c : n.children()) collectHoldablePaths(*c.second, prefix, out);
}

} // namespace

namespace {
const QString kTimelinesScope = QStringLiteral("timelines");
const QString kTimelineScope = QStringLiteral("timeline:");

std::optional<Animation> readDefinition(const std::string &text)
{
    return animationDefinitionFromJson(QJsonDocument::fromJson(QByteArray::fromStdString(text)).object());
}

std::string writeDefinition(const Animation &animation)
{
    return QJsonDocument(animationDefinitionToJson(animation)).toJson(QJsonDocument::Compact).toStdString();
}

void showTrack(QTreeWidgetItem *parent, const AnimTrack &track, const QString &name)
{
    const auto target = track.param.isEmpty() ? QStringLiteral("Unassigned") : track.param;
    const QString source = track.oscillator
        ? QStringLiteral("%1 · %2 s period").arg(animWaveKeys().value(int(track.wave)).replace(QLatin1Char('_'), QLatin1Char(' '))).arg(track.period)
        : QStringLiteral("%1 key(s)").arg(track.keys.size());
    auto *row = new QTreeWidgetItem(parent, {name, target + QStringLiteral(" · ") + source});
    for (size_t k = 0; k < track.keys.size(); ++k) {
        const auto &key = track.keys[k];
        const auto value = key.isCurrentValue ? QStringLiteral("Current value") : track.color
            ? QStringLiteral("RGB %1, %2, %3").arg(key.rgb[0]).arg(key.rgb[1]).arg(key.rgb[2])
            : QString::number(key.v);
        new QTreeWidgetItem(row, {QStringLiteral("Key at %1 s").arg(key.t), value});
    }
    for (const auto &data : track.dataFilters) {
        const auto filter = InteractionStage::fromJson(data.toObject());
        QString description;
        for (int i = 0; i < kFilterSettingCount; ++i) {
            const auto setting = interactionFilterSetting(filter.kind, i);
            if (setting.name.isEmpty()) continue;
            if (!description.isEmpty()) description += QStringLiteral(" · ");
            description += QStringLiteral("%1 %2").arg(setting.name).arg(filter.setting(i));
        }
        new QTreeWidgetItem(row, {filter.displayName() + (filter.enabled ? QString() : QStringLiteral(" (bypassed)")), description});
    }
    for (size_t i = 0; i < track.axes.size(); ++i)
        showTrack(row, track.axes[i], i == 0 ? QStringLiteral("X") : QStringLiteral("Y"));
    row->setExpanded(true);
}
}

std::optional<std::vector<Animation>> prepareTimelines(const pxs::core::tree::Preset &preset,
                                                       const std::function<bool(quint64)> &exists)
{
    std::vector<Animation> timelines;
    for (const auto &[id, definition] : preset.timelines) {
        const auto timeline = readDefinition(definition);
        if (!timeline || std::to_string(timeline->id) != id || !exists(timeline->id)) return std::nullopt;
        timelines.push_back(*timeline);
    }
    return timelines;
}

void addTimelineDetails(QTreeWidget &tree, const pxs::core::tree::Preset &preset)
{
    if (preset.timelines.empty()) return;
    auto *section = new QTreeWidgetItem(&tree, {QStringLiteral("Timelines"), QString::number(preset.timelines.size())});
    section->setExpanded(true);
    for (const auto &[id, definition] : preset.timelines) {
        const auto timeline = readDefinition(definition);
        if (!timeline) continue;
        auto *row = new QTreeWidgetItem(section, {timeline->name, QStringLiteral("%1 s · %2 track(s)").arg(timeline->duration).arg(timeline->tracks.size())});
        row->setExpanded(true);
        for (size_t i = 0; i < timeline->tracks.size(); ++i)
            showTrack(row, timeline->tracks[i], QStringLiteral("Track %1").arg(i + 1));
    }
}

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
        if (column == 1) {
            edit->setText(value == edits.values.end() ? QString() : QString::fromStdString(value->second));
            edit->setPlaceholderText(QString::fromStdString(base.values.at(path)));
            edit->setToolTip(QStringLiteral("OSCQuery value as JSON arguments. Clear to inherit in a step."));
        } else {
            if (number != numbers.end()) edit->setText(QString::number(number->second));
            edit->setPlaceholderText(QStringLiteral("%1 · inherited").arg(inherited));
            edit->setToolTip(QStringLiteral("Seconds. Clear to inherit; zero is explicit."));
        }
        tree.setItemWidget(&item, column, edit);
        QObject::connect(edit, &QLineEdit::editingFinished, edit, [edit, path, column, step, &edits, &numbers, &tree, changed] {
            const QString text = edit->text().trimmed();
            if (column == 1) {
                if (text.isEmpty() && step) edits.values.erase(path);
                else {
                    const auto json = QJsonDocument::fromJson(text.toUtf8());
                    if (!json.isArray() || json.array().isEmpty()) return;
                    edits.values[path] = json.toJson(QJsonDocument::Compact).toStdString();
                    if (auto *item = !step ? tree.itemAt(edit->mapTo(tree.viewport(), QPoint(1, 1))) : nullptr)
                        item->setData(1, Qt::UserRole, QString::fromStdString(edits.values[path]));
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

    leftToolbar->addWidget(m_captureBtn);
    leftToolbar->addWidget(m_duplicateBtn);
    leftToolbar->addWidget(m_deleteBtn);
    leftLayout->addLayout(leftToolbar);

    m_presetList = new PresetList;
    auto *timer = new QTimer(this);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        m_playback.advance(m_tree, m_recallClock.restart() / 1000.0);
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
    m_updateBtn->setToolTip(QStringLiteral("Refresh the captured parameters and timelines from the show"));
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
    m_checkAllBtn = new QPushButton(QStringLiteral("Include All"));
    m_uncheckAllBtn = new QPushButton(QStringLiteral("Exclude All"));
    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("caption"));

    bottomRight->addWidget(m_checkAllBtn);
    bottomRight->addWidget(m_uncheckAllBtn);
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

    connect(m_presetList, &QListWidget::currentRowChanged, this, [this](int row) { showPresetDetails(row); });
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

    connect(m_paramsTree, &QTreeWidget::itemChanged, this, &PresetView::applyPresetTreeItemChange);

    connect(m_checkAllBtn, &QPushButton::clicked, this, [this] {
        const int row = m_presetList->currentRow();
        if (row < 0 || row >= int(m_presets.size())) return;
        m_updatingTree = true;
        for (int i = 0; i < m_paramsTree->topLevelItemCount(); ++i) {
            auto *top = m_paramsTree->topLevelItem(i);
            for (int k = 0; k < top->childCount(); ++k) {
                auto *leaf = top->child(k);
                const auto address = leaf->data(0, Qt::UserRole).toString().toStdString();
                if (address.empty()) continue;
                leaf->setCheckState(0, Qt::Checked);
                m_presets[row].values.try_emplace(address, leaf->data(1, Qt::UserRole).toString().toStdString());
            }
        }
        m_updatingTree = false;
        updateActivePresetFromLive();
    });

    connect(m_uncheckAllBtn, &QPushButton::clicked, this, [this] {
        const int row = m_presetList->currentRow();
        if (row < 0 || row >= int(m_presets.size())) return;
        m_updatingTree = true;
        for (int i = 0; i < m_paramsTree->topLevelItemCount(); ++i) {
            auto *top = m_paramsTree->topLevelItem(i);
            for (int k = 0; k < top->childCount(); ++k)
                if (top->child(k)->data(0, Qt::UserRole).isValid()) top->child(k)->setCheckState(0, Qt::Unchecked);
        }
        m_updatingTree = false;
        m_presets[size_t(row)].values.clear();
        emit presetsChanged();
    });
}

PresetView::~PresetView() = default;

void PresetView::rebuildScopes()
{
    const auto previous = m_scopeCombo->currentData();
    m_scopeCombo->clear();
    const auto scopes = m_host ? m_host->scopes() : std::vector<std::pair<QString, QString>>{{QStringLiteral(""), QStringLiteral("Tout l'arbre (Self)")}};
    for (const auto &[prefix, label] : scopes) m_scopeCombo->addItem(label, prefix);
    if (m_scopeCombo->count() == 1) {
        // Also automatically discover primary top-level branches
        for (const auto &child : m_tree.root().children()) {
            if (child.second->kind() == pxs::core::tree::Kind::Container || child.second->kind() == pxs::core::tree::Kind::Parameter) {
                const QString path = QString::fromStdString(child.second->path());
                m_scopeCombo->addItem(QStringLiteral("Branche : %1").arg(path), path);
            }
        }
    }
    if (m_host) {
        const auto timelines = m_host->timelineScopes();
        if (!timelines.empty()) m_scopeCombo->addItem(QStringLiteral("All timelines"), kTimelinesScope);
        for (const auto &[id, name] : timelines)
            m_scopeCombo->addItem(QStringLiteral("Timeline · %1").arg(name), kTimelineScope + QString::number(id));
    }
    const int index = m_scopeCombo->findData(previous);
    if (index >= 0) m_scopeCombo->setCurrentIndex(index);
}

void PresetView::setPresets(const std::vector<pxs::core::tree::Preset> &presets)
{
    const int selected = currentPresetIndex();
    m_updatingTree = true;
    m_paramsTree->clear();
    m_updatingTree = false;
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
        auto *item = new QListWidgetItem(p.timelines.empty() ? QStringLiteral("%1   (%2 params)").arg(name).arg(p.values.size())
                                                                  : QStringLiteral("%1   (%2 timelines)").arg(name).arg(p.timelines.size()));
        item->setData(Qt::UserRole, QByteArray::fromStdString(p.id));
        m_presetList->addItem(item);
    }
    if (current >= 0 && current < m_presetList->count()) m_presetList->setCurrentRow(current);
    else if (m_presetList->count() > 0) m_presetList->setCurrentRow(0);
    else showPresetDetails(-1);
}

void PresetView::captureCurrent(const QString &customName)
{
    const QString scope = m_scopeCombo->currentData().toString();
    pxs::core::tree::Preset p;
    p.scope = scope.toStdString();
    if (m_host && (scope == kTimelinesScope || scope.startsWith(kTimelineScope))) {
        for (const auto &[id, name] : m_host->timelineScopes()) {
            if (scope != kTimelinesScope && scope != kTimelineScope + QString::number(id)) continue;
            if (const auto timeline = m_host->readTimeline(id)) p.timelines[std::to_string(id)] = writeDefinition(*timeline);
        }
    } else {
        std::vector<std::string> paths;
        collectHoldablePaths(m_tree.root(), p.scope, paths);
        if (!paths.empty() || scope.isEmpty()) p.values = pxs::core::tree::capture(m_tree, paths).values;
    }
    if (p.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("No known values in this scope"));
        return;
    }
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
        const auto timelines = prepareTimelines(p, [this](quint64 id) { return m_host && m_host->readTimeline(id).has_value(); });
        if (!timelines) {
            m_statusLabel->setText(QStringLiteral("Timeline unavailable"));
            return false;
        }
        for (const auto &timeline : *timelines) m_host->restoreTimeline(timeline);
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
    m_updatingTree = true;
    m_paramsTree->clear();
    m_paramsTree->setHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value"), QStringLiteral("Time (s)"), QStringLiteral("Delay (s)"), QStringLiteral("Curve")});
    m_fade->setEnabled(index >= 0 && index < int(m_presets.size()));
    if (index < 0 || index >= int(m_presets.size())) {
        m_nameEdit->setEnabled(false);
        m_nameEdit->clear();
        m_scopeSummary->clear();
        m_updateBtn->setEnabled(false);
        m_recallBtn->setEnabled(false);
        m_updatingTree = false;
        return;
    }
    auto &p = m_presets[size_t(index)];
    m_fade->setValue(p.fade);
    m_nameEdit->setEnabled(true);
    m_nameEdit->setText(QString::fromStdString(p.name));
    m_updateBtn->setEnabled(true);
    const int scope = m_scopeCombo->findData(QString::fromStdString(p.scope));
    const auto scopeName = scope >= 0 ? m_scopeCombo->itemText(scope) : QString::fromStdString(p.scope);
    m_updateBtn->setToolTip(QStringLiteral("Update from captured scope: %1").arg(scopeName));
    m_scopeSummary->setText(QStringLiteral("Captured scope · %1").arg(scopeName));
    m_recallBtn->setEnabled(true);

    // Group items by category (e.g. top-level path segment)
    std::map<QString, QTreeWidgetItem *> categories;
    for (const auto &[address, valJson] : p.values) {
        const QString fullPath = QString::fromStdString(address);
        const QStringList parts = fullPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const QString catName = parts.isEmpty() ? QStringLiteral("/") : QStringLiteral("/%1").arg(parts[0]);

        auto &catItem = categories[catName];
        if (!catItem) {
            catItem = new QTreeWidgetItem(m_paramsTree, {catName, QString()});
            catItem->setExpanded(true);
            addParameterEditors(*m_paramsTree, *catItem, catName.toStdString(), p, p, -1, false, [this] { emit presetsChanged(); });
            QFont f = catItem->font(0);
            f.setBold(true);
            catItem->setFont(0, f);
        }

        const QString leafName = parts.size() > 1 ? parts.mid(1).join(QLatin1Char('/')) : fullPath;
        auto *leaf = new QTreeWidgetItem(catItem, {leafName, formatJsonValue(valJson)});
        leaf->setCheckState(0, Qt::Checked);
        leaf->setData(0, Qt::UserRole, fullPath);
        leaf->setData(1, Qt::UserRole, QString::fromStdString(valJson));
        addParameterEditors(*m_paramsTree, *leaf, address, p, p, -1, false, [this] { emit presetsChanged(); });
    }
    addTimelineDetails(*m_paramsTree, p);
    m_updatingTree = false;
}

void PresetView::updateActivePresetFromLive()
{
    const int row = m_presetList->currentRow();
    if (row < 0 || row >= int(m_presets.size())) return;
    auto &p = m_presets[size_t(row)];

    std::vector<std::string> addresses;
    addresses.reserve(p.values.size());
    for (const auto &[path, value] : p.values) addresses.push_back(path);
    bool updated = false;
    if (!addresses.empty()) {
        for (const auto &[path, value] : pxs::core::tree::capture(m_tree, addresses).values) {
            p.values[path] = value;
            updated = true;
        }
    }
    if (m_host) {
        for (auto &[id, definition] : p.timelines) {
            if (const auto timeline = m_host->readTimeline(QString::fromStdString(id).toULongLong())) {
                definition = writeDefinition(*timeline);
                updated = true;
            }
        }
    }
    if (!updated) {
        m_statusLabel->setText(QStringLiteral("No captured targets are available"));
        return;
    }
    showPresetDetails(row);
    refreshPresetsList();
    m_statusLabel->setText(QStringLiteral("Updated %1").arg(QString::fromStdString(p.name)));
    emit presetsChanged();
}

void PresetView::applyPresetTreeItemChange(QTreeWidgetItem *item, int column)
{
    if (m_updatingTree || column != 0 || !item || !item->parent()) return;
    const int row = m_presetList->currentRow();
    if (row < 0 || row >= int(m_presets.size())) return;

    auto &p = m_presets[size_t(row)];
    const std::string address = item->data(0, Qt::UserRole).toString().toStdString();
    if (address.empty()) return;

    if (item->checkState(0) == Qt::Checked) {
        // Re-include value
        const std::string val = item->data(1, Qt::UserRole).toString().toStdString();
        p.values[address] = val;
    } else {
        // Exclude from preset recall
        p.values.erase(address);
    }
    emit presetsChanged();
}

} // namespace pxs::presetview
