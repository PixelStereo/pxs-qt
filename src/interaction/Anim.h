#pragma once
// Numbers drawn over time, played on their own clock: the Qt face of pxs-core's timelines (pxs/core/timeline/timeline.h), where the logic
// is (the keys and their curves, the waves, the transport). This adds what a timeline drives (a layer's number, by its id and path),
// its name and its filters, in Qt's types. Shared by the timelines of the show (Engine::Animation in the
// Engine's list) and by the animations of a layer's numbers (Layer::anims: one track each, on that layer).
//
// An animation has a duration (one pass), a loop mode and a speed, and tracks, each driving one number: a curve
// (keys over the duration, each eased towards the next one — the pattern repeats when it loops) or an oscillator
// (a wave of its own period, on the time played so that it goes on without a jump across the loops). Values are
// absolute. An event track drives no number: as the playhead reaches each of its keys, it fires an action (a Trigger
// parameter: Source › Rewind, Show › GO…) or sets a switch, a choice or a number to the key's value.

#include <pxs/core/timeline/timeline.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// The names the applications use, for pxs-core's own
using AnimWave = pxs::core::timeline::Wave;
inline constexpr int kAnimWaveCount = pxs::core::timeline::kWaveCount;
using AnimLoop = pxs::core::timeline::Loop;
using AnimState = pxs::core::timeline::State;
using AnimAction = pxs::core::timeline::Action;
inline constexpr int kAnimHold = pxs::core::timeline::kHold;     // a key's curve: holds its value until the next key
inline constexpr int kAnimBezier = pxs::core::timeline::kBezier; // a key's curve: a cubic Bézier (its out handle, the next key's in handle)
using AnimKey = pxs::core::timeline::Key;

// The easing of normalized time 0..1: 0 linear, 1 in, 2 out, 3 in-out, 4 in cubic, 5 out cubic
inline double easeCurve(double t, int curve) { return pxs::core::timeline::ease(t, curve); }

struct AnimTrack {
    quint64 layer = 0; // the layer driven (its id; 0: the composition)
    QString param;     // the number (see Engine::animatableParams): "opacity", "spatial/rotation", "fx/<fx>/param/<name>"…
    bool enabled = true;
    bool oscillator = false;
    std::vector<AnimKey> keys; // curve: sorted by time
    AnimWave wave = AnimWave::Sine;
    double period = 1, center = 0.5, amplitude = 0.5, phase = 0; // oscillator (phase: 0..1 of a period)
    QJsonArray dataFilters; // Same ordered filter definitions as interaction chains; runtime history lives in Engine.
    quint32 seed = 1;                                          // Random waves
    // Its oscillator, as pxs-core has it
    pxs::core::timeline::Oscillator oscillatorOf() const { return {wave, period, center, amplitude, phase, seed}; }
    // nan: no value (a curve without keys)
    double valueAt(double position, double played) const
    {
        return oscillator ? oscillatorOf().valueAt(played) : pxs::core::timeline::curveAt(keys, position, captured);
    }
    double keyValue(size_t k) const { return pxs::core::timeline::keyValue(keys, k, captured); } // the one captured for a "current value" key
    // The two handles of the segment from key k to key k + 1, as absolute points (time, value), the automatic ones
    // resolved and both kept within the segment's time
    void bezierHandles(size_t k, double *t1, double *v1, double *t2, double *v2) const
    {
        pxs::core::timeline::bezierHandles(keys, k, captured, t1, v1, t2, v2);
    }
    // Where it is (not saved): the value read when the animation started, for a first key "current value"
    double captured = std::numeric_limits<double>::quiet_NaN();
    std::array<double, 3> capturedRgb{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::quiet_NaN()};
    bool color = false; // drives param/r, param/g, param/b together; alpha is left alone
    bool event = false; // at each key's time: fires `param` (a Trigger), or sets it to the key's value (a switch: 0 / 1,
                        // a choice: its index, a number); the keys' curves are unused
    // A typed XY track owns two scalar curves/waves and one transport. Children
    // are always scalar, with canonical /x and /y targets; no nested groups.
    std::vector<AnimTrack> axes;
    bool isPoint() const { return axes.size() == 2; }
    std::array<double, 3> keyColor(size_t k) const { return pxs::core::timeline::keyColor(keys, k, capturedRgb); }
    std::array<double, 3> colorAt(double position) const { return pxs::core::timeline::colorAt(keys, position, capturedRgb); }
    QStringList drivenParams() const
    {
        if (isPoint()) {
            QStringList paths;
            for (int c = 0; c < 2; ++c) if (axes[size_t(c)].enabled) paths << param + (c ? "/y" : "/x");
            return paths;
        }
        return color ? QStringList{param + "/r", param + "/g", param + "/b"} : QStringList{param};
    }
    bool drives(const QString &path) const { return path == param || ((color || isPoint()) && drivenParams().contains(path)); }
};

// A timeline: its transport (pxs-core's: the duration, the loop, the passes, the speed, where it is) and its tracks
struct Animation : pxs::core::timeline::Transport {
    quint64 id = 0; // a timeline: stable (sequences refer to it), given by Engine::addAnimation
    QString name;
    std::vector<AnimTrack> tracks;
    // A layer's number: how its card is shown in the Anim tab (saved)
    bool pinned = false, folded = false;
};

// The keys of a track in time order (a point's two axes too, which take the track's layer and their own /x and /y)
void sortAnimKeys(AnimTrack &t);

// Saved: the keys of the waves ("sine", "triangle", "saw", "square", "random", "smooth_random") and of the loop modes ("once", "loop",
// "pingpong"); what drives a number, into a JSON object (its keys [t, v, curve, "current", {"in": [dt, dv], "out": [dt, dv]}], all but t
// and v only when they matter, its oscillator, its filters) and back (keys kept within `duration`); the transport's settings (duration,
// loop, repeat, speed). What a track drives (its layer and its number) is the application's to write beside them.
QStringList animWaveKeys();
QString animLoopKey(AnimLoop l);
AnimLoop animLoopFromKey(const QString &k);
void animTrackToJson(const AnimTrack &t, QJsonObject &o);
void animTrackFromJson(AnimTrack &t, const QJsonObject &o, double duration);
void animTransportToJson(const Animation &a, QJsonObject &o);
void animTransportFromJson(Animation &a, const QJsonObject &o);
