#include <pxs/PresetView.h>
#include <pxs/core/tree/tree.h>
#include <pxs/core/tree/parameter.h>

#include <QApplication>
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

    if (failures) std::fprintf(stderr, "presetview: %d failure(s)\n", failures);
    else std::puts("presetview: ok");
    return failures ? 1 : 0;
}
