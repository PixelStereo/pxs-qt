// The filters of a tile: what an editor sends is made valid, the catalog the page draws, the chain on a value (at once, and over time).
#include <pxs/Control.h>
#include <pxs/Filters.h>

#include <cmath>
#include <cstdio>

using pxs::controls::Control;
using namespace pxs::controls;

static int failures = 0;
static void check(bool ok, const char *what)
{
    if (ok) return;
    std::printf("FAIL filters: %s\n", what);
    ++failures;
}

static QJsonObject stage(const char *type, double a = 0, double b = 0, bool on = true)
{
    QJsonObject o{{"type", type}, {"a", a}, {"b", b}};
    if (!on) o["enabled"] = false;
    return o;
}

int main()
{
    // The catalog: every filter, with its settings, and the operations and curves
    const QJsonObject cat = filterCatalog();
    check(cat.value("filters").toArray().size() == kInteractionFilterCount, "the catalog has every filter");
    check(cat.value("ops").toArray().size() == kMathOpCount && cat.value("curves").toArray().size() >= 6, "the operations and curves are there");
    for (const QJsonValue &v : cat.value("filters").toArray()) {
        bool ok = false;
        interactionFilterFromKey(v.toObject().value("key").toString(), &ok);
        check(ok && !v.toObject().value("name").toString().isEmpty() && !v.toObject().value("about").toString().isEmpty() && !v.toObject().value("group").toString().isEmpty(), "a filter of the catalog is known and described");
    }

    // Made valid: settings in range, a few stages, a curve kept
    QJsonArray many;
    for (int i = 0; i < 20; ++i) many.append(stage("clamp", -5, 99));
    const QJsonArray clean = cleanFilters(many);
    check(clean.size() == kFiltersMax, "at most a few filters on a tile");
    check(clean[0].toObject().value("a").toDouble() == 0 && clean[0].toObject().value("b").toDouble() == 1, "a setting is kept within its range");
    const QJsonValue curve = cat.value("curves").toArray()[1].toObject().value("curve");
    QJsonObject withCurve{{"type", "curve"}, {"curve", curve}};
    check(cleanFilters(QJsonArray{withCurve})[0].toObject().value("curve") == curve, "a curve survives cleaning");
    check(anyFilterOn(clean) && !anyFilterOn(QJsonArray{stage("clamp", 0, 1, false)}) && !anyFilterOn({}), "on or off");

    // On a control: kept in its JSON, edited by props, for a slider and a switch only
    Control s;
    s.kind = Control::Kind::Slider;
    s.applyProps(QJsonObject{{"filters", QJsonArray{stage("clamp", 0.2, 0.8)}}});
    check(s.filters.size() == 1 && Control::fromJson(s.toJson()).filters == s.filters && s.wireJson().value("filters").toArray() == s.filters, "a slider keeps its filters through JSON");
    s.applyProps(QJsonObject{{"filters", QJsonArray{}}});
    check(s.filters.isEmpty() && !s.toJson().contains("filters"), "no filter: nothing in the JSON");
    Control t;
    t.kind = Control::Kind::Toggle;
    t.applyProps(QJsonObject{{"filters", QJsonArray{stage("hold", 0.5)}}});
    check(t.filters.size() == 1, "a switch takes filters");
    Control b;
    b.kind = Control::Kind::Button;
    b.applyProps(QJsonObject{{"filters", QJsonArray{stage("clamp", 0, 1)}}});
    check(b.filters.isEmpty(), "a button does not");

    // Feedback: the target that gives the value the tile shows, kept through removals
    {
        Control m;
        m.kind = Control::Kind::Slider;
        for (const char *path : {"/a", "/b", "/c"}) {
            pxs::controls::Target n;
            n.path = path;
            m.targets.push_back(n);
        }
        check(m.feedback == -1, "no feedback to begin with");
        m.applyProps(QJsonObject{{"feedback", 2}});
        check(m.feedback == 2 && Control::fromJson(m.toJson()).feedback == 2 && m.wireJson().value("feedback").toInt() == 2, "a target gives the feedback, kept through JSON");
        m.applyProps(QJsonObject{{"feedback", 9}});
        check(m.feedback == -1, "one that is not there: none");
        m.applyProps(QJsonObject{{"feedback", 2}});
        // the first target removed: the feedback is the same one, now second
        m.applyProps(QJsonObject{{"targets", QJsonArray{QJsonObject{{"i", 1}}, QJsonObject{{"i", 2}}}}});
        check(m.targets.size() == 2 && m.feedback == 1 && m.targets[1].path == "/c", "the feedback follows its target");
        m.applyProps(QJsonObject{{"targets", QJsonArray{QJsonObject{{"i", 0}}}}, {"feedback", 1}});
        check(m.targets.size() == 1 && m.feedback == -1, "its target removed: none");
    }

    // A tile that only shows: read-only, kept through JSON, told to the page
    {
        Control r;
        r.kind = Control::Kind::Slider;
        r.readOnly = true;
        check(Control::fromJson(r.toJson()).readOnly && r.wireJson().value("ro").toBool() && !Control().wireJson().contains("ro"), "a tile that is only shown is read-only");
    }

    // The range of a tile that is a view: its own once the operator sets it, the parameter's again when asked
    {
        Control v;
        v.kind = Control::Kind::Slider;
        v.boundParam = "/p";
        check(!v.ownRange, "a view has the parameter's range to begin with");
        v.applyProps(QJsonObject{{"max", 127}});
        check(v.ownRange && v.max == 127 && Control::fromJson(v.toJson()).ownRange && v.wireJson().value("ownRange").toBool(), "setting a range makes it the tile's own");
        v.applyProps(QJsonObject{{"ownRange", false}});
        check(!v.ownRange && !v.toJson().contains("ownRange"), "and it can be given back");
        Control free;
        free.kind = Control::Kind::Slider;
        free.applyProps(QJsonObject{{"max", 5}});
        check(!free.ownRange, "a tile that is not a view has no parameter range to leave");
    }

    // The chain, at once
    FilterRunner run;
    const QJsonArray clamp{stage("clamp", 0.2, 0.8)};
    check(run.input(1, clamp, 1.0).value_or(-1) == 0.8 && run.input(1, clamp, 0.0).value_or(-1) == 0.2, "a clamp acts at once");
    check(!run.active(), "and nothing goes on after it");
    check(run.input(2, QJsonArray{stage("clamp", 0.2, 0.8, false)}, 1.0).value_or(-1) == 1.0, "a stage that is off lets it through");
    check(!run.input(3, clamp, std::nan("")).has_value(), "no number, nothing");

    // What each stage made of the value, to show in the inspector
    {
        FilterRunner tr;
        check(!tr.trace(5).has_value(), "nothing set: no trace");
        tr.input(5, QJsonArray{stage("clamp", 0.2, 0.8), stage("clamp", 0.3, 0.5)}, 1.0);
        const auto t = tr.trace(5);
        check(t && t->in == 1.0 && t->stages.size() == 2 && t->stages[0] == 0.8 && t->stages[1] == 0.5, "the value after each stage");
    }

    // Over time: a smoothing starts where the value is, then follows it, and stops when it got there
    FilterRunner slow;
    const QJsonArray smooth{stage("weighted_average", 0.3)};
    check(slow.input(9, smooth, 0.0).value_or(-1) == 0.0, "a smoothing starts where the value is");
    slow.input(9, smooth, 1.0);
    check(slow.active(), "a smoothing goes on by itself");
    double last = 0;
    int frames = 0;
    for (; frames < 1000 && slow.active(); ++frames) {
        for (const auto &m : slow.step(0.016)) {
            check(m.first == 9 && m.second >= last - 1e-9, "it rises");
            last = m.second;
        }
    }
    check(last > 0.99 && frames < 1000, "it gets there, and then it is still");
    check(!slow.active(), "and stops");
    slow.forget(9);
    check(slow.step(0.016).empty(), "a tile forgotten");

    if (!failures) std::printf("filters: OK\n");
    return failures;
}
