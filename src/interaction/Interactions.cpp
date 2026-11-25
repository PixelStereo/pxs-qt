#include "Interactions.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
static const char *const kSources[] = {"keyboard", "midi", "osc", "internal", "audio"};
template <size_t N> static int keyIndex(const char *const (&keys)[N], const QString &k, int def)
{
    for (size_t i = 0; i < N; ++i)
        if (k == QLatin1String(keys[i])) return int(i);
    return def;
}
static const char *const kMidi[] = {"cc", "note", "pitch_bend", "program"};

static const char *const kOps[] = {"eq", "ne", "gt", "ge", "lt", "le", "in", "out"};

QString conditionOpName(ConditionOp op)
{
    switch (op) {
    case ConditionOp::Equal: return QStringLiteral("=");
    case ConditionOp::NotEqual: return QStringLiteral("≠");
    case ConditionOp::Greater: return QStringLiteral(">");
    case ConditionOp::GreaterOrEqual: return QStringLiteral("≥");
    case ConditionOp::Less: return QStringLiteral("<");
    case ConditionOp::LessOrEqual: return QStringLiteral("≤");
    case ConditionOp::InRange: return QStringLiteral("in range");
    case ConditionOp::OutOfRange: return QStringLiteral("out of range");
    }
    return QString();
}

bool InteractionCondition::test(double x) const
{
    const bool same = std::abs(x - a) <= 1e-6 * std::max(1.0, std::abs(a));
    const double lo = std::min(a, b), hi = std::max(a, b);
    switch (op) {
    case ConditionOp::Equal: return same;
    case ConditionOp::NotEqual: return !same;
    case ConditionOp::Greater: return x > a && !same;
    case ConditionOp::GreaterOrEqual: return x >= a || same;
    case ConditionOp::Less: return x < a && !same;
    case ConditionOp::LessOrEqual: return x <= a || same;
    case ConditionOp::InRange: return x >= lo && x <= hi;
    case ConditionOp::OutOfRange: return x < lo || x > hi;
    }
    return false;
}

bool Interaction::movesOverTime() const
{
    return std::any_of(filters.begin(), filters.end(), [](const InteractionStage &f) {
        return f.enabled && f.kind != InteractionFilter::Curve && f.kind != InteractionFilter::Math &&
               f.kind != InteractionFilter::Clamp && f.kind != InteractionFilter::Steps && f.kind != InteractionFilter::Threshold;
    });
}

bool Interaction::conditionsMet(double x) const
{
    if (conditions.empty()) return true;
    if (conditionsAll) return std::all_of(conditions.begin(), conditions.end(), [x](const auto &c) { return c.test(x); });
    return std::any_of(conditions.begin(), conditions.end(), [x](const auto &c) { return c.test(x); });
}

QString interactionSourceName(InteractionSource s)
{
    switch (s) {
    case InteractionSource::Keyboard: return QStringLiteral("Keyboard");
    case InteractionSource::Midi: return QStringLiteral("MIDI");
    case InteractionSource::Osc: return QStringLiteral("OSC");
    case InteractionSource::Internal: return QStringLiteral("Internal");
    case InteractionSource::Audio: return QStringLiteral("Audio");
    }
    return QString();
}

QString interactionFilterName(InteractionFilter f) { return QString::fromUtf8(pxs::core::filter::name(f)); }

QString interactionFilterKey(InteractionFilter f) { return QString::fromLatin1(pxs::core::filter::key(f)); }

InteractionFilter interactionFilterFromKey(const QString &k, bool *ok)
{
    const auto kind = pxs::core::filter::kindFromKey(k.toStdString());
    if (ok) *ok = kind.has_value();
    return kind.value_or(InteractionFilter::Average);
}

QString mathOpName(MathOp op) { return QString::fromUtf8(pxs::core::filter::name(op)); }

bool mathOpUsesValue(MathOp op) { return pxs::core::filter::usesValue(op); }

double applyMathOp(MathOp op, double x, double a) { return pxs::core::filter::apply(op, x, a); }

QString midiMessageName(MidiMessageKind k)
{
    switch (k) {
    case MidiMessageKind::ControlChange: return QStringLiteral("CC");
    case MidiMessageKind::Note: return QStringLiteral("Note");
    case MidiMessageKind::PitchBend: return QStringLiteral("Pitch Bend");
    case MidiMessageKind::ProgramChange: return QStringLiteral("Program");
    }
    return QString();
}

FilterSetting interactionFilterSetting(InteractionFilter f, int which)
{
    const pxs::core::filter::Setting c = pxs::core::filter::setting(f, which);
    if (!c.name) return FilterSetting{};
    return FilterSetting{QString::fromUtf8(c.name), QString::fromUtf8(c.suffix), c.min, c.max, c.def};
}

QString InteractionEvent::describe() const
{
    switch (source) {
    case InteractionSource::Keyboard: return QStringLiteral("Key %1").arg(address);
    case InteractionSource::Midi: {
        QString s = QStringLiteral("MIDI · ch %1 · %2").arg(channel).arg(midiMessageName(message));
        if (message != MidiMessageKind::PitchBend) s += QStringLiteral(" %1").arg(number);
        return s;
    }
    case InteractionSource::Osc: return QStringLiteral("OSC %1").arg(address) + (argument ? QStringLiteral(" [%1]").arg(argument) : QString());
    case InteractionSource::Internal: return QStringLiteral("Internal");
    case InteractionSource::Audio: return QStringLiteral("Audio");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------
bool Interaction::matches(const InteractionEvent &e) const
{
    if (e.source != source) return false;
    switch (source) {
    case InteractionSource::Keyboard: return !key.isEmpty() && e.address.compare(key, Qt::CaseInsensitive) == 0;
    case InteractionSource::Midi:
        if (!midiPort.isEmpty() && e.port != midiPort) return false;
        if (midiChannel && e.channel != midiChannel) return false;
        if (e.message != midiMessage) return false;
        return midiMessage == MidiMessageKind::PitchBend || e.number == midiNumber;
    case InteractionSource::Osc: return !oscAddress.isEmpty() && e.address == oscAddress && e.argument == oscArgument;
    case InteractionSource::Audio:
    case InteractionSource::Internal: return false; // read from the show each frame, not sent
    }
    return false;
}

QString Interaction::label() const
{
    if (!name.isEmpty()) return name;
    switch (source) {
    case InteractionSource::Keyboard: return key.isEmpty() ? QStringLiteral("Key") : QStringLiteral("Key %1").arg(key);
    case InteractionSource::Midi:
        return QStringLiteral("MIDI %1 %2%3")
            .arg(midiChannel ? QStringLiteral("ch %1").arg(midiChannel) : QStringLiteral("any ch"), midiMessageName(midiMessage),
                 midiMessage == MidiMessageKind::PitchBend ? QString() : QStringLiteral(" %1").arg(midiNumber));
    case InteractionSource::Osc: return oscAddress.isEmpty() ? QStringLiteral("OSC") : QStringLiteral("OSC %1").arg(oscAddress);
    case InteractionSource::Internal: return QStringLiteral("Internal %1").arg(id);
    case InteractionSource::Audio: {
        const QString what = audioFeature==AudioFeature::Band ? QStringLiteral("%1–%2 Hz").arg(audioLow,0,'f',0).arg(audioHigh,0,'f',0) : audioFeature==AudioFeature::Level ? QStringLiteral("level") : audioFeature==AudioFeature::Hit ? QStringLiteral("hits %1–%2 Hz").arg(audioLow,0,'f',0).arg(audioHigh,0,'f',0) : QStringLiteral("attacks");
        return (audioInput ? QStringLiteral("Audio in ") : QStringLiteral("Audio ")) + what;
    }
    }
    return QString();
}

static const char *const kNotState[] = {
    "id",           "name",           "source",          "key",         "port",         "channel",
    "message",      "number",         "address",         "argument",    "source_layer", "source_param",
    "audio_low",    "audio_high",     "audio_feature",   "audio_input",
    "layer",       "param",        "mode",
    "conditions",   "conditions_all", "if_true",         "if_false"}; // an Action's: its definition

QJsonObject Interaction::stateJson() const
{
    QJsonObject o = toJson();
    for (const char *k : kNotState) o.remove(QLatin1String(k));
    o.remove("xy_y"); // source definitions remain unchanged during snapshot recall
    if (!xyInputs.empty()) o["xy_y_state"] = xyInputs.front().stateJson();
    return o;
}

void Interaction::applyState(const QJsonObject &state)
{
    QJsonObject kept;
    for (auto it = state.begin(); it != state.end(); ++it) {
        bool keep = true;
        for (const char *k : kNotState) keep = keep && it.key() != QLatin1String(k);
        if (keep && it.key() != QLatin1String("xy_y") && it.key() != QLatin1String("xy_y_state")) kept[it.key()] = it.value();
    }
    *this = fromJson(merged(toJson(), kept));
    if (!xyInputs.empty() && state.contains("xy_y_state")) xyInputs.front().applyState(state.value("xy_y_state").toObject());
}

// ---------------------------------------------------------------------------
// The filters
// ---------------------------------------------------------------------------
InteractionStage InteractionStage::made(InteractionFilter kind)
{
    InteractionStage s;
    s.kind = kind;
    for (int w = 0; w < kFilterSettingCount; ++w) s.setting(w) = interactionFilterSetting(kind, w).def;
    if (kind == InteractionFilter::Curve) s.curve = Interaction::straightCurve();
    return s;
}

InteractionStage InteractionStage::remap(double inMin, double inMax, double outMin, double outMax)
{
    InteractionStage s = made(InteractionFilter::Remap);
    s.a = inMin;
    s.b = inMax;
    s.c = outMin;
    s.d = outMax;
    return s;
}

bool InteractionStage::operator==(const InteractionStage &o) const
{
    if (kind != o.kind || name != o.name || enabled != o.enabled || a != o.a || b != o.b || c != o.c || d != o.d || op != o.op || adsrControl != o.adsrControl ||
        curve.keys.size() != o.curve.keys.size())
        return false;
    for (size_t k = 0; k < curve.keys.size(); ++k) {
        const AnimKey &x = curve.keys[k], &y = o.curve.keys[k];
        if (x.t != y.t || x.v != y.v || x.curve != y.curve || x.inDt != y.inDt || x.inDv != y.inDv || x.outDt != y.outDt ||
            x.outDv != y.outDv)
            return false;
    }
    return true;
}

static QJsonArray curveToJson(const AnimTrack &curve)
{
    QJsonArray keys;
    for (const AnimKey &k : curve.keys) {
        QJsonObject kj{{"t", k.t}, {"v", k.v}, {"curve", k.curve}};
        if (k.hasIn()) kj["in"] = QJsonArray{k.inDt, k.inDv};
        if (k.hasOut()) kj["out"] = QJsonArray{k.outDt, k.outDv};
        keys.append(kj);
    }
    return keys;
}

static AnimTrack curveFromJson(const QJsonArray &a)
{
    AnimTrack curve;
    for (const QJsonValue &v : a) {
        const QJsonObject kj = v.toObject();
        AnimKey k;
        k.t = std::clamp(kj.value("t").toDouble(), 0.0, 1.0);
        k.v = kj.value("v").toDouble();
        k.curve = kj.value("curve").toInt(0);
        const QJsonArray in = kj.value("in").toArray(), out = kj.value("out").toArray();
        k.inDt = in.at(0).toDouble(0);
        k.inDv = in.at(1).toDouble(0);
        k.outDt = out.at(0).toDouble(0);
        k.outDv = out.at(1).toDouble(0);
        curve.keys.push_back(k);
    }
    std::sort(curve.keys.begin(), curve.keys.end(), [](const AnimKey &x, const AnimKey &y) { return x.t < y.t; });
    if (curve.keys.empty()) curve = Interaction::straightCurve();
    return curve;
}

QJsonObject InteractionStage::toJson() const
{
    QJsonObject o{{"type", interactionFilterKey(kind)}};
    if (!name.isEmpty()) o["name"] = name;
    if (!enabled) o["enabled"] = false;
    for (int w = 0; w < kFilterSettingCount; ++w)
        if (!interactionFilterSetting(kind, w).name.isEmpty()) o[QString(QChar('a' + w))] = setting(w);
    if (kind == InteractionFilter::Math) o["op"] = QString::fromLatin1(pxs::core::filter::key(op));
    if (kind == InteractionFilter::Curve) o["curve"] = curveToJson(curve);
    if (kind == InteractionFilter::Envelope && !adsrControl.isEmpty()) o["adsr_control"] = adsrControl;
    return o;
}

InteractionStage InteractionStage::fromJson(const QJsonObject &o)
{
    InteractionStage s = made(interactionFilterFromKey(o.value("type").toString()));
    s.name = o.value("name").toString().trimmed();
    s.enabled = o.value("enabled").toBool(true);
    for (int w = 0; w < kFilterSettingCount; ++w) {
        const double v = o.value(QString(QChar('a' + w))).toDouble(s.setting(w));
        if (std::isfinite(v)) s.setting(w) = v;
    }
    s.op = pxs::core::filter::mathOpFromKey(o.value("op").toString().toStdString()).value_or(MathOp::Multiply);
    if (s.kind == InteractionFilter::Curve) s.curve = curveFromJson(o.value("curve").toArray());
    if (s.kind == InteractionFilter::Envelope) s.adsrControl = o.value("adsr_control").toString();
    return s;
}

QJsonArray Interaction::filtersToJson(const std::vector<InteractionStage> &filters)
{
    QJsonArray a;
    for (const InteractionStage &s : filters) a.append(s.toJson());
    return a;
}

std::vector<InteractionStage> Interaction::filtersFromJson(const QJsonObject &o)
{
    std::vector<InteractionStage> out;
    for (const QJsonValue &v : o.value("filters").toArray())
        if (v.isObject()) out.push_back(InteractionStage::fromJson(v.toObject()));
    return out;
}

QJsonObject Interaction::merged(QJsonObject base, const QJsonObject &over)
{
    for (auto it = over.begin(); it != over.end(); ++it) base[it.key()] = it.value();
    return base;
}

AnimTrack Interaction::straightCurve()
{
    AnimTrack t;
    AnimKey a, b;
    a.t = 0;
    a.v = 0;
    b.t = 1;
    b.v = 1;
    t.keys = {a, b};
    return t;
}

Interaction Interaction::made(InteractionSource s)
{
    Interaction it;
    it.source = s;
    // A fader or a knob: it waits to meet the value (pick-up); a key, a link: it sets it
    it.takeover = s == InteractionSource::Midi || s == InteractionSource::Osc ? Takeover::PickUp : Takeover::Jump;
    return it;
}

QJsonObject InteractionGroup::toJson() const
{
    QJsonObject o{{"id", QString::number(id)}, {"name", name}};
    if (parent) o["parent"] = QString::number(parent);
    return o;
}

InteractionGroup InteractionGroup::fromJson(const QJsonObject &o)
{
    InteractionGroup g;
    g.id = o.value("id").toString().toULongLong();
    g.name = o.value("name").toString();
    g.parent = o.value("parent").toString().toULongLong();
    return g;
}

QJsonObject Interaction::toJson() const
{
    QJsonObject o;
    o["id"] = QString::number(id); // ids are strings in files
    if (!name.isEmpty()) o["name"] = name;
    if (group) o["group"] = QString::number(group);
    o["enabled"] = enabled;
    o["source"] = kSources[int(source)];
    switch (source) {
    case InteractionSource::Keyboard: o["key"] = key; break;
    case InteractionSource::Midi:
        if (!midiPort.isEmpty()) o["port"] = midiPort;
        o["channel"] = midiChannel;
        o["message"] = kMidi[int(midiMessage)];
        o["number"] = midiNumber;
        break;
    case InteractionSource::Osc:
        o["address"] = oscAddress;
        o["argument"] = oscArgument;
        break;
    case InteractionSource::Internal:
        o["source_layer"] = QString::number(sourceLayer);
        o["source_param"] = sourceParam;
        break;
    case InteractionSource::Audio:
        o["audio_low"] = audioLow;
        o["audio_high"] = audioHigh;
        o["audio_feature"] = int(audioFeature);
        o["audio_input"] = audioInput;
        if (audioFeature == AudioFeature::Hit) {
            o["audio_sensitivity"] = audioSensitivity;
            o["audio_gap"] = audioGap;
        }
        break;
    }
    o["layer"] = QString::number(layer);
    o["param"] = param;
    o["color_map_alpha"] = colorMapAlpha;
    if (!xyInputs.empty()) {
        Interaction y = xyInputs.front();
        y.xyInputs.clear();
        o["xy_y"] = y.toJson();
    }
    o["master"] = master;
    o["filters"] = filtersToJson(filters);
    if (!sharedFilters.empty()) o["shared_filters"] = filtersToJson(sharedFilters);
    o["takeover"] = takeover == Takeover::PickUp ? "pickup" : "jump";
    if (isAction()) {
        o["mode"] = QStringLiteral("action");
        QJsonArray conds;
        for (const InteractionCondition &c : conditions)
            conds.append(QJsonObject{{"op", kOps[std::clamp(int(c.op), 0, kConditionOpCount - 1)]}, {"a", c.a}, {"b", c.b}});
        o["conditions"] = conds;
        o["conditions_all"] = conditionsAll;
        auto list = [](const std::vector<InteractionConsequence> &v) {
            QJsonArray a;
            for (const InteractionConsequence &c : v) {
                if (c.sendsOsc())
                    a.append(QJsonObject{{"osc_destination", c.oscDestination}, {"osc_address", c.oscAddress},
                                         {"osc_arguments", c.oscArguments}});
                else
                    a.append(QJsonObject{{"layer", QString::number(c.layer)}, {"param", c.param}, {"value", c.value},
                                         {"time", c.time}, {"curve", c.curve}});
            }
            return a;
        };
        o["if_true"] = list(onTrue);
        o["if_false"] = list(onFalse);
    }
    return o;
}


Interaction Interaction::fromJson(const QJsonObject &o)
{
    Interaction it;
    it.id = o.value("id").toString().toULongLong();
    it.name = o.value("name").toString();
    it.group = o.value("group").toString().toULongLong();
    it.enabled = o.value("enabled").toBool(true);
    it.source = InteractionSource(keyIndex(kSources, o.value("source").toString(), 0));
    it.key = o.value("key").toString();
    it.midiPort = o.value("port").toString();
    it.midiChannel = std::clamp(o.value("channel").toInt(0), 0, 16);
    it.midiMessage = MidiMessageKind(keyIndex(kMidi, o.value("message").toString(), 0));
    it.midiNumber = std::clamp(o.value("number").toInt(0), 0, 127);
    it.oscAddress = o.value("address").toString();
    it.oscArgument = std::max(0, o.value("argument").toInt(0));
    it.sourceLayer = o.value("source_layer").toString().toULongLong();
    it.sourceParam = o.value("source_param").toString();
    auto bounded=[](double v,double fallback){return std::isfinite(v)?std::clamp(v,20.,20000.):fallback;};
    it.audioLow=bounded(o.value("audio_low").toDouble(20),20);
    it.audioHigh=bounded(o.value("audio_high").toDouble(20000),20000);
    if(it.audioLow>it.audioHigh)std::swap(it.audioLow,it.audioHigh);
    it.audioFeature=AudioFeature(std::clamp(o.value("audio_feature").toInt(),0,3));
    { const double s=o.value("audio_sensitivity").toDouble(0.5),g=o.value("audio_gap").toDouble(0.08);
      it.audioSensitivity=std::isfinite(s)?std::clamp(s,0.,1.):0.5; it.audioGap=std::isfinite(g)?std::clamp(g,0.04,2.):0.08; }
    it.audioInput=o.value("audio_input").toBool(false);
    it.layer = o.value("layer").toString().toULongLong();
    it.param = o.value("param").toString();
    it.colorMapAlpha = o.value("color_map_alpha").toBool(false);
    if (o.value("xy_y").isObject()) {
        auto y = o.value("xy_y").toObject();
        y.remove("xy_y"); // fixed arity; never parse nested interactions
        it.xyInputs.push_back(fromJson(y));
    }
    it.master = std::clamp(o.value("master").toDouble(1), 0.0, 1.0);
    it.filters = filtersFromJson(o);
    it.sharedFilters = filtersFromJson(QJsonObject{{"filters", o.value("shared_filters")}});
    it.takeover = o.value("takeover").toString() == QLatin1String("pickup") ? Takeover::PickUp : Takeover::Jump;
    // An XY mapping drives its point: never an Action
    it.mode = o.value("mode").toString() == QLatin1String("action") && it.xyInputs.empty() ? InteractionMode::Action
                                                                                           : InteractionMode::Mapping;
    for (const QJsonValue &v : o.value("conditions").toArray()) {
        const QJsonObject c = v.toObject();
        InteractionCondition cond;
        cond.op = ConditionOp(keyIndex(kOps, c.value("op").toString(), int(ConditionOp::GreaterOrEqual)));
        const double a = c.value("a").toDouble(0.5), b = c.value("b").toDouble(1);
        cond.a = std::isfinite(a) ? a : 0.5;
        cond.b = std::isfinite(b) ? b : 1;
        it.conditions.push_back(cond);
    }
    it.conditionsAll = o.value("conditions_all").toBool(true);
    auto list = [](const QJsonValue &v) {
        std::vector<InteractionConsequence> out;
        for (const QJsonValue &x : v.toArray()) {
            const QJsonObject c = x.toObject();
            InteractionConsequence q;
            q.layer = c.value("layer").toString().toULongLong();
            q.param = c.value("param").toString();
            const double value = c.value("value").toDouble(1);
            q.value = std::isfinite(value) ? value : 1;
            const double time = c.value("time").toDouble(0);
            q.time = std::isfinite(time) ? std::clamp(time, 0.0, 600.0) : 0;
            q.curve = std::clamp(c.value("curve").toInt(3), 0, 5);
            q.oscDestination = c.value("osc_destination").toString();
            q.oscAddress = c.value("osc_address").toString();
            q.oscArguments = c.value("osc_arguments").toArray();
            if (q.sendsOsc() || !q.param.isEmpty()) out.push_back(q);
        }
        return out;
    };
    it.onTrue = list(o.value("if_true"));
    it.onFalse = list(o.value("if_false"));
    return it;
}

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
void InteractionProcessor::input(const Interaction &it, double raw)
{
    (void)it;
    m_chain.input(raw);
}

std::optional<double> InteractionProcessor::step(const Interaction &it, double dt)
{
    m_views.resize(it.filters.size());
    for (size_t i = 0; i < it.filters.size(); ++i) {
        const InteractionStage &f = it.filters[i];
        m_views[i] = {f.kind, f.enabled, f.a, f.b, f.c, f.d, f.op, &f.curve.keys};
    }
    const std::optional<double> y = m_chain.step(m_views, dt);
    if (!y) return std::nullopt;
    m_out = *y * std::clamp(it.master, 0.0, 1.0);
    return m_out;
}

std::optional<double> InteractionTargetState::numberToWrite(const Interaction &it, double out, double current, double tolerance)
{
    const double v = out;
    if (it.takeover == Takeover::Jump) {
        caught = true;
    } else {
        // Moved by something else since it was written: let go until met again
        if (caught && written && std::abs(current - *written) > tolerance) caught = false;
        if (!caught) {
            const bool met = std::abs(v - current) <= tolerance || (prevOut && (*prevOut - current) * (v - current) <= 0);
            prevOut = v;
            if (!met) return std::nullopt;
            caught = true;
        }
    }
    prevOut = v;
    if (written && std::abs(*written - v) < 1e-12 && std::abs(current - v) < 1e-12) return std::nullopt; // already there
    written = v;
    return v;
}

std::optional<std::array<double, 4>> InteractionColorTargetState::toWrite(const Interaction &it, const std::array<double, 4> &v,
                                                                          const std::array<double, 4> &current, int channels,
                                                                          double tolerance)
{
    channels = std::clamp(channels, 1, 4);
    auto farFrom = [&](const std::array<double, 4> &a, const std::array<double, 4> &b) {
        for (int c = 0; c < channels; ++c)
            if (std::abs(a[size_t(c)] - b[size_t(c)]) > tolerance) return true;
        return false;
    };
    if (it.takeover == Takeover::Jump) {
        caught = true;
    } else {
        // Moved by something else since it was written: let go, every channel has to meet it again
        if (caught && written && farFrom(current, *written)) {
            caught = false;
            met = {{false, false, false, false}};
        }
        if (!caught) {
            bool all = true;
            for (int c = 0; c < channels; ++c) {
                const size_t k = size_t(c);
                const bool crossed = prevOut && ((*prevOut)[k] - current[k]) * (v[k] - current[k]) <= 0;
                met[k] = met[k] || std::abs(v[k] - current[k]) <= tolerance || crossed;
                all = all && met[k];
            }
            prevOut = v;
            if (!all) return std::nullopt;
            caught = true;
        }
    }
    prevOut = v;
    if (written && !farFrom(*written, v) && !farFrom(current, v)) {
        bool same = true; // within a hair: nothing to write
        for (int c = 0; c < channels; ++c) same = same && std::abs((*written)[size_t(c)] - v[size_t(c)]) < 1e-9 &&
                                                 std::abs(current[size_t(c)] - v[size_t(c)]) < 1e-9;
        if (same) return std::nullopt;
    }
    written = v;
    return v;
}
