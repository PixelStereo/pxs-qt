// The cue list on screen (pxs/SequenceView.h): its rows from a sequence, an edit as an undo step, GO from the view, a step on its way
#include <pxs/SequenceView.h>

#include <QApplication>
#include <QComboBox>
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

    // + Step: a step on the host's current timeline, an undo step
    QPushButton *add = nullptr;
    for (auto *b : view.findChildren<QPushButton *>())
        if (b->text() == QLatin1String("+ Step")) add = b;
    check(add != nullptr, "+ Step");
    if (add) add->click();
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

    if (failures) std::fprintf(stderr, "sequenceview: %d failure(s)\n", failures);
    else std::puts("sequenceview: ok");
    return failures ? 1 : 0;
}
