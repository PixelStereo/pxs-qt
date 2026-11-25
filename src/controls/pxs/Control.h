#pragma once
// A control: a value the operator makes (a slider, a switch, a selector, a button, a color, an envelope, a row of sliders, a pad with
// several points) or a view of a parameter of the application it belongs to, shown on a page (ControlsPage.h) that a phone, a tablet or a
// window can lay out and play. The model is the same in every Pixel Stereo application: its kinds, its JSON, where it sits on the page.
// The application gives the page its controls (Server.h) and takes what is set on it.
//
// A control that is a view of a parameter (boundParam) has no value of its own: the application reads it from the parameter and sets the
// parameter itself. The others keep their value here.
#include <pxs/core/physics/physics.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace pxs::controls {

QJsonArray cleanFilters(const QJsonArray &filters); // Filters.h: every stage valid, its settings in range, a few of them


// What moves a control's numbers (Server.h runs it), as in Lemur: Interpolate (the value glides to where the hand is) or Mass-Spring (it is
// attracted to the finger and, when the finger lets go, goes on with the push it gave: thrown, it bounces on the walls; with a tension, it is
// pulled to where the finger last was and settles there after some bounces: friction slows it, the walls bounce it). All of it in the 0..1 of each number's range
// The physics itself is pxs-core's (pxs/core/physics/physics.h): this adds how a control saves it
struct Physics : pxs::core::physics::Settings {
    QJsonObject toJson() const
    {
        return QJsonObject{{"mode", mode}, {"friction", friction}, {"attraction", attraction}, {"tension", tension}, {"wall", wall}, {"rate", rate}};
    }
    void apply(const QJsonObject &p)
    {
        namespace core = pxs::core::physics;
        auto number = [&](const char *key, double lo, double hi, double &into) {
            if (p.value(key).isDouble() && std::isfinite(p.value(key).toDouble())) into = std::clamp(p.value(key).toDouble(), lo, hi);
        };
        if (p.value("mode").isDouble()) mode = std::clamp(p.value("mode").toInt(), int(None), int(MassSpring));
        number("friction", 0, core::kFrictionMax, friction);
        number("attraction", 0, core::kAttractionMax, attraction);
        number("tension", 0, core::kTensionMax, tension);
        number("wall", 0, core::kWallMax, wall);
        number("rate", core::kRateMin, core::kRateMax, rate);
    }
};

// One of the parameters a control sends to when it is a macro (Control::targets): the control's value (0 to 1 of its own range) goes through a
// curve to this parameter's own range, `outMin` where the control is at its lowest and `outMax` at its highest (outMin above outMax: inverted).
// A Pad sends its X to the targets of axis 0 and its Y to those of axis 1
// What a tile's filters did to the last value set: the value that came in, and the value after each stage (0..1 of the tile's range)
struct FilterTrace {
    double in = 0;
    std::vector<double> stages;
};

struct Target {
    quint64 scope = 0;
    QString path;
    double outMin = 0, outMax = 1;
    double curve = 1; // 1 linear, above 1 fine at the low end, below 1 fine at the high end (the same as a slider's gesture)
    int axis = 0;
    QString label; // derived, not kept: where it is, for the page to show (the application tells)
};

// A ball of a Points control: where its X and Y go (OSC addresses on the control's device: empty, it is only on the page), the range of
// each, its name, its color and its physics
struct Ball {
    QString name, px, py, accent;
    double minX = 0, maxX = 1, minY = 0, maxY = 1;
    Physics physics;
    bool own = false; // the ball has a physics of its own, over the control's (which is for all its balls)
    static QString cleanAddress(const QString &a) { return a.isEmpty() || (a.startsWith(QLatin1Char('/')) && a.size() < 200) ? a : QString(); }
    QJsonObject toJson() const
    {
        return QJsonObject{{"name", name}, {"x", px}, {"y", py}, {"accent", accent}, {"minX", minX}, {"maxX", maxX}, {"minY", minY}, {"maxY", maxY}, {"own", own}, {"physics", physics.toJson()}};
    }
    void apply(const QJsonObject &o)
    {
        if (o.value("name").isString()) name = o.value("name").toString().left(30);
        if (o.value("x").isString()) px = cleanAddress(o.value("x").toString());
        if (o.value("y").isString()) py = cleanAddress(o.value("y").toString());
        if (o.contains("accent")) {
            const QString a = o.value("accent").toString();
            accent = a.size() == 7 && a[0] == QLatin1Char('#') ? a.toLower() : QString();
        }
        auto number = [&](const char *key, double &into) {
            if (o.value(key).isDouble() && std::isfinite(o.value(key).toDouble())) into = o.value(key).toDouble();
        };
        number("minX", minX), number("maxX", maxX), number("minY", minY), number("maxY", maxY);
        if (!(maxX > minX)) maxX = minX + 1;
        if (!(maxY > minY)) maxY = minY + 1;
        if (o.value("own").isBool()) own = o.value("own").toBool();
        if (o.value("physics").isObject()) physics.apply(o.value("physics").toObject());
    }
};

struct Control {
    enum class Kind { Slider = 0, Toggle, Choice, Button, Color, Pad, Group, Adsr, Multi, Points };
    static constexpr int kKindCount = 10;
    static constexpr int kGridColumns = 12, kGridRowsMax = 400; // the remote page's grid
    quint64 id = 0;
    QString name = QStringLiteral("Control");
    QString key; // its address, control/<key>: derived from the name and unique among the controls
    Kind kind = Kind::Slider;
    quint64 group = 0; // the Group that holds it (0: the page)
    int span = 1;      // the columns it takes in its group (1..4)
    // Its place on the remote page (a 12-column grid of square cells; the page is its top-level Group, or the first page for
    // what is not in one): x, y, w, h in cells. w = 0: not placed yet, the page flows it where it fits
    int gx = 0, gy = 0, gw = 0, gh = 0;
    // How the remote page draws it (the tile's own settings): a widget among those of its kind ("": the default one), the curve
    // of its gesture (a Slider: 1 linear, above 1 fine at the low end, below 1 fine at the high end) and its accent color ("#rrggbb")
    QString widget, accent;
    double curve = 1;
    // The filters of what a Slider or a Toggle sends (Filters.h): the stages of an interaction's chain, as JSON, in their order. Empty: none
    QJsonArray filters;
    Physics physics; // Slider, Pad, Multi: what moves its numbers (a Points control has one for each ball)
    // A macro: a Slider, a Toggle, a Button or a Pad that has a value of its own and sends it to several parameters, each in its own range and
    // with its own curve. Not a view: nothing is read back from the parameters
    std::vector<Target> targets;
    int feedback = -1; // a macro: which of its targets gives the value the tile shows (the application reads it back); -1: none, the tile keeps its own
    static constexpr int kTargetsMax = 32;
    // A view of a parameter: where it is (the application's own scope: a layer in NyxMap, a device in an explorer; 0: the application's
    // own) and its address there. The control has no value of its own: it shows the parameter and sets it, so that a change made anywhere
    // shows on the page too
    quint64 boundScope = 0;
    QString boundParam;
    QString boundParam2; // a Pad that is a view of two parameters: the second one (its Y); the pad is 0..1 on both axes, each mapped to its parameter's own range
    // A Slider that is a view of a physics setting of another control (friction, attraction, tension, wall, rate; a ball of a Points control:
    // "2.friction"): it has no value of its own, it shows the setting and sets it
    quint64 physTarget = 0;
    QString physKey;
    bool canReset = false; // derived, not kept: what it is a view of announces a DEFAULT_VALUE (a reset is writing it, the application does); the tile shows a reset button
    bool ownRange = false; // a view of a parameter whose range (Min, Max, Step) is the tile's own, set by the operator: the parameter's is only a limit
    bool readOnly = false; // what it shows cannot be set (a parameter that is only read): drawn with any widget, never moved
    bool signal = false; // what it shows is a signal the device emits (no value of its own): shown, never set
    QString boundLabel;  // where it comes from, for the page to show on the tile ("Device:/layer.1/opacity")
    int columns = 2;   // Group: how many columns it lays its controls out in (1..4)
    double min = 0, max = 1, step = 0.01; // Slider, Pad (both axes)
    QString unit;                         // Slider: " %", " Hz", " s"
    double def = 0;                       // Slider, Pad: where a reset goes
    QStringList choices;                  // Choice: its names (also its keys: renaming one renames what was set)
    // Its value: a slider's number, a switch's 0 / 1, a choice's index, a button's 0 / 1 (1 while it is held down)
    double value = 0;
    double value2 = 0; // Pad: Y (value is X)
    std::array<double, 3> rgb{{1, 1, 1}}; // Color
    // Adsr: an envelope's four numbers, control/<key>/attack, decay, sustain (0..1) and release; the times in seconds up to `max`
    std::array<double, 4> adsr{{0.05, 0.2, 0.7, 0.4}};
    // Multi: a row of sliders in one control (2..32), each of the range min..max; control/<key>/1, /2…
    int count = 8;
    std::vector<double> multi;
    static constexpr int kMultiMax = 32;
    // Points: a pad with several points (1..16), each an X and a Y within min..max; control/<key>/p1/x, /p1/y, /p2/x…; `count` is how many
    std::vector<std::array<double, 2>> points;
    // Points, from an application that gives them each a place of their own (an explorer): one ball for each point, with its addresses, range,
    // color and physics. Empty: the points are plain, all in min..max
    std::vector<Ball> balls;
    static constexpr int kPointsMax = 16;
    static constexpr const char *const kAdsrKeys[4] = {"attack", "decay", "sustain", "release"};

    // What a remote page may ask for: a short word, a color in #rrggbb; anything else is the default
    static QString cleanWidget(const QString &w)
    {
        if (w.size() > 12) return QString();
        for (const QChar ch : w)
            if (!ch.isLetter()) return QString();
        return w.toLower();
    }
    static QString cleanAccent(const QString &a)
    {
        if (a.size() != 7 || a[0] != QLatin1Char('#')) return QString();
        for (int i = 1; i < 7; ++i)
            if (!a[i].isDigit() && !(a[i].toLower() >= QLatin1Char('a') && a[i].toLower() <= QLatin1Char('f'))) return QString();
        return a.toLower();
    }
    // The numbers a parameter can be driven from: {label, path after control/<key> ("" : the control itself)}. Not the switches, the
    // buttons, the choices, the colors or the groups
    std::vector<std::pair<QString, QString>> numberSources() const
    {
        std::vector<std::pair<QString, QString>> out;
        switch (kind) {
        case Kind::Slider: out.push_back({name, QString()}); break;
        case Kind::Pad:
            out.push_back({name + QStringLiteral(" X"), QStringLiteral("x")});
            out.push_back({name + QStringLiteral(" Y"), QStringLiteral("y")});
            break;
        case Kind::Adsr:
            for (int i = 0; i < 4; ++i) out.push_back({QString::fromLatin1(kAdsrKeys[i]), QString::fromLatin1(kAdsrKeys[i])});
            break;
        case Kind::Multi:
            for (int i = 0; i < count; ++i) out.push_back({QStringLiteral("Slider %1").arg(i + 1), QString::number(i + 1)});
            break;
        case Kind::Points:
            for (int i = 0; i < count; ++i) {
                out.push_back({QStringLiteral("Point %1 X").arg(i + 1), QStringLiteral("p%1/x").arg(i + 1)});
                out.push_back({QStringLiteral("Point %1 Y").arg(i + 1), QStringLiteral("p%1/y").arg(i + 1)});
            }
            break;
        default: break;
        }
        return out;
    }
    // A view of parameters: a parameter, or a Points control (whose balls give theirs)
    bool macro() const { return !targets.empty(); }
    bool bound() const { return !boundParam.isEmpty() || (kind == Kind::Points && boundScope != 0); }
    QStringList boundPaths() const // every address this control reads and sets
    {
        QStringList out;
        for (const QString &p : {boundParam, boundParam2}) if (!p.isEmpty()) out << p;
        for (const Ball &b : balls) for (const QString &p : {b.px, b.py}) if (!p.isEmpty()) out << p;
        return out;
    }
    // The range of a point's X and Y (a ball has its own)
    std::array<double, 4> pointRange(size_t i) const { return i < balls.size() ? std::array<double, 4>{balls[i].minX, balls[i].maxX, balls[i].minY, balls[i].maxY} : std::array<double, 4>{min, max, min, max}; }
    const Physics &physicsOf(size_t point) const { return point < balls.size() && balls[point].own ? balls[point].physics : physics; } // a ball's own, or the control's
    bool physical() const { return kind == Kind::Slider || kind == Kind::Pad || kind == Kind::Multi || kind == Kind::Points; }
    bool moving() const // some of its numbers have a physics
    {
        if (kind == Kind::Points && !balls.empty()) { for (size_t i = 0; i < balls.size(); ++i) if (physicsOf(i).on()) return true; return false; }
        return physical() && physics.on();
    }
    static QString kindKey(Kind k)
    {
        static const char *const keys[] = {"slider", "toggle", "choice", "button", "color", "pad", "group", "adsr", "multi", "points"};
        return QString::fromLatin1(keys[std::clamp(int(k), 0, kKindCount - 1)]);
    }
    static Kind kindFromKey(const QString &s)
    {
        for (int k = 0; k < kKindCount; ++k)
            if (kindKey(Kind(k)) == s) return Kind(k);
        return Kind::Slider;
    }
    static QString kindName(Kind k)
    {
        static const char *const names[] = {"Slider", "Toggle", "Selector", "Button", "Color", "2D Slider", "Group", "ADSR", "Multi-slider", "Multi-point pad"};
        return QString::fromLatin1(names[std::clamp(int(k), 0, kKindCount - 1)]);
    }
    // The value kept within what the control takes
    double clamped(double v) const
    {
        if (!std::isfinite(v)) return value;
        switch (kind) {
        case Kind::Slider:
        case Kind::Pad: return std::clamp(v, std::min(min, max), std::max(min, max)); // (a pad's Y: the same range)
        case Kind::Toggle:
        case Kind::Button: return v >= 0.5 ? 1.0 : 0.0;
        case Kind::Choice: return choices.isEmpty() ? 0.0 : std::clamp(std::round(v), 0.0, double(choices.size() - 1));
        case Kind::Color:
        case Kind::Adsr:
        case Kind::Multi:
        case Kind::Points:
        case Kind::Group: return value;
        }
        return value;
    }
    QJsonObject toJson() const
    {
        QJsonObject o{{"id", QString::number(id)}, {"name", name}, {"key", key}, {"kind", kindKey(kind)}};
        if (group) o["group"] = QString::number(group);
        if (span != 1) o["span"] = span;
        if (!boundLabel.isEmpty() && physTarget) o["boundLabel"] = boundLabel;
        if (signal) o["signal"] = true;
        if (readOnly) o["readonly"] = true;
        if (ownRange) o["ownRange"] = true;
        if (bound()) {
            QJsonObject b{{"scope", QString::number(boundScope)}, {"param", boundParam}};
            if (!boundParam2.isEmpty()) b["param2"] = boundParam2;
            if (!boundLabel.isEmpty()) b["label"] = boundLabel;
            o["bind"] = b;
        }
        if (physTarget) o["physicsOf"] = QJsonObject{{"id", QString::number(physTarget)}, {"key", physKey}};
        if (!targets.empty()) {
            QJsonArray list;
            for (const Target &t : targets)
                list.append(QJsonObject{{"scope", QString::number(t.scope)}, {"path", t.path}, {"min", t.outMin}, {"max", t.outMax}, {"curve", t.curve}, {"axis", t.axis}});
            o["targets"] = list;
            if (feedback >= 0) o["feedback"] = feedback;
        }
        if (!widget.isEmpty()) o["widget"] = widget;
        if (!accent.isEmpty()) o["accent"] = accent;
        if (curve != 1) o["curve"] = curve;
        if (!filters.isEmpty()) o["filters"] = filters;
        if (physical()) o["physics"] = physics.toJson();
        if (gw > 0 && gh > 0) o["layout"] = QJsonObject{{"x", gx}, {"y", gy}, {"w", gw}, {"h", gh}};
        switch (kind) {
        case Kind::Group: o["columns"] = columns; break;
        case Kind::Pad:
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            o["x"] = value;
            o["y"] = value2;
            break;
        case Kind::Slider:
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            o["value"] = value;
            if (!unit.isEmpty()) o["unit"] = unit;
            break;
        case Kind::Toggle: o["value"] = value >= 0.5; break;
        case Kind::Button: break; // never saved down
        case Kind::Choice: {
            QJsonArray list;
            for (const QString &c : choices) list.append(c);
            o["choices"] = list;
            o["value"] = int(value);
            break;
        }
        case Kind::Color: o["rgb"] = QJsonArray{rgb[0], rgb[1], rgb[2]}; break;
        case Kind::Points: {
            o["count"] = count;
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            QJsonArray list;
            for (const auto &q : points) list.append(QJsonArray{q[0], q[1]});
            o["points"] = list;
            if (!balls.empty()) {
                QJsonArray bl;
                for (const Ball &b : balls) bl.append(b.toJson());
                o["balls"] = bl;
            }
            break;
        }
        case Kind::Multi: {
            o["count"] = count;
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            QJsonArray list;
            for (double v : multi) list.append(v);
            o["values"] = list;
            break;
        }
        case Kind::Adsr:
            o["max"] = max;
            o["adsr"] = QJsonArray{adsr[0], adsr[1], adsr[2], adsr[3]};
            break;
        }
        return o;
    }
    // What a page may change of a control (its tile's settings), kept within bounds: its name, widget, accent, and for the numbers their count,
    // range, step, unit and curve. Anything else it sends is ignored
    void applyProps(const QJsonObject &p)
    {
        auto number = [&](const char *key, double &into) {
            if (p.value(key).isDouble() && std::isfinite(p.value(key).toDouble())) into = p.value(key).toDouble();
        };
        if (p.value("name").isString()) name = p.value("name").toString().left(60);
        if (p.contains("widget")) widget = cleanWidget(p.value("widget").toString());
        if (p.contains("accent")) accent = cleanAccent(p.value("accent").toString());
        if (p.value("filters").isArray() && (kind == Kind::Slider || kind == Kind::Toggle)) filters = cleanFilters(p.value("filters").toArray());
        if (physical() && p.value("physics").isObject()) physics.apply(p.value("physics").toObject());
        if (!targets.empty() && p.value("targets").isArray()) { // by position: its range, its curve, its axis; one that is left out is removed
            std::vector<Target> next;
            std::vector<int> kept; // the positions they had
            for (const QJsonValue &v : p.value("targets").toArray()) {
                const QJsonObject t = v.toObject();
                const int i = t.value("i").toInt(-1);
                if (i < 0 || size_t(i) >= targets.size()) continue;
                Target n = targets[size_t(i)];
                auto num = [&](const char *key, double lo, double hi, double &into) {
                    if (t.value(key).isDouble() && std::isfinite(t.value(key).toDouble())) into = std::clamp(t.value(key).toDouble(), lo, hi);
                };
                num("min", -1e9, 1e9, n.outMin);
                num("max", -1e9, 1e9, n.outMax);
                num("curve", 0.1, 10, n.curve);
                if (t.value("axis").isDouble()) n.axis = t.value("axis").toInt() == 1 ? 1 : 0;
                next.push_back(std::move(n));
                kept.push_back(i);
            }
            const int was = p.value("feedback").isDouble() ? p.value("feedback").toInt(-1) : feedback; // as the targets were numbered before
            int fb = -1;
            for (size_t k = 0; k < kept.size(); ++k)
                if (kept[k] == was) fb = int(k);
            targets = std::move(next);
            feedback = fb; // the same target, wherever it is now (-1: it was removed)
        }
        if (p.value("feedback").isDouble() && !p.value("targets").isArray() && !targets.empty()) {
            const int f = p.value("feedback").toInt(-1);
            feedback = f >= 0 && size_t(f) < targets.size() ? f : -1;
        }
        if (kind == Kind::Adsr) {
            number("max", max);
            max = std::clamp(max, 0.1, 60.0);
        }
        if (kind == Kind::Multi) {
            double n = count;
            number("count", n);
            count = std::clamp(int(n), 2, kMultiMax);
        }
        if (kind == Kind::Points && p.value("balls").isArray()) { // the balls, as the page has them: a new one is a copy of the last
            const QJsonArray list = p.value("balls").toArray();
            std::vector<Ball> next;
            for (int i = 0; i < list.size() && i < kPointsMax; ++i) {
                Ball b = size_t(i) < balls.size() ? balls[size_t(i)] : (balls.empty() ? Ball{} : balls.back());
                if (size_t(i) >= balls.size()) { b.px.clear(); b.py.clear(); b.name.clear(); if (balls.empty()) b.minX = b.minY = min, b.maxX = b.maxY = max; }
                b.apply(list.at(i).toObject());
                next.push_back(b);
            }
            if (!next.empty()) {
                balls = next;
                count = int(balls.size());
            }
        }
        if (kind == Kind::Points) {
            double n = count;
            number("count", n);
            count = std::clamp(int(n), 1, kPointsMax);
            number("min", min);
            number("max", max);
            if (!(max > min)) max = min + 1;
        }
        if (kind == Kind::Slider || kind == Kind::Multi) {
            if (kind == Kind::Slider && !boundParam.isEmpty() && (p.contains("min") || p.contains("max") || p.contains("step"))) ownRange = true; // its range is its own from now on
            if (p.value("ownRange").isBool()) ownRange = p.value("ownRange").toBool();               // false: the parameter's again
            number("min", min);
            number("max", max);
            number("step", step);
            number("default", def);
            if (p.value("unit").isString()) unit = p.value("unit").toString().left(8);
            number("curve", curve);
            curve = std::clamp(curve, 0.1, 10.0);
            step = std::clamp(step, 1e-6, std::max(1e-6, std::abs(max - min)));
        }
    }
    // The control as the page is told of it: its definition and its value now (a value is a number, or an array for a color, a pad, an
    // envelope, a row of sliders, the points of a pad)
    QJsonObject wireJson() const
    {
        QJsonObject o{{"id", QString::number(id)}, {"name", name}, {"key", key}, {"kind", kindKey(kind)}};
        if (group) o["group"] = QString::number(group);
        o["span"] = span;
        if ((bound() || physTarget) && !boundLabel.isEmpty()) o["dest"] = boundLabel;
        if (signal) o["signal"] = true;
        if (readOnly) o["ro"] = true;
        if (ownRange) o["ownRange"] = true;
        if (!targets.empty()) o["dest"] = QString("%1 target%2").arg(targets.size()).arg(targets.size() == 1 ? "" : "s");
        if (canReset) o["reset"] = true;
        if (!targets.empty()) {
            QJsonArray list;
            for (const Target &t : targets)
                list.append(QJsonObject{{"path", t.path}, {"label", t.label}, {"min", t.outMin}, {"max", t.outMax}, {"curve", t.curve}, {"axis", t.axis}});
            o["targets"] = list;
            if (feedback >= 0) o["feedback"] = feedback;
        }
        if (!widget.isEmpty()) o["widget"] = widget;
        if (!accent.isEmpty()) o["accent"] = accent;
        if (curve != 1) o["curve"] = curve;
        if (!filters.isEmpty()) o["filters"] = filters;
        if (physical()) o["physics"] = physics.toJson();
        if (gw > 0 && gh > 0) o["layout"] = QJsonObject{{"x", gx}, {"y", gy}, {"w", gw}, {"h", gh}};
        switch (kind) {
        case Kind::Group: o["columns"] = columns; break;
        case Kind::Points: {
            o["count"] = count;
            o["min"] = min;
            o["max"] = max;
            QJsonArray list;
            for (const auto &q : points) list.append(QJsonArray{q[0], q[1]});
            o["points"] = list;
            if (!balls.empty()) {
                QJsonArray bl;
                for (const Ball &b : balls) bl.append(b.toJson());
                o["balls"] = bl;
            }
            break;
        }
        case Kind::Multi: {
            o["count"] = count;
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            QJsonArray list;
            for (double v : multi) list.append(v);
            o["values"] = list;
            break;
        }
        case Kind::Adsr:
            o["max"] = max;
            o["adsr"] = QJsonArray{adsr[0], adsr[1], adsr[2], adsr[3]};
            break;
        case Kind::Pad:
            o["min"] = min;
            o["max"] = max;
            o["x"] = value;
            o["y"] = value2;
            break;
        case Kind::Slider:
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["unit"] = unit;
            o["value"] = value;
            break;
        case Kind::Choice: o["choices"] = QJsonArray::fromStringList(choices); [[fallthrough]];
        case Kind::Toggle:
        case Kind::Button: o["value"] = value; break;
        case Kind::Color: o["rgb"] = QJsonArray{rgb[0], rgb[1], rgb[2]}; break;
        }
        return o;
    }
    // Its values as numbers, to see what changed
    std::vector<double> numbers() const
    {
        switch (kind) {
        case Kind::Color: return {rgb[0], rgb[1], rgb[2]};
        case Kind::Pad: return {value, value2};
        case Kind::Adsr: return {adsr[0], adsr[1], adsr[2], adsr[3]};
        case Kind::Multi: return multi;
        case Kind::Points: {
            std::vector<double> flat;
            for (const auto &q : points) flat.push_back(q[0]), flat.push_back(q[1]);
            return flat;
        }
        default: return {value};
        }
    }

    static Control fromJson(const QJsonObject &o)
    {
        Control c;
        c.id = o.value("id").toString().toULongLong();
        c.name = o.value("name").toString(QStringLiteral("Control"));
        c.key = o.value("key").toString();
        c.kind = kindFromKey(o.value("kind").toString());
        c.group = o.value("group").toString().toULongLong();
        c.span = std::clamp(o.value("span").toInt(1), 1, 4);
        c.signal = o.value("signal").toBool();
        c.readOnly = o.value("readonly").toBool();
        c.ownRange = o.value("ownRange").toBool();
        if (const QJsonObject b = o.value("bind").toObject(); !b.value("param").toString().isEmpty() || (c.kind == Kind::Points && b.contains("scope"))) {
            c.boundScope = b.value("scope").toString().toULongLong();
            c.boundParam = b.value("param").toString();
            c.boundParam2 = b.value("param2").toString();
            c.boundLabel = b.value("label").toString().left(160);
        }
        for (const QJsonValue &v : o.value("targets").toArray()) {
            const QJsonObject t = v.toObject();
            if (c.targets.size() >= size_t(kTargetsMax) || t.value("path").toString().isEmpty()) continue;
            Target n;
            n.scope = t.value("scope").toString().toULongLong();
            n.path = t.value("path").toString();
            n.outMin = t.value("min").toDouble(0);
            n.outMax = t.value("max").toDouble(1);
            n.curve = std::clamp(t.value("curve").toDouble(1), 0.1, 10.0);
            n.axis = t.value("axis").toInt(0) == 1 ? 1 : 0;
            c.targets.push_back(std::move(n));
        }
        if (const int f = o.value("feedback").toInt(-1); f >= 0 && size_t(f) < c.targets.size()) c.feedback = f;
        if (const QJsonObject po = o.value("physicsOf").toObject(); !po.isEmpty()) {
            c.physTarget = po.value("id").toString().toULongLong();
            c.physKey = po.value("key").toString().left(24);
            c.boundLabel = o.value("boundLabel").toString();
        }
        c.widget = cleanWidget(o.value("widget").toString());
        c.accent = cleanAccent(o.value("accent").toString());
        if (c.kind == Kind::Slider || c.kind == Kind::Toggle) c.filters = cleanFilters(o.value("filters").toArray());
        c.curve = std::isfinite(o.value("curve").toDouble(1)) ? std::clamp(o.value("curve").toDouble(1), 0.1, 10.0) : 1.0;
        if (const QJsonObject ph = o.value("physics").toObject(); !ph.isEmpty()) c.physics.apply(ph);
        if (const QJsonObject l = o.value("layout").toObject(); l.value("w").toInt() > 0 && l.value("h").toInt() > 0) {
            c.gw = std::clamp(l.value("w").toInt(), 1, kGridColumns);
            c.gh = std::clamp(l.value("h").toInt(), 1, kGridRowsMax);
            c.gx = std::clamp(l.value("x").toInt(), 0, kGridColumns - c.gw);
            c.gy = std::clamp(l.value("y").toInt(), 0, kGridRowsMax);
        }
        auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
        switch (c.kind) {
        case Kind::Group: c.columns = std::clamp(o.value("columns").toInt(2), 1, 4); break;
        case Kind::Pad:
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.def = std::clamp(finite(o.value("default").toDouble(c.min), c.min), c.min, c.max);
            c.value = std::clamp(finite(o.value("x").toDouble(c.def), c.def), c.min, c.max);
            c.value2 = std::clamp(finite(o.value("y").toDouble(c.def), c.def), c.min, c.max);
            break;
        case Kind::Slider:
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.unit = o.value("unit").toString();
            c.def = std::clamp(finite(o.value("default").toDouble(c.min), c.min), c.min, c.max);
            c.value = std::clamp(finite(o.value("value").toDouble(c.def), c.def), c.min, c.max);
            break;
        case Kind::Toggle: c.value = o.value("value").toBool() ? 1.0 : 0.0; break;
        case Kind::Button: break;
        case Kind::Points: {
            c.count = std::clamp(o.value("count").toInt(3), 1, kPointsMax);
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.def = std::clamp(finite(o.value("default").toDouble((c.min + c.max) / 2), (c.min + c.max) / 2), c.min, c.max);
            const QJsonArray a = o.value("points").toArray();
            c.points.assign(size_t(c.count), {c.def, c.def});
            const QJsonArray bl = o.value("balls").toArray();
            for (int i = 0; i < c.count && i < bl.size(); ++i) {
                Ball b;
                b.minX = b.minY = c.min, b.maxX = b.maxY = c.max;
                b.apply(bl.at(i).toObject());
                c.balls.push_back(b);
            }
            if (!c.balls.empty()) c.balls.resize(size_t(c.count), c.balls.back());
            for (int i = 0; i < c.count; ++i) {
                const QJsonArray q = a.at(i).toArray();
                const auto r = c.pointRange(size_t(i));
                c.points[size_t(i)] = {std::clamp(finite(q.at(0).toDouble((r[0] + r[1]) / 2), c.def), r[0], r[1]), std::clamp(finite(q.at(1).toDouble((r[2] + r[3]) / 2), c.def), r[2], r[3])};
            }
            break;
        }
        case Kind::Multi: {
            c.count = std::clamp(o.value("count").toInt(8), 2, kMultiMax);
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.def = std::clamp(finite(o.value("default").toDouble(c.min), c.min), c.min, c.max);
            const QJsonArray a = o.value("values").toArray();
            c.multi.assign(size_t(c.count), c.def);
            for (int i = 0; i < c.count && i < a.size(); ++i) c.multi[size_t(i)] = std::clamp(finite(a.at(i).toDouble(c.def), c.def), c.min, c.max);
            break;
        }
        case Kind::Adsr: {
            c.min = 0;
            c.max = std::clamp(finite(o.value("max").toDouble(2), 2), 0.1, 60.0);
            const QJsonArray a = o.value("adsr").toArray();
            static const double fallback[4] = {0.05, 0.2, 0.7, 0.4};
            for (int i = 0; i < 4; ++i) c.adsr[size_t(i)] = std::clamp(finite(a.at(i).toDouble(fallback[i]), fallback[i]), 0.0, i == 2 ? 1.0 : c.max);
            break;
        }
        case Kind::Choice:
            for (const QJsonValue &v : o.value("choices").toArray()) c.choices << v.toString();
            if (c.choices.isEmpty()) c.choices << QStringLiteral("A") << QStringLiteral("B");
            c.value = std::clamp(double(o.value("value").toInt(0)), 0.0, double(c.choices.size() - 1));
            break;
        case Kind::Color: {
            const QJsonArray a = o.value("rgb").toArray();
            for (int i = 0; i < 3; ++i) c.rgb[size_t(i)] = std::clamp(finite(a.at(i).toDouble(1), 1), 0.0, 1.0);
            break;
        }
        }
        return c;
    }
};

} // namespace pxs::controls
