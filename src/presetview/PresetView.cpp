#include "pxs/PresetView.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMimeData>
#include <QRandomGenerator>
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
            if (prefix.empty() || n.path().rfind(prefix, 0) == 0) out.push_back(n.path());
        }
    }
    for (const auto &c : n.children()) collectHoldablePaths(*c.second, prefix, out);
}

} // namespace

void addParameterEditors(QTreeWidget &tree, QTreeWidgetItem &item, const std::string &path,
                         const pxs::core::tree::Preset &base, pxs::core::tree::Preset &edits,
                         double fadeOverride, bool step, const std::function<void()> &changed)
{
    const auto effective = base.resolved(step ? edits : pxs::core::tree::Preset{}, fadeOverride);
    for (int column = 1; column <= 3; ++column) {
        auto *edit = new QLineEdit;
        auto &numbers = column == 2 ? edits.times : edits.delays;
        const auto &effectiveNumbers = column == 2 ? effective.times : effective.delays;
        const auto value = edits.values.find(path);
        const auto number = numbers.find(path);
        if (column == 1) {
            edit->setText(value == edits.values.end() ? QString() : QString::fromStdString(value->second));
            edit->setPlaceholderText(QString::fromStdString(effective.values.at(path)));
            edit->setToolTip(QStringLiteral("OSCQuery value as JSON arguments. Clear to inherit in a step."));
        } else {
            if (number != numbers.end()) edit->setText(QString::number(number->second));
            edit->setPlaceholderText(QStringLiteral("%1 · inherited").arg(effectiveNumbers.at(path)));
            edit->setToolTip(QStringLiteral("Seconds. Clear to inherit; zero is explicit."));
        }
        tree.setItemWidget(&item, column, edit);
        QObject::connect(edit, &QLineEdit::editingFinished, edit, [edit, path, column, step, &edits, &numbers, changed] {
            const QString text = edit->text().trimmed();
            if (column == 1) {
                if (text.isEmpty() && step) edits.values.erase(path);
                else {
                    const auto json = QJsonDocument::fromJson(text.toUtf8());
                    if (!json.isArray() || json.array().isEmpty()) return;
                    edits.values[path] = json.toJson(QJsonDocument::Compact).toStdString();
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
    m_presetList->setDragEnabled(true);
    m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_presetList, 1);

    splitter->addWidget(leftWidget);

    // ================= Right Pane (Preset & Scope Inspector) =================
    auto *rightWidget = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto *rightHeader = new QHBoxLayout;
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(QStringLiteral("Preset name..."));
    m_nameEdit->setEnabled(false);

    m_updateBtn = new QPushButton(QStringLiteral("Update Live"));
    m_updateBtn->setToolTip(QStringLiteral("Update values in this preset from the live tree state"));
    m_updateBtn->setEnabled(false);

    m_recallBtn = new QPushButton(QStringLiteral("▶ Recall (GO)"));
    m_recallBtn->setObjectName(QStringLiteral("go"));
    m_recallBtn->setStyleSheet(QStringLiteral("QPushButton { color: #4cd964; font-weight: bold; }"));
    m_recallBtn->setToolTip(QStringLiteral("Recall this preset onto the tree"));
    m_recallBtn->setEnabled(false);

    rightHeader->addWidget(m_nameEdit, 1);
    rightHeader->addWidget(m_updateBtn);
    m_fade = new QDoubleSpinBox;
    m_fade->setRange(0, 3600);
    m_fade->setDecimals(3);
    m_fade->setSuffix(QStringLiteral(" s"));
    rightHeader->addWidget(new QLabel(QStringLiteral("Fade")));
    rightHeader->addWidget(m_fade);
    connect(m_fade, &QDoubleSpinBox::editingFinished, this, [this] {
        const int row = currentPresetIndex();
        if (row < 0 || row >= int(m_presets.size()) || m_presets[row].fade == m_fade->value()) return;
        m_presets[row].fade = m_fade->value();
        emit presetsChanged();
    });
    rightHeader->addWidget(m_recallBtn);
    rightLayout->addLayout(rightHeader);

    m_paramsTree = new QTreeWidget;
    m_paramsTree->setHeaderLabels({QStringLiteral("Parameter (Scope)"), QStringLiteral("Captured Value")});
    m_paramsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_paramsTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_paramsTree->header()->resizeSection(1, 140);
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
            for (int k = 0; k < top->childCount(); ++k) top->child(k)->setCheckState(0, Qt::Checked);
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
            for (int k = 0; k < top->childCount(); ++k) top->child(k)->setCheckState(0, Qt::Unchecked);
        }
        m_updatingTree = false;
        m_presets[size_t(row)].values.clear();
        emit presetsChanged();
    });
}

PresetView::~PresetView() = default;

void PresetView::rebuildScopes()
{
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
}

void PresetView::setPresets(const std::vector<pxs::core::tree::Preset> &presets)
{
    const int selected = currentPresetIndex();
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
        auto *item = new QListWidgetItem(QStringLiteral("%1   (%2 params)").arg(name).arg(p.values.size()));
        item->setData(Qt::UserRole, QByteArray::fromStdString(p.id));
        m_presetList->addItem(item);
    }
    if (current >= 0 && current < m_presetList->count()) m_presetList->setCurrentRow(current);
    else if (m_presetList->count() > 0) m_presetList->setCurrentRow(0);
    else showPresetDetails(-1);
}

void PresetView::captureCurrent(const QString &customName)
{
    const std::string prefix = m_scopeCombo->currentData().toString().toStdString();
    std::vector<std::string> paths;
    collectHoldablePaths(m_tree.root(), prefix, paths);

    pxs::core::tree::Preset p = pxs::core::tree::capture(m_tree, paths);
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
    m_statusLabel->setText(QStringLiteral("Preset '%1' capturé (%2 paramètres)").arg(name).arg(p.values.size()));
    emit presetsChanged();
}

bool PresetView::recallPreset(int index)
{
    if (index < 0 || index >= int(m_presets.size())) return false;
    const auto &p = m_presets[size_t(index)];
    const auto name = QString::fromStdString(p.name);
    if (m_recall) {
        m_recall(index);
        m_statusLabel->setText(QStringLiteral("Recall: %1").arg(name));
    } else {
        const auto res = pxs::core::tree::recall(m_tree, p);
        m_statusLabel->setText(QStringLiteral("%1 values applied").arg(res.applied));
    }
    emit presetRecalled(index, name);
    return true;
}

void PresetView::showPresetDetails(int index)
{
    m_updatingTree = true;
    m_paramsTree->clear();
    m_paramsTree->setHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value"), QStringLiteral("Time (s)"), QStringLiteral("Delay (s)")});
    m_fade->setEnabled(index >= 0 && index < int(m_presets.size()));
    if (index < 0 || index >= int(m_presets.size())) {
        m_nameEdit->setEnabled(false);
        m_nameEdit->clear();
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
    m_updatingTree = false;
}

void PresetView::updateActivePresetFromLive()
{
    const int row = m_presetList->currentRow();
    if (row < 0 || row >= int(m_presets.size())) return;
    auto &p = m_presets[size_t(row)];

    // Update only the currently checked addresses
    std::vector<std::string> checkedAddresses;
    for (int i = 0; i < m_paramsTree->topLevelItemCount(); ++i) {
        auto *cat = m_paramsTree->topLevelItem(i);
        for (int k = 0; k < cat->childCount(); ++k) {
            auto *leaf = cat->child(k);
            if (leaf->checkState(0) == Qt::Checked) {
                checkedAddresses.push_back(leaf->data(0, Qt::UserRole).toString().toStdString());
            }
        }
    }
    if (checkedAddresses.empty()) {
        // If tree is empty, capture from selected scope
        captureCurrent(QString::fromStdString(p.name));
        return;
    }
    p = pxs::core::tree::capture(m_tree, checkedAddresses);
    p.name = m_nameEdit->text().toStdString();
    showPresetDetails(row);
    refreshPresetsList();
    m_statusLabel->setText(QStringLiteral("Preset '%1' mis à jour depuis le live").arg(QString::fromStdString(p.name)));
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
