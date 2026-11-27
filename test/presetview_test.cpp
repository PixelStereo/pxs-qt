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
    std::vector<std::pair<QString, QString>> scopes() const override
    {
        return {{QStringLiteral("/timelines"), QStringLiteral("All timelines")},
                {QStringLiteral("/timelines/lighting"), QStringLiteral("Lighting")}};
    }
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
    Cell duration{Value(0.5)}, level{Value(0.25)}, external{Value(0.75)};
    tree.add("/timelines/lighting/settings/duration", makeParam(duration, "Duration", Type::Float));
    tree.add("/timelines/lighting/tracks/opacity/level", makeParam(level, "Level", Type::Float));
    tree.add("/devices/millumin/opacity", makeParam(external, "Opacity", Type::Float));
    pxs::presetview::PresetView timelines(tree, nullptr, &host);
    auto *scope = timelines.findChild<QComboBox *>("presetScope");
    auto *update = timelines.findChild<QPushButton *>("updatePreset");
    auto *edit = timelines.inspector()->findChild<QPushButton *>("editPreset");
    auto *remove = timelines.inspector()->findChild<QPushButton *>("removePresetParameter");
    auto *details = timelines.inspector()->findChild<QTreeWidget *>();
    check(scope && update && edit && remove && details && scope->count() == 2, "only timeline scopes offered");
    if (scope && update && edit && remove && details) {
        const std::string path = "/timelines/lighting/tracks/opacity/level";
        scope->setCurrentIndex(scope->findData("/timelines/lighting"));
        timelines.captureCurrent(QStringLiteral("Lighting"));
        check(timelines.presets().size() == 1 && timelines.presets().front().values.size() == 2, "timeline parameters only");
        if (!timelines.presets().empty()) {
            const auto id = timelines.presets().front().id;
            auto saved = timelines.presets().front();
            saved.delays[path] = 1;
            timelines.setPresets({saved});
            edit->click();
            const auto rows = details->findItems("Level", Qt::MatchExactly | Qt::MatchRecursive);
            check(!rows.empty(), "parameter tree uses the short label");
            if (!rows.empty()) {
                details->setCurrentItem(rows.front());
                remove->click();
            }
            check(timelines.presets().front().values.count(path) == 0 && timelines.presets().front().delays.count(path) == 0,
                  "removing a row clears its value and timing");
            duration.value = Value(0.8);
            level.value = Value(0.6);
            update->click();
            check(timelines.presets().front().id == id && timelines.presets().front().values.size() == 1,
                  "update keeps the preset identity and membership");
            duration.value = Value(0.4);
            check(timelines.recallPreset(0) && duration.value.number() == 0.8 && level.value.number() == 0.6,
                  "recall changes only the remaining row");
        }
    }

    if (failures) std::fprintf(stderr, "presetview: %d failure(s)\n", failures);
    else std::puts("presetview: ok");
    return failures ? 1 : 0;
}
