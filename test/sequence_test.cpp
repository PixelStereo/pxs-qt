// Sequences (pxs/Sequencer.h) with a host of the test's own: the JSON of every kind of step as NyxMap's projects hold it, and what GO
// does with its waits, follows, groups, back, stop and loop, moved by hand (SequencerHost::fadesManual)
#include <pxs/Sequencer.h>

#include <QCoreApplication>
#include <QJsonDocument>

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

// Snapshots 10 and 11, preset 20, timelines 5 and 6 (each plays 2 s)
class Host : public SequencerHost
{
public:
    std::vector<quint64> recalled, presets;
    std::vector<std::pair<quint64, AnimAction>> controlled;
    std::map<quint64, double> remaining;
    BeatClock clock;

    int indexOfSnapshot(quint64 id) const override { return id == 10 ? 0 : id == 11 ? 1 : -1; }
    void recallSnapshot(int index, double) override { recalled.push_back(index == 0 ? 10 : 11); }
    bool recallSnapshotById(quint64 id, double) override
    {
        if (indexOfSnapshot(id) < 0) return false;
        recalled.push_back(id);
        return true;
    }
    int indexOfPreset(quint64 id) const override { return id == 20 ? 0 : -1; }
    void recallPreset(int, double) override { presets.push_back(20); }
    void controlAnimation(quint64 id, AnimAction action, double, AnimLoop, int) override
    {
        controlled.emplace_back(id, action);
        if (action == AnimAction::Play) remaining[id] = 2;
        if (action == AnimAction::Stop) remaining[id] = 0;
    }
    double animationPlayTime(quint64 id) const override { return id == 5 || id == 6 ? 2 : 0; }
    double animationRemaining(quint64 id) const override
    {
        const auto it = remaining.find(id);
        return it == remaining.end() ? 0 : it->second;
    }
    const BeatClock &beatClock() const override { return clock; }
    bool fadesManual() const override { return true; } // only advance() moves the waits
    void pass(Sequencer &seq, double seconds)          // the timelines and the waits move on together
    {
        for (auto &[id, left] : remaining) left = std::max(0.0, left - seconds);
        seq.advance(seconds);
    }
};

bool playedAt(const Host &h, quint64 timeline, size_t at)
{
    return h.controlled.size() > at && h.controlled[at].first == timeline && h.controlled[at].second == AnimAction::Play;
}

// Every kind of step and of sequence setting, as the JSON holds them
const char *kSequences = R"([
 {"id":"1","name":"Show","loop":false,"beats":false,"tracking":true,"edit_mode":false,
  "closed_folders":["9"],"timecode":{"layer":"3","param":"playhead"},
  "steps":[
   {"snapshot":"10","description":"House","fade_override":2.5,"pre_wait":1,"post_wait":0.5,"continue":"auto_follow"},
   {"preset":"20","quantize":4},
   {"timeline":"5","action":"play","at":12.5},
   {"timeline":"5","action":"seek","time":1.5},
   {"timeline":"6","action":"speed","speed":0.5},
   {"timeline":"6","action":"loop_mode","loop":"pingpong","repeat":3},
   {"snapshot":"0","folder":"9","closed":true,
    "actions":[{"timeline":"5","action":"play"},{"timeline":"6","action":"stop","pre_wait":0.25}]},
   {"sequence":"2"}
  ]},
 {"id":"2","name":"Second","loop":true,"beats":true,"tracking":false,"edit_mode":false,"steps":[{"snapshot":"11"}]}
])";

void json()
{
    Host host;
    pxs::Annotated<QRecursiveMutex> mutex;
    Sequencer seq(host, mutex);
    const QJsonArray in = QJsonDocument::fromJson(kSequences).array();
    seq.fromJson(in, 0);
    check(seq.count() == 2, "two sequences read");
    const QJsonArray out = seq.toJson();
    check(out == in, "every kind of step written back as it was read");
    if (out != in) std::fprintf(stderr, "%s\n", QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
    const Sequencer::Step s = Sequencer::stepFromJson(in[0].toObject()["steps"].toArray()[0].toObject());
    check(s.snapshot == 10 && s.preWait == 1 && s.postWait == 0.5 && s.next == Sequencer::StepContinue::AutoFollow && s.fadeOverride == 2.5,
          "a step's settings read");
}

Sequencer::Step snapshotStep(quint64 id, double preWait = 0, double postWait = 0, bool follow = false)
{
    Sequencer::Step st;
    st.snapshot = id;
    st.preWait = preWait;
    st.postWait = postWait;
    st.next = follow ? Sequencer::StepContinue::AutoFollow : Sequencer::StepContinue::Wait;
    return st;
}

Sequencer::Step timelineStep(quint64 id, AnimAction action, double preWait = 0)
{
    Sequencer::Step st;
    st.timeline = id;
    st.action = action;
    st.preWait = preWait;
    return st;
}

void go()
{
    Host host;
    pxs::Annotated<QRecursiveMutex> mutex;
    Sequencer seq(host, mutex);
    Sequencer::Sequence s;
    s.name = QStringLiteral("Show");
    s.steps = {snapshotStep(10, 0, 0.5, true), timelineStep(5, AnimAction::Play), snapshotStep(11, 1)};
    seq.add(s);
    seq.setCurrent(0);
    check(seq.next() == 0, "GO plays the first step");
    check(seq.go(), "GO");
    check(host.recalled == std::vector<quint64>{10}, "the first step's snapshot, at once");
    host.pass(seq, 0.4);
    check(host.controlled.empty(), "the follow waits its post-wait");
    host.pass(seq, 0.2);
    check(playedAt(host, 5, 0), "then the next step gets its GO: its timeline plays");
    check(seq.position() == 1 && seq.next() == 2, "the step played, the next one");
    check(seq.go(), "GO the third");
    host.pass(seq, 0.5);
    check(host.recalled.size() == 1, "its pre-wait is not over");
    seq.stop();
    host.pass(seq, 1);
    check(host.recalled.size() == 1, "stopped: its pre-wait goes nowhere");
    check(seq.back(), "GO BACK");
    check(seq.position() == 1, "back to the step before");
}

void groups()
{
    Host host;
    pxs::Annotated<QRecursiveMutex> mutex;
    Sequencer seq(host, mutex);
    Sequencer::Step group = snapshotStep(10);
    group.actions = {timelineStep(5, AnimAction::Play), timelineStep(6, AnimAction::Play, 0.5)};
    Sequencer::Sequence s;
    s.steps = {group};
    s.loop = true;
    seq.add(s);
    seq.setCurrent(0);
    seq.go();
    check(playedAt(host, 5, 0) && host.controlled.size() == 1, "a group: its first action at once");
    host.pass(seq, 0.6);
    check(playedAt(host, 6, 1), "its second after its own pre-wait");
    check(seq.next() == 0, "a loop: after the last, the first");
    check(seq.anyActive(), "a timeline of the group plays: the step is on its way");
    seq.stopAll();
    check(!seq.anyActive(), "stop all: nothing on its way");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    json();
    go();
    groups();
    if (failures) std::fprintf(stderr, "sequence: %d failure(s)\n", failures);
    else std::puts("sequence: ok");
    return failures ? 1 : 0;
}
