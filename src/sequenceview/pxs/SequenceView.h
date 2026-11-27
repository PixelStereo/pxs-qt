#pragma once
// A sequence (pxs/Sequencer.h) as a cue list on screen, for an application whose steps drive timelines: GO (big, with the step on
// standby and where the steps on their way are), GO BACK, Stop, Stop all; and one row per step: its text, its pre-wait, what it does to
// a timeline and the value it takes (a seek's time, a speed), its post-wait, and then: GO (the next step waits for GO) or AUTO (it
// follows), a double-click flips it. While a step is on its way, its times fill in their colours (NyxMap's): the pre-wait yellow, the
// action green, the post-wait blue. The step GO plays is framed in red, the one played is green. Edits are undo steps of the
// application's stack. A step made of several actions (a group) is shown, not edited here.
#include <pxs/Sequencer.h>

#include <QUndoCommand>
#include <QWidget>

#include <utility>
#include <vector>

class QFrame;
class QLabel;
class QPushButton;
class QTableWidget;
class QUndoStack;
class QListWidget;

namespace pxs::sequenceview {

// What the view asks of the application
class SequenceViewHost
{
public:
    virtual ~SequenceViewHost() = default;
    // The timelines a step can drive (id, name), in order
    virtual std::vector<std::pair<quint64, QString>> timelines() const = 0;
    // The timeline a new step drives (the one the application shows)
    virtual quint64 currentTimeline() const { return 0; }
    virtual const pxs::core::tree::Preset *preset(quint64) const { return nullptr; }
    // Stop all: every wait and follow, and what the application stops with them (its timelines)
    virtual void stopAll(Sequencer &sequencer) { sequencer.stopAll(); }
};

// A sequence as it was edited: an undo step (the view pushes them; an application may too)
class SetSequence : public QUndoCommand
{
public:
    SetSequence(Sequencer &seq, int index, const Sequencer::Sequence &before, const Sequencer::Sequence &after, const QString &text);
    void redo() override;
    void undo() override;

private:
    Sequencer &m_seq;
    int m_index;
    Sequencer::Sequence m_before, m_after;
};

class SequenceView : public QWidget
{
public:
    SequenceView(Sequencer &sequencer, SequenceViewHost *host, QUndoStack *undo, QWidget *parent = nullptr);

    void rebuild(); // Refresh the steps and their available targets.
    void inspectStep(int row);
    QWidget *stepInspector(int row, QWidget *parent);
    void setInspectorSink(std::function<void(QWidget *)> sink) { m_inspectorSink = std::move(sink); }

    // The time of a step as the list shows it: 2.5 under a minute, 1:05.0 beyond, ∞ endless
    static QString seconds(double t);

private:
    Sequencer::Sequence sequence() const;
    void refresh();      // the step played, the one on standby, the times of the steps on their way
    void scheduleInspectorUpdate();
    void store();        // the rows, as edited, to the sequence (an undo step)
    void apply(const Sequencer::Sequence &after, const QString &text);
    bool typing() const; // a step being edited: Space and Escape are not GO and Stop all

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    Sequencer &m_seq;
    SequenceViewHost *m_host;
    QUndoStack *m_undo;
    QTableWidget *m_table;
    QFrame *m_dropLine = nullptr;
    QPushButton *m_go;
    QLabel *m_next, *m_state;
    std::function<void(QWidget *)> m_inspectorSink;
    bool m_filling = false;
    bool m_inspectorUpdatePending = false;
};

} // namespace pxs::sequenceview
