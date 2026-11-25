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

class QLabel;
class QPushButton;
class QTableWidget;
class QUndoStack;

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

    void rebuild(); // the rows again: the steps, and the timelines they can drive (call it when those change)

    // The time of a step as the list shows it: 2.5 under a minute, 1:05.0 beyond, ∞ endless
    static QString seconds(double t);

private:
    Sequencer::Sequence sequence() const;
    void refresh();      // the step played, the one on standby, the times of the steps on their way
    void store();        // the rows, as edited, to the sequence (an undo step)
    void apply(const Sequencer::Sequence &after, const QString &text);
    void move(int by);   // the selected step up or down
    bool typing() const; // a step being edited: Space and Escape are not GO and Stop all

    Sequencer &m_seq;
    SequenceViewHost *m_host;
    QUndoStack *m_undo;
    QTableWidget *m_table;
    QPushButton *m_go;
    QLabel *m_next, *m_state;
    bool m_filling = false;
};

} // namespace pxs::sequenceview
