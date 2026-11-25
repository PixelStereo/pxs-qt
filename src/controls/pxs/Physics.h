#pragma once
// The physics of a control's numbers (Control::physics, a Points control's balls each have theirs), the logic is pxs-core's (pxs/core/physics/physics.h): one body per number, in
// the 0..1 of its range, that goes toward where the hand is (Interpolate) or is held to it by a spring and goes on by itself when the hand
// lets go (Mass-Spring: attracted to the finger, then thrown or pulled back to where it was: friction, tension, the bounce on the walls). Pure: Server.h steps it every 16 ms.
#include "Control.h"

#include <pxs/core/physics/physics.h>

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <vector>

namespace pxs::controls::physics {

// A control's body: pxs-core's, and when it last moved (Server.h)
struct Body : pxs::core::physics::Body {
    bool moving = false;
    qint64 movedAt = 0;
};

// Number i of a control: its range, its physics, its rest place
inline void rangeOf(const Control &c, size_t i, double *lo, double *hi)
{
    if (c.kind == Control::Kind::Points) {
        const auto r = c.pointRange(i / 2);
        *lo = i % 2 == 0 ? r[0] : r[2];
        *hi = i % 2 == 0 ? r[1] : r[3];
    } else {
        *lo = c.min;
        *hi = c.max;
    }
}
inline const Physics &of(const Control &c, size_t i) { return c.kind == Control::Kind::Points ? c.physicsOf(i / 2) : c.physics; }
inline std::vector<double> toUnits(const Control &c, const std::vector<double> &numbers)
{
    std::vector<double> out(numbers.size());
    for (size_t i = 0; i < numbers.size(); ++i) {
        double lo, hi;
        rangeOf(c, i, &lo, &hi);
        out[i] = pxs::core::physics::toUnit(numbers[i], lo, hi);
    }
    return out;
}
inline std::vector<double> fromUnits(const Control &c, const std::vector<double> &units)
{
    std::vector<double> out(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        double lo, hi;
        rangeOf(c, i, &lo, &hi);
        out[i] = pxs::core::physics::fromUnit(units[i], lo, hi);
    }
    return out;
}

// One step of dt seconds, by pxs-core: each number by its own physics (a Points control's balls each have theirs). True while something still
// moves (or is held by the hand and not there yet)
inline bool step(const Control &c, Body &b, double dt)
{
    std::vector<pxs::core::physics::Settings> each(b.pos.size());
    for (size_t i = 0; i < each.size(); ++i) each[i] = of(c, i);
    return pxs::core::physics::step(b, each.data(), each.size(), dt);
}

} // namespace pxs::controls::physics
