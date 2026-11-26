#include <pxs/PresetView.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/tree/parameter.h>

#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QTreeWidget>
#include <cstdio>
#include <memory>

using namespace pxs::core;
using namespace pxs::core::tree;
using Type = ParamInfo::Type;

namespace {

int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what);
        ++failures;
    }
}

struct Cell {
    Value value;
    int writes = 0;
};

std::unique_ptr<Parameter> makeParam(Cell &cell, const std::string &path, Type type)
{
    auto p = std::make_unique<Parameter>(path, path, type);
    p->bind([&cell] { return cell.value; }, [&cell](const Value &v) { cell.value = v; ++cell.writes; });
    return p;
}

class TimelineHost : public pxs::presetview::PresetHost
{
public:
    Animation animation;
    bool available = true;
    std::vector<std::pair<quint64, QString>> timelineScopes() const override { return {{animation.id, animation.name}}; }
    std::optional<Animation> readTimeline(quint64 id) const override
    {
        if (!available || id != animation.id) return std::nullopt;
        return animation;
    }
    void restoreTimeline(const Animation &state) override { animation = state; }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    Tree tree;
    Cell speed;
    speed.value = Value(0.25);
    auto p = makeParam(speed, "speed", Type::Float);
    tree.add("/generator/speed", std::move(p));

    pxs::presetview::PresetView view(tree);
    check(view.presets().empty(), "initially empty");

    // Capture current state of the tree
    view.captureCurrent(QStringLiteral("My Test Preset"));
    check(view.presets().size() == 1, "preset captured");
    check(view.presets()[0].name == "My Test Preset", "preset name set");
    check(view.presets()[0].values.find("/generator/speed") != view.presets()[0].values.end(), "value captured");

    // Change live value
    speed.value = Value(0.85);
    check(speed.value.number().value_or(0) == 0.85, "value modified live");

    // Recall preset
    bool recalled = view.recallPreset(0);
    check(recalled, "recall succeeded");
    check(speed.value.number().value_or(0) == 0.25, "value restored to 0.25 from preset");

    TimelineHost host;
    host.animation.id = 42;
    host.animation.name = "Cue lights";
    AnimTrack dimmer;
    dimmer.layer = 3;
    dimmer.param = "/dimmer";
    dimmer.keys = {AnimKey{0, 0.5}};
    appendTrackRemap(dimmer, 0, 1, 0, 255);
    host.animation.tracks = {dimmer};
    pxs::presetview::PresetView timelines(tree, nullptr, &host);
    auto *scope = timelines.findChild<QComboBox *>("presetScope");
    auto *update = timelines.findChild<QPushButton *>("updatePreset");
    check(scope && update && scope->findData("timeline:42") >= 0, "timeline is offered as a scope");
    if (scope && update) {
        scope->setCurrentIndex(scope->findData("timeline:42"));
        timelines.captureCurrent(QStringLiteral("Lighting"));
        check(timelines.presets().size() == 1 && timelines.presets().front().values.empty(), "timeline-only preset captured");
        if (!timelines.presets().empty()) {
            const auto capturedId = timelines.presets().front().id;
            auto *details = timelines.inspector()->findChild<QTreeWidget *>();
            check(details && !details->findItems("Remap", Qt::MatchExactly | Qt::MatchRecursive).empty(), "stored filter is shown");
            scope->setCurrentIndex(0); // the capture menu is independent of the stored scope
            host.animation.tracks.front().keys.front().v = 0.75;
            update->click();
            check(timelines.presets().size() == 1 && timelines.presets().front().id == capturedId &&
                  timelines.presets().front().scope == "timeline:42", "updating preserves the preset identity and scope");
            const auto saved = Preset::fromJson(timelines.presets().front().toJson());
            timelines.setPresets({saved});
            host.animation.tracks.clear();
            check(timelines.recallPreset(0) && host.animation.tracks.size() == 1, "recall restores a track after serialization");
            if (!host.animation.tracks.empty()) {
                const auto &restored = host.animation.tracks.front();
                check(restored.layer == 3 && restored.param == "/dimmer" && restored.keys.front().v == 0.75 &&
                      restored.dataFilters == dimmer.dataFilters, "recall restores routing, edited key and Remap");
            }
            host.available = false;
            check(!timelines.recallPreset(0), "recall fails if the timeline is missing");
        }
    }

    if (failures) std::fprintf(stderr, "presetview: %d failure(s)\n", failures);
    else std::puts("presetview: ok");
    return failures ? 1 : 0;
}
