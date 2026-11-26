#include "pxs/SequenceView.h"
#include <pxs/PresetView.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMimeData>
#include <QTreeWidget>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>
#include <map>

namespace pxs::sequenceview {

namespace {

enum Column { TextColumn = 0, PreWaitColumn, ActionColumn, TimelineColumn, ValueColumn, PostWaitColumn, NextColumn, ColumnCount };

// NyxMap's colours: the step GO plays, the one played, and the times of a step on its way
const QColor kStandby(232, 76, 61);
const QColor kPlayed(76, 217, 100);
const QColor kPreWait(224, 180, 58);
const QColor kAction(76, 217, 100);
const QColor kPostWait(86, 156, 230);

// What a step does to a timeline, in the order of AnimAction
const QStringList kActions = {QStringLiteral("Play"), QStringLiteral("Pause"),     QStringLiteral("Stop"), QStringLiteral("Rewind"),
                              QStringLiteral("Seek"), QStringLiteral("Loop mode"), QStringLiteral("Speed")};

constexpr int kFractionRole = Qt::UserRole + 1; // how far a time of a step on its way is: 0..1 (absent: not on its way)

// Where a step on its way is in each of its times (NyxMap's): -1 not there yet, 0..1 on the way, 1 over
struct Phases {
    double pre = -1, action = -1, post = -1;
};
Phases phasesOf(const Sequencer::Run &r)
{
    Phases ph;
    auto frac = [](double e, double len) { return len > 0 ? std::clamp(e / len, 0.0, 1.0) : (e >= 0 ? 1.0 : 0.0); };
    ph.pre = r.preWait > 0 ? frac(r.elapsed, r.preWait) : -1;
    if (r.fired) ph.action = std::isfinite(r.duration) ? frac(r.elapsed - r.preWait, r.duration) : 0.5;
    const double at = r.continueAt();
    if (at >= 0 && r.fired && r.elapsed >= at - r.postWait) ph.post = frac(r.elapsed - (at - r.postWait), r.postWait);
    return ph;
}

// The times as numbers to edit, and as bars that fill while a step is on its way; Then as a badge, GO or AUTO
class StepDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QString displayText(const QVariant &value, const QLocale &) const override
    {
        return value.typeId() == QMetaType::Double ? SequenceView::seconds(value.toDouble()) : value.toString();
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        if (idx.column() != PreWaitColumn && idx.column() != PostWaitColumn && idx.column() != ValueColumn)
            return QStyledItemDelegate::createEditor(parent, opt, idx);
        auto *box = new QDoubleSpinBox(parent);
        box->setRange(0, 3600);
        box->setDecimals(2);
        box->setSingleStep(0.1);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return box;
    }
    void setEditorData(QWidget *e, const QModelIndex &idx) const override
    {
        if (auto *box = qobject_cast<QDoubleSpinBox *>(e)) box->setValue(idx.data(Qt::EditRole).toDouble());
        else QStyledItemDelegate::setEditorData(e, idx);
    }
    void setModelData(QWidget *e, QAbstractItemModel *m, const QModelIndex &idx) const override
    {
        if (auto *box = qobject_cast<QDoubleSpinBox *>(e)) m->setData(idx, box->value(), Qt::EditRole);
        else QStyledItemDelegate::setModelData(e, m, idx);
    }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        if (idx.column() == NextColumn) { // the badge
            const bool autoFollow = idx.data(Qt::UserRole).toBool();
            const QColor ink = autoFollow ? QColor("#b7a1d9") : QColor("#d5bd82");
            QColor fill = ink, edge = ink;
            fill.setAlphaF(.12);
            edge.setAlphaF(.6);
            const QRectF r = QRectF(opt.rect).adjusted(8, 6, -8, -6);
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(QPen(edge, 1));
            p->setBrush(fill);
            p->drawRoundedRect(r, 3, 3);
            QFont f = opt.font;
            f.setBold(true);
            p->setFont(f);
            p->setPen(ink);
            p->drawText(r, Qt::AlignCenter, autoFollow ? QStringLiteral("AUTO") : QStringLiteral("GO"));
            p->restore();
            return;
        }
        const QVariant fraction = idx.data(kFractionRole);
        if (fraction.isValid()) { // a time on its way: the part gone by, in its colour
            QColor c = idx.column() == PreWaitColumn ? kPreWait : idx.column() == PostWaitColumn ? kPostWait : kAction;
            c.setAlphaF(.32);
            QRect r = opt.rect.adjusted(1, 1, -1, -1);
            r.setWidth(int(r.width() * std::clamp(fraction.toDouble(), 0.0, 1.0)));
            p->fillRect(r, c);
        }
        QStyledItemDelegate::paint(p, opt, idx);
    }
};

QTableWidgetItem *timeItem(double v, bool editable = true)
{
    auto *item = new QTableWidgetItem;
    item->setData(Qt::EditRole, v);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

QString SequenceView::seconds(double t)
{
    if (!std::isfinite(t)) return QStringLiteral("∞");
    t = std::max(0.0, t);
    if (t < 60) return QString::number(t, 'f', 1);
    const int m = int(t / 60);
    return QStringLiteral("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QLatin1Char('0'));
}

SequenceView::SequenceView(Sequencer &sequencer, SequenceViewHost *host, QUndoStack *undo, QWidget *parent)
    : QWidget(parent), m_seq(sequencer), m_host(host), m_undo(undo)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 8, 0, 0);
    outer->setSpacing(10);

    // The transport: GO, the step on standby, GO BACK, Stop, Stop all
    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    m_go = new QPushButton(QStringLiteral("GO"));
    m_go->setObjectName(QStringLiteral("go"));
    m_go->setToolTip(QStringLiteral("Plays the step on standby (space bar)"));
    m_go->setMinimumSize(140, 64);
    m_go->setStyleSheet(QStringLiteral("QPushButton#go { background: #2fa45a; color: #ffffff; font-size: 26px; font-weight: 800; letter-spacing: 2px;"
                                       " border: none; border-radius: 10px; }"
                                       "QPushButton#go:hover { background: #38bd69; }"
                                       "QPushButton#go:pressed { background: #23864a; }"
                                       "QPushButton#go:disabled { background: #2b2e34; color: #6b6f78; }"));
    top->addWidget(m_go);
    auto *standby = new QVBoxLayout;
    auto *standbyTitle = new QLabel(QStringLiteral("STANDBY"));
    standbyTitle->setStyleSheet(QStringLiteral("color: #8a8e97; font-size: 10px; letter-spacing: 1.5px;"));
    m_next = new QLabel;
    m_next->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 600;"));
    m_state = new QLabel;
    m_state->setObjectName(QStringLiteral("caption"));
    standby->addWidget(standbyTitle);
    standby->addWidget(m_next);
    standby->addWidget(m_state);
    top->addLayout(standby, 1);
    auto *back = new QPushButton(QStringLiteral("◀  GO BACK"));
    back->setToolTip(QStringLiteral("The step before, at once"));
    auto *stop = new QPushButton(QStringLiteral("Stop"));
    stop->setToolTip(QStringLiteral("The waits and follows on their way stopped; what plays goes on"));
    auto *stopAll = new QPushButton(QStringLiteral("■  Stop all"));
    stopAll->setToolTip(QStringLiteral("Every wait and follow, and every timeline, stopped (Escape)"));
    stopAll->setStyleSheet(QStringLiteral("QPushButton { color: #ff6b6b; }"));
    top->addWidget(back);
    top->addWidget(stop);
    top->addWidget(stopAll);
    outer->addLayout(top);

    m_table = new QTableWidget(0, ColumnCount);
    m_table->setItemDelegate(new StepDelegate(m_table));
    m_table->setHorizontalHeaderLabels({QStringLiteral("Step"), QStringLiteral("Pre-wait"), QStringLiteral("Does"), QStringLiteral("Timeline"),
                                        QStringLiteral("Value"), QStringLiteral("Post-wait"), QStringLiteral("Then")});
    m_table->horizontalHeader()->setSectionResizeMode(TextColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(TimelineColumn, QHeaderView::Stretch);
    for (const auto &[c, w] : std::initializer_list<std::pair<int, int>>{
             {PreWaitColumn, 84}, {ActionColumn, 110}, {ValueColumn, 84}, {PostWaitColumn, 84}, {NextColumn, 74}})
        m_table->setColumnWidth(c, w);
    m_table->horizontalHeaderItem(NextColumn)->setToolTip(QStringLiteral("GO: the next step waits for GO. AUTO: it follows once this one's "
                                                                         "action and post-wait are over. Double-click to flip"));
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->verticalHeader()->setToolTip(QStringLiteral("Double-click: play this step. Right-click: Load as Next"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->viewport()->setAcceptDrops(true);
    m_table->setDragDropMode(QAbstractItemView::DragDrop);
    m_table->setDropIndicatorShown(false);
    m_table->viewport()->installEventFilter(this);

    m_dropLine = new QFrame(m_table->viewport());
    m_dropLine->setObjectName(QStringLiteral("stepDropLine"));
    m_dropLine->setStyleSheet(QStringLiteral("background:#4cd964;"));
    m_dropLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropLine->hide();

    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int row = m_table->rowAt(pos.y());
        if (row < 0) return;
        QMenu menu(this);
        menu.addAction(QStringLiteral("Inspect Step…"), this, [this, row] { inspectStep(row); });
        auto *load = menu.addAction(QStringLiteral("Load as Next"), this, [this, row] {
            m_seq.loadAsNext(std::max(0, m_seq.current()), row);
            refresh();
        });
        load->setToolTip(QStringLiteral("GO plays this step next"));
        menu.addAction(QStringLiteral("Play this step"), this, [this, row] { m_seq.goTo(row); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Duplicate step"), this, [this, row] {
            Sequencer::Sequence s = sequence();
            if (row < int(s.steps.size())) {
                Sequencer::Step copy = s.steps[size_t(row)];
                copy.runtimeId = 0;
                s.steps.insert(s.steps.begin() + row + 1, copy);
                apply(s, QStringLiteral("Duplicate Step"));
                m_table->selectRow(row + 1);
            }
        });
        menu.addAction(QStringLiteral("Delete step"), this, [this, row] {
            Sequencer::Sequence s = sequence();
            if (row < int(s.steps.size())) {
                s.steps.erase(s.steps.begin() + row);
                apply(s, QStringLiteral("Remove Step"));
            }
        });
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });

    outer->addWidget(m_table, 1);

    connect(m_go, &QPushButton::clicked, this, [this] { m_seq.go(); });
    connect(back, &QPushButton::clicked, this, [this] { m_seq.back(); });
    connect(stop, &QPushButton::clicked, this, [this] { m_seq.stop(); });
    connect(stopAll, &QPushButton::clicked, this, [this] {
        if (m_host) m_host->stopAll(m_seq);
        else m_seq.stopAll();
    });
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setContext(Qt::WidgetWithChildrenShortcut);
    connect(space, &QShortcut::activated, this, [this] {
        if (!typing()) m_seq.go();
    });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this, stopAll] {
        if (!typing()) stopAll->click();
    });
    auto *remove = new QShortcut(QKeySequence::Delete, m_table);
    connect(remove, &QShortcut::activated, this, [this] {
        if (typing()) return;
        const int row = m_table->currentRow();
        Sequencer::Sequence s = sequence();
        if (row < 0 || row >= int(s.steps.size())) return;
        s.steps.erase(s.steps.begin() + row);
        apply(s, QStringLiteral("Remove Step"));
    });
    connect(m_table->verticalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int row) { m_seq.goTo(row); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != NextColumn) return;
        Sequencer::Sequence s = sequence();
        if (row >= int(s.steps.size())) return;
        auto &next = s.steps[size_t(row)].next;
        next = next == Sequencer::StepContinue::AutoFollow ? Sequencer::StepContinue::Wait : Sequencer::StepContinue::AutoFollow;
        apply(s, next == Sequencer::StepContinue::AutoFollow ? QStringLiteral("Auto Follow") : QStringLiteral("Wait for GO"));
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this] {
        if (!m_filling) QTimer::singleShot(0, this, [this] { store(); }); // not from inside the item's own change
    });

    connect(&m_seq, &Sequencer::changed, this, [this] { rebuild(); });
    connect(&m_seq, &Sequencer::positionChanged, this, [this] { refresh(); });
    auto *poll = new QTimer(this); // the steps on their way
    poll->start(50);
    connect(poll, &QTimer::timeout, this, [this] { refresh(); });
    rebuild();
}

Sequencer::Sequence SequenceView::sequence() const
{
    return m_seq.sequence(std::max(0, m_seq.current()));
}

bool SequenceView::typing() const
{
    const QWidget *focus = QApplication::focusWidget();
    return qobject_cast<const QLineEdit *>(focus) || qobject_cast<const QAbstractSpinBox *>(focus) || qobject_cast<const QComboBox *>(focus);
}

void SequenceView::rebuild()
{
    if (typing() && m_table->rowCount() == int(sequence().steps.size())) { // a cell being edited stays: only what is shown moves
        refresh();
        return;
    }
    m_filling = true;
    const Sequencer::Sequence s = sequence();
    const auto timelines = m_host ? m_host->timelines() : std::vector<std::pair<quint64, QString>>{};
    m_table->setRowCount(int(s.steps.size()));
    for (int row = 0; row < int(s.steps.size()); ++row) {
        const Sequencer::Step &st = s.steps[size_t(row)];
        const bool group = !st.actions.empty();
        m_table->setItem(row, TextColumn, new QTableWidgetItem(st.description));
        m_table->setItem(row, PreWaitColumn, timeItem(st.preWait));
        m_table->setItem(row, PostWaitColumn, timeItem(st.postWait));
        const bool valued = !group && (st.action == AnimAction::Seek || st.action == AnimAction::Speed);
        auto *value = timeItem(st.action == AnimAction::Speed ? st.speed : st.seekTime, valued);
        if (!valued) value->setText(QString());
        m_table->setItem(row, ValueColumn, value);
        auto *next = new QTableWidgetItem;
        next->setData(Qt::UserRole, st.next == Sequencer::StepContinue::AutoFollow);
        next->setFlags(next->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, NextColumn, next);

        auto *action = new QComboBox;
        auto *timeline = new QComboBox;
        if (st.preset && !group) {
            action->addItem(QStringLiteral("Recall"));
            action->setEnabled(false);
            const auto *preset = m_host ? m_host->preset(st.preset) : nullptr;
            timeline->addItem(preset ? QString::fromStdString(preset->name) : QStringLiteral("Missing preset"));
            timeline->setEnabled(false);
        } else if (group) { // several actions (a group): shown, not edited here
            action->addItem(QStringLiteral("%1 actions").arg(st.actions.size()));
            action->setEnabled(false);
            timeline->addItem(QStringLiteral("—"));
            timeline->setEnabled(false);
        } else {
            action->addItems(kActions);
            action->setCurrentIndex(std::clamp(int(st.action), 0, int(kActions.size()) - 1));
            timeline->addItem(QStringLiteral("—"), QVariant::fromValue<qulonglong>(0));
            for (const auto &[id, name] : timelines) timeline->addItem(name, QVariant::fromValue<qulonglong>(id));
            timeline->setCurrentIndex(std::max(0, timeline->findData(QVariant::fromValue<qulonglong>(st.timeline))));
            connect(action, &QComboBox::currentIndexChanged, this, [this] { QTimer::singleShot(0, this, [this] { store(); }); });
            connect(timeline, &QComboBox::currentIndexChanged, this, [this] { QTimer::singleShot(0, this, [this] { store(); }); });
        }
        m_table->setCellWidget(row, ActionColumn, action);
        m_table->setCellWidget(row, TimelineColumn, timeline);
    }
    m_filling = false;
    refresh();
}

void SequenceView::refresh()
{
    const Sequencer::Sequence s = sequence();
    const int next = m_seq.next(), played = m_seq.position();
    std::map<int, Phases> phases;
    for (const Sequencer::Run &r : m_seq.runs()) phases[r.step] = phasesOf(r);
    const QSignalBlocker block(m_table);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *head = m_table->verticalHeaderItem(row);
        if (!head) {
            head = new QTableWidgetItem;
            m_table->setVerticalHeaderItem(row, head);
        }
        head->setText(row == next ? QStringLiteral("▶ %1").arg(row + 1) : QString::number(row + 1));
        head->setForeground(row == next ? kStandby : row == played ? kPlayed : QColor(0x8a, 0x8e, 0x97));
        if (QTableWidgetItem *text = m_table->item(row, TextColumn)) {
            QFont f = text->font();
            f.setBold(row == next);
            text->setFont(f);
            text->setForeground(row == next ? kStandby : row == played ? kPlayed : QColor(0xe6, 0xe8, 0xec));
        }
        const auto ph = phases.find(row);
        const auto mark = [&](int column, double fraction) {
            if (QTableWidgetItem *item = m_table->item(row, column))
                item->setData(kFractionRole, ph != phases.end() && fraction >= 0 ? QVariant(fraction) : QVariant());
        };
        mark(PreWaitColumn, ph != phases.end() ? ph->second.pre : -1);
        mark(TextColumn, ph != phases.end() ? ph->second.action : -1);
        mark(PostWaitColumn, ph != phases.end() ? ph->second.post : -1);
    }
    m_go->setEnabled(next >= 0);
    m_next->setText(next >= 0 && next < int(s.steps.size()) ? QStringLiteral("%1   %2").arg(next + 1).arg(s.steps[size_t(next)].description)
                                                            : QStringLiteral("— end of the sequence —"));
    QString state = QStringLiteral("%1 step(s)").arg(s.steps.size());
    if (!phases.empty()) {
        const Sequencer::Run r = m_seq.runs().back();
        const char *phase = r.elapsed < r.preWait ? "pre-wait" : !std::isfinite(r.duration) || r.elapsed < r.preWait + r.duration ? "action" : "post-wait";
        state = QStringLiteral("step %1 · %2 · %3").arg(r.step + 1).arg(QString::fromLatin1(phase), seconds(r.elapsed));
    }
    m_state->setText(state);
}

void SequenceView::store()
{
    if (m_filling) return;
    Sequencer::Sequence s = sequence();
    for (int row = 0; row < m_table->rowCount() && row < int(s.steps.size()); ++row) {
        Sequencer::Step &st = s.steps[size_t(row)];
        if (const QTableWidgetItem *text = m_table->item(row, TextColumn)) st.description = text->text();
        if (const QTableWidgetItem *pre = m_table->item(row, PreWaitColumn)) st.preWait = pre->data(Qt::EditRole).toDouble();
        if (const QTableWidgetItem *post = m_table->item(row, PostWaitColumn)) st.postWait = post->data(Qt::EditRole).toDouble();
        if (!st.actions.empty() || st.preset) continue; // a group: its actions as they are
        if (auto *action = qobject_cast<QComboBox *>(m_table->cellWidget(row, ActionColumn))) st.action = AnimAction(action->currentIndex());
        if (auto *timeline = qobject_cast<QComboBox *>(m_table->cellWidget(row, TimelineColumn))) st.timeline = timeline->currentData().toULongLong();
        if (const QTableWidgetItem *value = m_table->item(row, ValueColumn); value && !value->text().isEmpty()) {
            if (st.action == AnimAction::Speed) st.speed = value->data(Qt::EditRole).toDouble();
            if (st.action == AnimAction::Seek) st.seekTime = value->data(Qt::EditRole).toDouble();
        }
    }
    apply(s, QStringLiteral("Edit Step"));
}

void SequenceView::apply(const Sequencer::Sequence &after, const QString &text)
{
    const Sequencer::Sequence before = sequence();
    QJsonArray a, b;
    for (const auto &st : before.steps) a.append(Sequencer::stepToJson(st));
    for (const auto &st : after.steps) b.append(Sequencer::stepToJson(st));
    if (a == b) return; // nothing changed: no step
    const int index = std::max(0, m_seq.current());
    if (m_undo) m_undo->push(new SetSequence(m_seq, index, before, after, text));
    else m_seq.setSequence(index, after);
}

SetSequence::SetSequence(Sequencer &seq, int index, const Sequencer::Sequence &before, const Sequencer::Sequence &after, const QString &text)
    : QUndoCommand(text), m_seq(seq), m_index(index), m_before(before), m_after(after)
{
}
void SetSequence::redo() { m_seq.setSequence(m_index, m_after); }
void SetSequence::undo() { m_seq.setSequence(m_index, m_before); }

void SequenceView::inspectStep(int row)
{
    auto edited = sequence();
    if (row < 0 || row >= int(edited.steps.size())) return;
    auto &step = edited.steps[size_t(row)];
    const auto *source = step.preset && m_host ? m_host->preset(step.preset) : nullptr;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Step · %1").arg(step.description));
    auto *layout = new QVBoxLayout(&dialog);
    if (source) {
        const auto preset = *source;
        auto *fade = new QLineEdit(step.fadeOverride < 0 ? QString() : QString::number(step.fadeOverride));
        fade->setPlaceholderText(QStringLiteral("%1 s · inherited").arg(preset.fade));
        layout->addWidget(new QLabel(QStringLiteral("Default fade override (s)")));
        layout->addWidget(fade);
        auto *tree = new QTreeWidget;
        tree->setHeaderLabels({QStringLiteral("Parameter"), QStringLiteral("Value"), QStringLiteral("Time (s)"), QStringLiteral("Delay (s)")});
        auto *duration = new QLabel;
        const auto update = [&] {
            bool ok = false;
            const auto seconds = fade->text().toDouble(&ok);
            step.fadeOverride = ok && seconds >= 0 && seconds <= 3600 ? seconds : -1;
            duration->setText(QStringLiteral("Effective duration: %1 s").arg(preset.resolved(step.presetOverrides, step.fadeOverride).duration()));
        };
        const auto populate = [&] {
            tree->clear();
            for (const auto &[path, value] : preset.values) {
                auto *item = new QTreeWidgetItem(tree, {QString::fromStdString(path)});
                pxs::presetview::addParameterEditors(*tree, *item, path, preset, step.presetOverrides, step.fadeOverride, true, update);
            }
        };
        populate();
        QObject::connect(fade, &QLineEdit::editingFinished, &dialog, [&] { update(); populate(); });
        layout->addWidget(tree);
        layout->addWidget(duration);
        update();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(700, 360);
        if (dialog.exec() == QDialog::Accepted) apply(edited, QStringLiteral("Edit preset overrides"));
        return;
    }
    layout->addWidget(new QLabel(QStringLiteral("Pre-wait: %1 s · Post-wait: %2 s").arg(step.preWait).arg(step.postWait)));
    dialog.exec();
}

bool SequenceView::eventFilter(QObject *o, QEvent *e)
{
    if (m_table && o == m_table->viewport()) {
        if (e->type() == QEvent::DragLeave) {
            if (m_dropLine) m_dropLine->hide();
        } else if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
            auto *d = static_cast<QDropEvent *>(e);
            if (d->source() == m_table || d->mimeData()->hasFormat("application/x-pxs-preset") || d->mimeData()->hasFormat("application/x-pxs-timeline")) {
                const int y = d->position().toPoint().y();
                const int over = m_table->rowAt(y);
                int lineY = 0;
                if (over < 0) {
                    const int last = m_table->rowCount() - 1;
                    lineY = last >= 0 ? m_table->rowViewportPosition(last) + m_table->rowHeight(last) : 0;
                } else {
                    const int rowTop = m_table->rowViewportPosition(over);
                    const int rowHeight = m_table->rowHeight(over);
                    lineY = y < rowTop + rowHeight / 2 ? rowTop : rowTop + rowHeight;
                }
                if (m_dropLine) {
                    m_dropLine->setGeometry(0, lineY - 1, m_table->viewport()->width(), 2);
                    m_dropLine->show();
                    m_dropLine->raise();
                }
                d->setDropAction(Qt::CopyAction);
                d->accept();
                return true;
            }
        } else if (e->type() == QEvent::Drop) {
            if (m_dropLine) m_dropLine->hide();
            auto *d = static_cast<QDropEvent *>(e);
            if (d->source() == m_table || d->mimeData()->hasFormat("application/x-pxs-preset") || d->mimeData()->hasFormat("application/x-pxs-timeline")) {
                const int y = d->position().toPoint().y();
                const int over = m_table->rowAt(y);
                int target = 0;
                if (over < 0) {
                    target = m_table->rowCount();
                } else {
                    const int rowTop = m_table->rowViewportPosition(over);
                    const int rowHeight = m_table->rowHeight(over);
                    target = y < rowTop + rowHeight / 2 ? over : over + 1;
                }
                if (d->source() != m_table) {
                    Sequencer::Step step;
                    const auto *mime = d->mimeData();
                    step.preset = mime->data("application/x-pxs-preset").toULongLong();
                    step.timeline = mime->data("application/x-pxs-timeline").toULongLong();
                    if (step.preset && m_host) {
                        if (const auto *preset = m_host->preset(step.preset)) step.description = QString::fromStdString(preset->name);
                        else return true;
                    } else if (step.timeline && m_host) {
                        for (const auto &[id, name] : m_host->timelines())
                            if (id == step.timeline) step.description = name;
                        if (step.description.isEmpty()) return true;
                    } else return true;
                    auto after = sequence();
                    after.steps.insert(after.steps.begin() + target, step);
                    apply(after, QStringLiteral("Insert Step"));
                    m_table->selectRow(target);
                    d->acceptProposedAction();
                    return true;
                }
                const int fromRow = m_table->currentRow();
                if (fromRow >= 0 && fromRow < m_table->rowCount() && target != fromRow && target != fromRow + 1) {
                    Sequencer::Sequence s = sequence();
                    if (fromRow < int(s.steps.size())) {
                        Sequencer::Step st = s.steps[size_t(fromRow)];
                        s.steps.erase(s.steps.begin() + fromRow);
                        if (target > fromRow) --target;
                        target = std::clamp(target, 0, int(s.steps.size()));
                        s.steps.insert(s.steps.begin() + target, st);
                        apply(s, QStringLiteral("Move Step"));
                        m_table->selectRow(target);
                    }
                }
                d->setDropAction(Qt::CopyAction);
                d->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace pxs::sequenceview
