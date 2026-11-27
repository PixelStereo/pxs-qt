// The timeline editor, on a host of its own: it shows what the host holds, and what it edits goes back to the host
#include <pxs/TimelineEditor.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMouseEvent>
#include <QSlider>
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
    std::vector<double> filterValues;
    bool animationFilterSignal(quint64, quint64, const QString &, double *input, std::vector<double> *after) const override
    {
        if (filterValues.empty()) return false;
        *input = 0.5;
        *after = filterValues;
        return true;
    }
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

    // Overlay selection changes the editable lane without writing to either curve.
    t.oscillator = true;
    t.center = 2;
    t.amplitude = 0;
    auto other = t;
    other.center = 8;
    timeline.tracks = {t, other};
    editor.reload();
    app.processEvents();
    auto *overlay = editor.findChild<QCheckBox *>("overlayTracks");
    auto *picker = editor.findChild<QComboBox *>("overlayEditTrack");
    auto *clickSelect = editor.findChild<QCheckBox *>("overlayClickSelect");
    CHECK(overlay && picker && clickSelect);
    if (overlay && picker && clickSelect) {
        overlay->setChecked(true);
        app.processEvents();
        const auto editorLane = [&]() -> pxs::curveview::CurveLane * {
            pxs::curveview::CurveLane *visible = nullptr;
            for (auto *lane : editor.findChildren<pxs::curveview::CurveLane *>())
                if (lane->isVisibleTo(&editor)) {
                    CHECK(!visible);
                    visible = lane;
                }
            return visible;
        };
        const auto clickValue = [&](double value) {
            auto *lane = editorLane();
            CHECK(lane);
            if (!lane) return;
            const double lo = t.center - 1e-6, hi = other.center + 1e-6;
            const QPointF point(lane->width() / 2., 8 + (hi - value) / (hi - lo) * (lane->height() - 16));
            QMouseEvent event(QEvent::MouseButtonPress, point, lane->mapToGlobal(point.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(lane, &event);
            app.processEvents();
        };
        const int before = editor.stores;
        picker->setCurrentIndex(1);
        CHECK(editorLane() && editorLane()->track().center == 8);
        clickValue(2);
        CHECK(picker->currentIndex() == 0);
        clickSelect->setChecked(false);
        clickValue(8);
        CHECK(picker->currentIndex() == 0 && editor.stores == before);
    }

    // The unfiltered meter is present without a filter, on the curve's own scale.
    t.center = 0.5;
    t.amplitude = 0.5;
    timeline.tracks = {t};
    editor.reload();
    app.processEvents();
    const auto visibleMeters = [&](const QString &name) {
        QList<QSlider *> visible;
        for (auto *meter : editor.findChildren<QSlider *>(name))
            if (meter->isVisibleTo(&editor)) visible.append(meter);
        return visible;
    };
    editor.poll();
    auto outputs = visibleMeters(QStringLiteral("animationTimelineSignal"));
    CHECK(outputs.size() == 1 && outputs.front()->value() == 500);
    CHECK(outputs.size() == 1 && outputs.front()->accessibleDescription() == QStringLiteral("Range: 0 to 1"));
    CHECK(visibleMeters(QStringLiteral("animationFilterSignal")).empty());

    appendTrackRemap(timeline.tracks[0], 0, 1, 0, 255);
    auto clamp = InteractionStage::made(InteractionFilter::Clamp);
    clamp.a = 0;
    clamp.b = 200;
    timeline.tracks[0].dataFilters.append(clamp.toJson());
    host.filterValues = {127.5, 127.5};
    editor.reload();
    app.processEvents();
    editor.poll();
    outputs = visibleMeters(QStringLiteral("animationTimelineSignal"));
    CHECK(outputs.size() == 1 && outputs.front()->value() == 500);
    const auto filters = visibleMeters(QStringLiteral("animationFilterSignal"));
    CHECK(filters.size() == 2);
    if (filters.size() == 2) {
        CHECK(filters[0]->accessibleDescription() == QStringLiteral("Range: 0 to 255"));
        CHECK(filters[0]->value() == 500);
        CHECK(filters[1]->accessibleDescription() == QStringLiteral("Range: 0 to 200"));
        CHECK(filters[1]->value() == 638);
    }

    // Navigate by timeline position even when a ping-pong pass runs backwards.
    t.oscillator = false;
    t.keys = {{1, 2}, {4, 3}, {6, 2}};
    timeline.tracks = {t};
    timeline.duration = 7;
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
