#pragma once
// PresetView: manage and recall presets on a pxs::core::Tree (and optional scopes).
// Left pane: library of presets (create, delete, duplicate, recall/GO).
// Right pane: inspector showing the captured tree parameters with checkboxes to include/exclude parameters in the scope.
#include <pxs/core/tree/preset.h>
#include <pxs/core/tree/tree.h>

#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;

namespace pxs::presetview {

class PresetHost
{
public:
    virtual ~PresetHost() = default;
    // Returns available capture scopes: pair of (prefix path, display label)
    virtual std::vector<std::pair<QString, QString>> scopes() const
    {
        return {{QStringLiteral(""), QStringLiteral("Tout l'arbre (Self)")}};
    }
};

void addParameterEditors(QTreeWidget &tree, QTreeWidgetItem &item, const std::string &path,
                         const pxs::core::tree::Preset &base, pxs::core::tree::Preset &edits,
                         double fadeOverride, bool step, const std::function<void()> &changed);

class PresetView : public QWidget
{
    Q_OBJECT
public:
    PresetView(pxs::core::tree::Tree &tree, QUndoStack *undo = nullptr, PresetHost *host = nullptr, QWidget *parent = nullptr);
    ~PresetView() override;

    const std::vector<pxs::core::tree::Preset> &presets() const { return m_presets; }
    void setRecaller(std::function<void(int)> recall) { m_recall = std::move(recall); }
    void setPresets(const std::vector<pxs::core::tree::Preset> &presets);

    int currentPresetIndex() const;
    void selectPreset(int index);

    // Capture state under the currently selected scope into a new preset
    void captureCurrent(const QString &name = QString());
    // Recall a preset by index
    bool recallPreset(int index);

signals:
    void presetsChanged();
    void presetRecalled(int index, const QString &name);

private:
    void rebuildScopes();
    void refreshPresetsList();
    void showPresetDetails(int index);
    void updateActivePresetFromLive();
    void applyPresetTreeItemChange(QTreeWidgetItem *item, int column);

    pxs::core::tree::Tree &m_tree;
    QUndoStack *m_undo;
    PresetHost *m_host;
    std::vector<pxs::core::tree::Preset> m_presets;

    std::function<void(int)> m_recall;
    QDoubleSpinBox *m_fade;
    QComboBox *m_scopeCombo;
    QPushButton *m_captureBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_duplicateBtn;
    QListWidget *m_presetList;

    QLineEdit *m_nameEdit;
    QPushButton *m_updateBtn;
    QPushButton *m_recallBtn;
    QLabel *m_statusLabel;
    QTreeWidget *m_paramsTree;
    QPushButton *m_checkAllBtn;
    QPushButton *m_uncheckAllBtn;

    bool m_updatingTree = false;
};

} // namespace pxs::presetview
