#include "pxs/Filters.h"

#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace pxs::controls {

namespace {
std::vector<InteractionStage> stagesOf(const QJsonArray &filters)
{
    std::vector<InteractionStage> out;
    for (const QJsonValue &v : filters)
        if (v.isObject() && out.size() < size_t(kFiltersMax)) out.push_back(InteractionStage::fromJson(v.toObject()));
    return out;
}

// Group, and what it does, of each filter
struct Info {
    InteractionFilter kind;
    const char *group, *about;
};
const Info kInfo[] = {
    {InteractionFilter::Average, "Smoothing", "The mean of what was set over a window of time: a steadier value."},
    {InteractionFilter::WeightedAverage, "Smoothing", "Follows what is set with a soft lag: the smoothing is the time to get there."},
    {InteractionFilter::AttackRelease, "Smoothing", "Rises in the attack time and falls in the release time."},
    {InteractionFilter::Damper, "Smoothing", "A spring: follows with a little life; damping 1 does not overshoot."},
    {InteractionFilter::Curve, "Shape", "Reshapes the value with a curve (the presets are the usual ones)."},
    {InteractionFilter::Math, "Shape", "An operation on the value: add, multiply, power, round, invert…"},
    {InteractionFilter::Clamp, "Shape", "Keeps the value between two limits (0 to 1 of the tile's range)."},
    {InteractionFilter::Steps, "Shape", "Rounds the value to a number of steps."},
    {InteractionFilter::Remap, "Shape", "Takes a part of the range (In) and spreads it over another (Out)."},
    {InteractionFilter::Threshold, "Shape", "On above the threshold, off below, with a margin so that it does not flicker."},
    {InteractionFilter::Delay, "Timing", "What is set arrives later."},
    {InteractionFilter::Hold, "Timing", "A fall is held for a time before it follows."},
    {InteractionFilter::Debounce, "Timing", "Follows once the value has stopped changing for a time: a flicker is ignored."},
    {InteractionFilter::AutoReturn, "Timing", "Leaves its rest value for a time, then goes back to it."},
    {InteractionFilter::Speed, "Timing", "The speed of the value (how fast it moves) instead of the value."},
    {InteractionFilter::Toggle, "Trigger", "Each time the value goes over the threshold, it flips between on and off."},
    {InteractionFilter::PulseDetection, "Trigger", "A sudden rise gives a pulse that fades."},
    {InteractionFilter::Envelope, "Trigger", "A trigger gives an ADSR: attack, decay, sustain, release."},
};
} // namespace

QJsonArray cleanFilters(const QJsonArray &filters)
{
    QJsonArray out;
    for (InteractionStage s : stagesOf(filters)) {
        for (int w = 0; w < kFilterSettingCount; ++w) {
            const FilterSetting f = interactionFilterSetting(s.kind, w);
            if (!f.name.isEmpty()) s.setting(w) = std::clamp(s.setting(w), f.min, f.max);
        }
        s.name = s.name.left(40);
        s.adsrControl.clear(); // its own ADSR: a tile has no control to take it from
        out.append(s.toJson());
    }
    return out;
}

bool anyFilterOn(const QJsonArray &filters)
{
    for (const QJsonValue &v : filters)
        if (v.isObject() && v.toObject().value("enabled").toBool(true)) return true;
    return false;
}

QJsonObject filterCatalog()
{
    QJsonArray list;
    for (const Info &i : kInfo) {
        QJsonArray settings;
        for (int w = 0; w < kFilterSettingCount; ++w) {
            const FilterSetting f = interactionFilterSetting(i.kind, w);
            if (f.name.isEmpty()) continue;
            settings.append(QJsonObject{{"id", QString(QChar('a' + w))}, {"name", f.name}, {"suffix", f.suffix.trimmed()}, {"min", f.min}, {"max", f.max}, {"def", f.def}});
        }
        list.append(QJsonObject{{"key", interactionFilterKey(i.kind)}, {"name", interactionFilterName(i.kind)}, {"group", QString::fromLatin1(i.group)}, {"about", QString::fromLatin1(i.about)}, {"settings", settings}});
    }
    QJsonArray ops;
    for (int k = 0; k < kMathOpCount; ++k) {
        InteractionStage s = InteractionStage::made(InteractionFilter::Math);
        s.op = MathOp(k);
        ops.append(QJsonObject{{"key", s.toJson().value("op").toString()}, {"name", mathOpName(MathOp(k))}, {"value", mathOpUsesValue(MathOp(k))}});
    }
    struct Preset {
        const char *name;
        int ease;
        bool inverted;
    };
    const Preset presets[] = {{"Linear", 0, false}, {"Ease in", 1, false}, {"Ease out", 2, false}, {"Ease in-out", 3, false}, {"Cubic in", 4, false}, {"Cubic out", 5, false}, {"Inverted", 0, true}};
    QJsonArray curves;
    for (const Preset &p : presets) {
        InteractionStage s = InteractionStage::made(InteractionFilter::Curve);
        AnimKey a, b;
        a.t = 0;
        a.v = p.inverted ? 1 : 0;
        a.curve = p.ease;
        b.t = 1;
        b.v = p.inverted ? 0 : 1;
        s.curve.keys = {a, b};
        curves.append(QJsonObject{{"name", QString::fromLatin1(p.name)}, {"curve", s.toJson().value("curve")}});
    }
    return QJsonObject{{"filters", list}, {"ops", ops}, {"curves", curves}, {"max", kFiltersMax}};
}

std::optional<double> FilterRunner::input(quint64 id, const QJsonArray &filters, double x)
{
    if (!std::isfinite(x)) return std::nullopt;
    Run &r = m_runs[id];
    const QByteArray sig = QJsonDocument(filters).toJson(QJsonDocument::Compact);
    if (sig != r.signature) {
        r.signature = sig;
        r.chain.filters = stagesOf(filters);
        double patience = 2;
        for (const InteractionStage &s : r.chain.filters)
            if (s.enabled) patience = std::max(patience, 2 + s.a + s.b);
        r.patience = std::min(patience, 70.0); // Delay, Hold, Auto Return: as long as they take
    }
    r.processor.input(r.chain, std::clamp(x, 0.0, 1.0));
    const std::optional<double> out = r.processor.step(r.chain, 0.0);
    r.live = r.chain.movesOverTime();
    r.quiet = 0;
    if (!out) return std::nullopt;
    r.last = std::clamp(*out, 0.0, 1.0);
    return r.last;
}

std::vector<std::pair<quint64, double>> FilterRunner::step(double dt)
{
    std::vector<std::pair<quint64, double>> moved;
    for (auto &[id, r] : m_runs) {
        if (!r.live) continue;
        const std::optional<double> out = r.processor.step(r.chain, dt);
        const double v = out ? std::clamp(*out, 0.0, 1.0) : r.last;
        if (out && std::abs(v - r.last) > 1e-6) {
            r.last = v;
            r.quiet = 0;
            moved.emplace_back(id, v);
        } else if ((r.quiet += dt) > r.patience) {
            r.live = false;
        }
    }
    return moved;
}

bool FilterRunner::active() const
{
    return std::any_of(m_runs.begin(), m_runs.end(), [](const auto &r) { return r.second.live; });
}

} // namespace pxs::controls
