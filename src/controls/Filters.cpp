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

// The filters as the editor lists them, by group (their group and what they do are pxs-core's)
const InteractionFilter kOrder[] = {
    InteractionFilter::Average, InteractionFilter::WeightedAverage, InteractionFilter::AttackRelease, InteractionFilter::Damper, InteractionFilter::Physics,
    InteractionFilter::Curve, InteractionFilter::Math, InteractionFilter::Clamp, InteractionFilter::Steps, InteractionFilter::Remap, InteractionFilter::Threshold,
    InteractionFilter::Delay, InteractionFilter::Hold, InteractionFilter::Debounce, InteractionFilter::AutoReturn, InteractionFilter::Speed,
    InteractionFilter::Toggle, InteractionFilter::PulseDetection, InteractionFilter::Envelope,
};
QString aboutOf(InteractionFilter kind)
{
    const QString about = QString::fromUtf8(pxs::core::filter::about(kind));
    return kind == InteractionFilter::Clamp ? about.chopped(1) + QStringLiteral(" (0 to 1 of the tile's range).") : about; // on a tile
}
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
    for (const InteractionFilter kind : kOrder) {
        QJsonArray settings;
        for (int w = 0; w < kFilterSettingCount; ++w) {
            const FilterSetting f = interactionFilterSetting(kind, w);
            if (f.name.isEmpty()) continue;
            settings.append(QJsonObject{{"id", QString(QChar('a' + w))}, {"name", f.name}, {"suffix", f.suffix.trimmed()}, {"min", f.min}, {"max", f.max}, {"def", f.def}});
        }
        list.append(QJsonObject{{"key", interactionFilterKey(kind)}, {"name", interactionFilterName(kind)}, {"group", QString::fromUtf8(pxs::core::filter::group(kind))}, {"about", aboutOf(kind)}, {"settings", settings}});
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

std::optional<FilterTrace> FilterRunner::trace(quint64 id) const
{
    const auto it = m_runs.find(id);
    if (it == m_runs.end() || !it->second.processor.level()) return std::nullopt;
    return FilterTrace{*it->second.processor.level(), it->second.processor.stageOutputs()};
}

bool FilterRunner::active() const
{
    return std::any_of(m_runs.begin(), m_runs.end(), [](const auto &r) { return r.second.live; });
}

} // namespace pxs::controls
