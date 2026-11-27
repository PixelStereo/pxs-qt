// The timeline editor, on a host of its own: it shows what the host holds, and what it edits goes back to the host
#include <pxs/TimelineEditor.h>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>

#include <cstdio>

using namespace pxs::timelineview;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

namespace {

// One number to drive, "level", from 0 to 10
class Host : public TimelineHost
{
public:
    std::vector<ParamInfo> animatableParams(quint64) const override
    {
        ParamInfo p;
        p.path = "level";
        p.label = "Output › Level";
        p.min = p.lo = 0;
        p.max = p.hi = 10;
        p.animatable = true;
        return {p};
    }
    bool animParamValue(quint64, const QString &path, double *value) const override
    {
        if (path != "level") return false;
        *value = 2.5;
        return true;
    }
};

// The timeline lives here, in memory
class Editor : public AnimEditor
{
public:
    Editor(TimelineHost *host, Animation *timeline) : AnimEditor(host, nullptr, Layout::Side, false), m_timeline(timeline) {}
    int stores = 0;
    std::vector<AnimAction> actions;
    double lastTime = 0;

protected:
    bool fetchPlayback(AnimationPlayback *a) const override
    {
        a->id = m_timeline->id;
        a->state = m_timeline->state;
        a->loop = m_timeline->loop;
        a->duration = m_timeline->duration;
        a->position = m_timeline->position();
        a->clock = m_timeline->clock;
        a->played = m_timeline->played();
        a->tracks = int(m_timeline->tracks.size());
        return true;
    }
    bool fetch(Animation *a) const override
    {
        *a = *m_timeline;
        return true;
    }
    void store(const Animation &, const Animation &after, const QString &, const QString &) override
    {
        *m_timeline = after;
        ++stores;
    }
    void control(AnimAction action, double time) override
    {
        actions.push_back(action);
        lastTime = time;
    }

private:
    Animation *m_timeline;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Host host;
    Animation timeline;
    timeline.id = 1;
    AnimTrack t;
    t.param = "level";
    t.oscillator = true;
    t.wave = AnimWave::Sine;
    timeline.tracks.push_back(t);

    Editor editor(&host, &timeline);
    editor.reload();
    editor.resize(900, 300);
    editor.show();
    app.processEvents();

    // The wave of the track: chosen in its menu, it goes to the host
    const auto waveMenu = [&] {
        QComboBox *found = nullptr;
        for (QComboBox *box : editor.findChildren<QComboBox *>())
            if (box->isVisibleTo(&editor) && box->count() == kAnimWaveCount && box->itemText(1) == animWaveName(AnimWave::Triangle)) found = box;
        return found;
    };
    QComboBox *waves = waveMenu();
    CHECK(waves);
    if (waves) {
        CHECK(waves->currentIndex() == int(AnimWave::Sine));
        emit waves->activated(int(AnimWave::Saw));
        CHECK(timeline.tracks.front().wave == AnimWave::Saw);
        CHECK(editor.stores >= 1);
    }

    // What the host holds now is shown when it reloads
    timeline.tracks.front().wave = AnimWave::Square;
    timeline.duration = 7;
    editor.reload();
    app.processEvents(); // the rows made again: the old ones go
    waves = waveMenu();
    CHECK(waves && waves->currentIndex() == int(AnimWave::Square));
    bool durationShown = false;
    for (QDoubleSpinBox *box : editor.findChildren<QDoubleSpinBox *>())
        if (box->value() == 7) durationShown = true;
    CHECK(durationShown);

    // The menu of the numbers a track can drive, by category
    const auto params = animationMenuParams(&host, 0);
    CHECK(params.size() == 1 && params.front().path == "level");
    CHECK(animWaveName(AnimWave::Square) == "Step (square)");

    // Navigate by timeline position even when a ping-pong pass runs backwards.
    timeline.tracks.front().oscillator = false;
    timeline.tracks.front().keys = {{1, 2}, {4, 3}, {6, 2}};
    AnimTrack disabled = timeline.tracks.front();
    disabled.enabled = false;
    disabled.keys = {{3, 2}};
    timeline.tracks.push_back(disabled);
    timeline.loop = AnimLoop::PingPong;
    timeline.state = AnimState::Paused;
    timeline.clock = 12; // second pass, at 2 s of a 7 s timeline
    editor.reload();
    QPushButton *next = nullptr;
    QPushButton *previous = nullptr;
    for (auto *button : editor.findChildren<QPushButton *>()) {
        if (button->accessibleName() == "Next keyframe") next = button;
        if (button->accessibleName() == "Previous keyframe") previous = button;
    }
    CHECK(next && previous);
    if (next && previous) {
        next->click();
        CHECK(!editor.actions.empty() && editor.actions.back() == AnimAction::Seek && editor.lastTime == 10);
        previous->click();
        CHECK(!editor.actions.empty() && editor.actions.back() == AnimAction::Seek && editor.lastTime == 13);
    }

    if (failures) std::fprintf(stderr, "timelineview: %d check(s) failed\n", failures);
    else std::printf("timelineview: ok\n");
    return failures ? 1 : 0;
}
