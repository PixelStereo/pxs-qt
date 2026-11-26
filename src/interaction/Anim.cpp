// The timelines' logic is pxs-core's (pxs/core/timeline/timeline.h); Anim.h is its Qt face. Here: how a timeline is saved (the keys of
// NyxMap's projects, which read back the same).
#include "Anim.h"
#include "Interactions.h"

#include <QJsonObject>
#include <cmath>

void sortAnimKeys(AnimTrack &t)
{
    std::stable_sort(t.keys.begin(), t.keys.end(), [](const AnimKey &a, const AnimKey &b) { return a.t < b.t; });
    if (t.isPoint()) for (int c = 0; c < 2; ++c) {
        auto &axis = t.axes[size_t(c)];
        axis.axes.clear();
        axis.color = false;
        axis.layer = t.layer;
        axis.param = t.param + (c ? "/y" : "/x");
        sortAnimKeys(axis);
    }
}

// ---------------------------------------------------------------------------
// Saved

static const char *const kWaveKeys[] = {"sine", "triangle", "saw", "square", "random", "smooth_random"};
static const char *const kLoopKeys[] = {"once", "loop", "pingpong"};

static int keyIndex(const char *const *keys, int n, const QString &k, int fallback)
{
    for (int i = 0; i < n; ++i)
        if (k == QLatin1String(keys[i])) return i;
    return fallback;
}

QStringList animWaveKeys()
{
    QStringList k;
    for (const char *x : kWaveKeys) k << QString::fromLatin1(x);
    return k;
}

QString animLoopKey(AnimLoop l) { return QString::fromLatin1(kLoopKeys[std::clamp(int(l), 0, 2)]); }
AnimLoop animLoopFromKey(const QString &k) { return AnimLoop(keyIndex(kLoopKeys, 3, k, 1)); }

// What drives a number (a timeline's track, or a layer's animation), into a JSON object: its keys
// [t, v, curve, "current", {"in": [dt, dv], "out": [dt, dv]}] (all but t and v when they matter) and its oscillator
void appendTrackRemap(AnimTrack &track, double inMin, double inMax, double outMin, double outMax)
{
    track.dataFilters.append(InteractionStage::remap(inMin, inMax, outMin, outMax).toJson());
}

void animTrackToJson(const AnimTrack &t, QJsonObject &o)
{
    if (t.unitCurve) o["unit_curve"] = true;
    if (!t.dataFilters.isEmpty()) o["data_filters"] = t.dataFilters;
    if (t.isPoint()) {
        o["type"] = QStringLiteral("point2d");
        if (!t.enabled) o["enable"] = false;
        QJsonArray axes;
        for (const auto &axis : t.axes) {
            QJsonObject value;
            animTrackToJson(axis, value);
            axes.append(value);
        }
        o["axes"] = axes;
        return;
    }
    if (t.event) {
        o["type"] = QStringLiteral("event");
        if (!t.enabled) o["enable"] = false;
        QJsonArray times, values;
        bool valued = false;
        for (const AnimKey &k : t.keys) {
            times.append(k.t);
            values.append(k.v);
            valued = valued || k.v != 0;
        }
        o["keys"] = times;
        if (valued) o["values"] = values; // what each flag sets (a switch, a choice, a number); none: an action
        return;
    }
    if (t.color) o["type"] = QStringLiteral("color");
    if (!t.enabled) o["enable"] = false;
    if (t.oscillator) {
        o["oscillator"] = QJsonObject{{"wave", QString::fromLatin1(kWaveKeys[std::clamp(int(t.wave), 0, kAnimWaveCount - 1)])},
                                      {"period", t.period},
                                      {"center", t.center},
                                      {"amplitude", t.amplitude},
                                      {"phase", t.phase},
                                      {"seed", double(t.seed)}};
    }
    QJsonArray keys;
    for (const AnimKey &k : t.keys) {
        QJsonArray kv{k.t};
        if (t.color) kv.append(QJsonArray{k.rgb[0], k.rgb[1], k.rgb[2]});
        else kv.append(k.v);
        QJsonObject handles;
        if (k.hasIn()) handles["in"] = QJsonArray{k.inDt, k.inDv};
        if (k.hasOut()) handles["out"] = QJsonArray{k.outDt, k.outDv};
        const bool more = k.isCurrentValue || !handles.isEmpty();
        if (k.curve == kAnimHold) kv.append(QStringLiteral("hold"));
        else if (k.curve == kAnimBezier) kv.append(QStringLiteral("bezier"));
        else if (k.curve != 3 || more) kv.append(k.curve);
        if (k.isCurrentValue) kv.append(QStringLiteral("current"));
        if (!handles.isEmpty()) kv.append(handles);
        keys.append(kv);
    }
    if (!keys.isEmpty()) o["keys"] = keys; // an oscillator keeps the curve it had, should it go back to it
}

void animTrackFromJson(AnimTrack &t, const QJsonObject &o, double duration)
{
    t.dataFilters = o.value("data_filters").toArray();
    t.unitCurve = o.value("unit_curve").toBool();
    t.axes.clear();
    if (o.value("type").toString() == QLatin1String("point2d")) {
        t.color = false;
        t.enabled = o.value("enable").toBool(true);
        const auto axes = o.value("axes").toArray();
        if (axes.size() == 2) for (const auto &value : axes) {
            auto child = value.toObject();
            child.remove("type"); // children are scalar; do not parse recursive groups
            child.remove("axes");
            AnimTrack axis;
            animTrackFromJson(axis, child, duration);
            t.axes.push_back(axis);
        }
        sortAnimKeys(t);
        return;
    }
    t.event = o.value("type").toString() == QLatin1String("event");
    if (t.event) {
        t.color = t.oscillator = false;
        t.enabled = o.value("enable").toBool(true);
        t.keys.clear();
        const QJsonArray times = o.value("keys").toArray(), values = o.value("values").toArray();
        for (int k = 0; k < times.size(); ++k)
            if (times[k].isDouble()) {
                const double v = values.at(k).toDouble();
                t.keys.push_back(AnimKey{std::clamp(times[k].toDouble(), 0.0, duration), std::isfinite(v) ? v : 0.0, 0});
            }
        sortAnimKeys(t);
        return;
    }
    t.color = o.value("type").toString() == QLatin1String("color");
    t.enabled = o.value("enable").toBool(true);
    if (o.contains("oscillator")) {
        const QJsonObject w = o.value("oscillator").toObject();
        t.oscillator = true;
        t.wave = AnimWave(keyIndex(kWaveKeys, kAnimWaveCount, w.value("wave").toString(), 0));
        t.period = std::clamp(w.value("period").toDouble(1), 0.01, 36000.0);
        t.center = w.value("center").toDouble(0.5);
        t.amplitude = w.value("amplitude").toDouble(0.5);
        t.phase = w.value("phase").toDouble(0);
        t.seed = quint32(std::clamp(w.value("seed").toDouble(1), 0.0, 4294967295.0));
    }
    t.keys.clear();
    for (const QJsonValue &kv : o.value("keys").toArray()) {
        const QJsonArray k = kv.toArray();
        if (k.size() < 2) continue;
        if (t.color) {
            const auto rgb = k[1].toArray();
            if (rgb.size() != 3 || !rgb[0].isDouble() || !rgb[1].isDouble() || !rgb[2].isDouble()) continue;
        } else if (!k[1].isDouble()) continue;
        AnimKey key;
        key.t = std::clamp(k[0].toDouble(), 0.0, duration);
        key.v = k[1].toDouble();
        if (t.color) {
            const auto rgb = k[1].toArray();
            for (int c = 0; c < 3; ++c) key.rgb[c] = std::clamp(rgb[c].toDouble(), 0.0, 1.0);
        }
        key.curve = 3;
        for (int i = 2; i < k.size(); ++i) {
            const QJsonValue x = k[i];
            if (x.isDouble()) key.curve = std::clamp(x.toInt(3), 0, 5);
            else if (x.toString() == "hold") key.curve = kAnimHold;
            else if (x.toString() == "bezier") key.curve = kAnimBezier;
            else if (x.toString() == "current") key.isCurrentValue = t.keys.empty(); // the first key only
            else if (x.isObject()) {
                const QJsonArray in = x.toObject().value("in").toArray(), out = x.toObject().value("out").toArray();
                if (in.size() == 2) key.inDt = std::min(0.0, in[0].toDouble()), key.inDv = in[1].toDouble();
                if (out.size() == 2) key.outDt = std::max(0.0, out[0].toDouble()), key.outDv = out[1].toDouble();
            }
        }
        t.keys.push_back(key);
    }
    sortAnimKeys(t);
    if (t.color) {
        t.oscillator = false;
        for (auto &key : t.keys) {
            if (key.curve == kAnimBezier) key.curve = 3;
        }
    }
}

// The transport settings shared by a timeline and a layer's animation
void animTransportToJson(const Animation &a, QJsonObject &o)
{
    o["duration"] = a.duration;
    o["loop"] = animLoopKey(a.loop);
    if (a.repeat > 0) o["repeat"] = a.repeat;
    if (a.speed != 1.0) o["speed"] = a.speed;
}

void animTransportFromJson(Animation &a, const QJsonObject &o)
{
    a.duration = std::clamp(o.value("duration").toDouble(4), pxs::core::timeline::Transport::kMinDuration, 36000.0);
    a.loop = animLoopFromKey(o.value("loop").toString());
    a.repeat = std::clamp(o.value("repeat").toInt(0), 0, 100000);
    a.speed = std::clamp(o.value("speed").toDouble(1.0), 0.0, 10.0);
}


QJsonObject animationDefinitionToJson(const Animation &animation)
{
    QJsonObject definition{{"id", double(animation.id)}, {"name", animation.name}};
    animTransportToJson(animation, definition);
    QJsonArray tracks;
    for (const auto &track : animation.tracks) {
        QJsonObject data;
        animTrackToJson(track, data);
        data["holder"] = double(track.layer);
        data["path"] = track.param;
        tracks.append(data);
    }
    definition["tracks"] = tracks;
    return definition;
}

std::optional<Animation> animationDefinitionFromJson(const QJsonObject &definition)
{
    const double identity = definition.value("id").toDouble();
    if (!(identity >= 1 && identity <= 9007199254740991.0 && std::floor(identity) == identity)
        || !definition.value("tracks").isArray()) return std::nullopt;
    Animation animation;
    animation.id = quint64(identity);
    animation.name = definition.value("name").toString();
    animTransportFromJson(animation, definition);
    for (const auto &value : definition.value("tracks").toArray()) {
        const auto data = value.toObject();
        const double holder = data.value("holder").toDouble(0);
        if (!data.value("path").isString() || !(holder >= 0 && holder <= 9007199254740991.0 && std::floor(holder) == holder))
            return std::nullopt;
        AnimTrack track;
        track.layer = quint64(holder);
        track.param = data.value("path").toString();
        animTrackFromJson(track, data, animation.duration);
        animation.tracks.push_back(std::move(track));
    }
    return animation;
}
