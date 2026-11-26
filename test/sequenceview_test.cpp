// The cue list on screen (pxs/SequenceView.h): its rows from a sequence, an edit as an undo step, GO from the view, a step on its way
#include <pxs/SequenceView.h>

#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QMimeData>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QUndoStack>

#include <cmath>

#include <cstdio>

namespace {

int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

class Host : public SequencerHost, public pxs::sequenceview::SequenceViewHost
{
public:
    std::vector<std::pair<quint64, AnimAction>> controlled;
    BeatClock clock;
    void controlAnimation(quint64 id, AnimAction a, double, AnimLoop, int) override { controlled.emplace_back(id, a); }
    double animationPlayTime(quint64) const override { return 0; }
    double animationRemaining(quint64) const override { return 0; }
    const BeatClock &beatClock() const override { return clock; }
    bool fadesManual() const override { return true; }
    std::vector<std::pair<quint64, QString>> timelines() const override { return {{5, QStringLiteral("Intro")}, {6, QStringLiteral("Song")}}; }
    quint64 currentTimeline() const override { return 6; }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Host host;
    pxs::Annotated<QRecursiveMutex> mutex;
    Sequencer seq(host, mutex);
    Sequencer::Sequence s;
    Sequencer::Step a;
    a.description = QStringLiteral("Intro");
    a.timeline = 5;
    a.preWait = 1;
    s.steps = {a};
    seq.add(s);
    seq.setCurrent(0);
    QUndoStack undo;
    pxs::sequenceview::SequenceView view(seq, &host, &undo);
    view.resize(900, 400);
    view.show();
    auto *table = view.findChild<QTableWidget *>();
    check(table && table->rowCount() == 1, "a row for the step");
    auto *timeline = qobject_cast<QComboBox *>(table->cellWidget(0, 3));
    check(timeline && timeline->currentText() == QLatin1String("Intro") && timeline->count() == 3, "its timeline among the host's");
    check(table->item(0, 1)->data(Qt::EditRole).toDouble() == 1.0 && pxs::sequenceview::SequenceView::seconds(1) == QLatin1String("1.0") &&
              pxs::sequenceview::SequenceView::seconds(65) == QLatin1String("1:05.0"),
          "its pre-wait, as the list shows times");

    auto *header = table->horizontalHeader();
    const int oldStepWidth = header->sectionSize(0);
    const int oldWaitWidth = header->sectionSize(1);
    const QPointF handle(header->sectionViewportPosition(0) + oldStepWidth - 1, header->height() / 2);
    const auto mouse = [&](QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, header->viewport()->mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &event);
    };
    mouse(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, handle + QPointF(22, 0), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, handle + QPointF(22, 0), Qt::LeftButton, Qt::NoButton);
    check(header->sectionSize(0) == oldStepWidth + 22 && header->sectionSize(1) == oldWaitWidth,
          "dragging the separator resizes only its column");

    // A timeline dragged below the last cue is inserted as an undoable step.
    QMimeData timelineDrag;
    timelineDrag.setData("application/x-pxs-timeline", QByteArray("6"));
    const QPoint dropAt(10, table->rowHeight(0) + 10);
    QDragEnterEvent enter(dropAt, Qt::CopyAction, &timelineDrag, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(table->viewport(), &enter);
    check(enter.isAccepted(), "timeline drag accepted");
    QDropEvent drop(QPointF(dropAt), Qt::CopyAction, &timelineDrag, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(table->viewport(), &drop);
    check(seq.sequence(0).steps.size() == 2 && seq.sequence(0).steps[1].timeline == 6 && undo.count() == 1, "a step made, on the current timeline, undoable");
    check(table->rowCount() == 2, "its row");
    undo.undo();
    check(seq.sequence(0).steps.size() == 1 && table->rowCount() == 1, "undone");

    // GO: the step's pre-wait fills its cell, then its timeline plays
    QPushButton *go = view.findChild<QPushButton *>(QStringLiteral("go"));
    check(go && go->isEnabled(), "GO enabled");
    if (go) go->click();
    seq.advance(0.5);
    QApplication::processEvents();
    QTimer::singleShot(80, &app, &QApplication::quit);
    app.exec(); // the view polls the steps on their way
    const QVariant half = table->item(0, 1)->data(Qt::UserRole + 1);
    check(half.isValid() && std::abs(half.toDouble() - 0.5) < 0.05, "half its pre-wait gone by, shown");
    seq.advance(0.6);
    check(!host.controlled.empty() && host.controlled.back().first == 5 && host.controlled.back().second == AnimAction::Play, "then its timeline plays");

    // Load as Next
    seq.stopAll();
    Sequencer::Step b;
    b.description = QStringLiteral("Outro");
    b.timeline = 6;
    s = seq.sequence(0);
    s.steps.push_back(b);
    seq.setSequence(0, s);
    view.rebuild();
    check(seq.position() == 0 && seq.next() == 1, "next is 1 after playing 0");
    check(seq.loadAsNext(0, 0), "loadAsNext succeeds");
    check(seq.next() == 0, "loadAsNext sets next to 0");

    if (failures) std::fprintf(stderr, "sequenceview: %d failure(s)\n", failures);
    else std::puts("sequenceview: ok");
    return failures ? 1 : 0;
}
