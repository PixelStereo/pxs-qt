#pragma once
// What the timeline editor asks of the application that holds the timelines: the numbers a track can drive on a holder (a layer, a
// device: its id) and where each is now. An application gives one object that answers (NyxMap's Engine does); the editor never
// reaches the rest of the application. A holder of id 0 is the application's own (the composition, the device).
#include "Anim.h"

#include <pxs/Parameter.h>

#include <QString>

#include <array>
#include <optional>
#include <utility>
#include <vector>

// Where a timeline is, for the editor (what it shows while it plays; its tracks and keys are the editor's own copy)
struct AnimationPlayback {
    quint64 id = 0;
    QString name;
    AnimState state = AnimState::Stopped;
    AnimLoop loop = AnimLoop::Once;
    double position = 0, duration = 0, clock = 0, played = 0, speed = 1, length = 0;
    bool backwards = false;
    int tracks = 0, pass = 1;
    std::vector<double> captured;
    std::vector<std::array<double, 3>> capturedRgb;
    std::vector<std::array<double, 2>> capturedXy;
};

class TimelineHost
{
public:
    virtual ~TimelineHost() = default;
    // The numbers of a holder a track can drive, with their ranges (ParamInfo::label "Category › Name" puts them in sub-menus)
    virtual std::vector<ParamInfo> animatableParams(quint64 holder) const = 0;
    virtual std::optional<std::pair<double, double>> externalAnimationRange(quint64, const QString &) const { return {}; }
    // Its value now; false: it has none at that address
    virtual bool animParamValue(quint64 holder, const QString &path, double *value) const = 0;
    // The colours (driven as path/r, path/g, path/b together) and the points (path/x, path/y), path without the component
    virtual std::vector<ParamInfo> animatableColors(quint64) const { return {}; }
    virtual std::vector<ParamInfo> animatablePoints(quint64) const { return {}; }
    virtual bool animColorValue(quint64, const QString &, std::array<double, 3> *) const { return false; }
    // Where a wave goes around this number: its center and amplitude (by default, the middle of its range and half of it)
    virtual void waveAround(quint64 holder, const QString &path, double *center, double *amplitude) const;
    // The declaration of what is at that address (an event track's target: an action, a switch, a choice, a number)
    virtual bool parameterInfo(quint64 holder, const QString &path, ParamInfo *info) const;
    // A filtered value's signal, as last computed: what came in, then the value after each filter (a timeline's id; a holder's
    // own animation: 0). false: none
    virtual bool animationFilterSignal(quint64, quint64, const QString &, double *, std::vector<double> *) const { return false; }
};
