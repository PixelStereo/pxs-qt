#pragma once
// The list of a page's controls and what is done to it: keys, groups, adding, changing, removing, moving, the values the page sets,
// and the controls that are views of a parameter (Control::boundScope / boundParam). Pure functions on a std::vector<Control>: the
// application owns the list (and its lock, and its revision) and calls these; what it drives is behind a ParameterSource
// (NyxMap: the show's parameters; the OSCQuery explorer: a remote device's).
#include "Control.h"

#include <pxs/core/tree/parameter.h> // ParamInfo

#include <QHash>
#include <QJsonValue>
#include <QStringList>
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace pxs::controls {

using ParamInfo = core::tree::ParamInfo; // what a view of a parameter knows of it (pxs-core: std::string, no Qt)

constexpr int kMaxControls = 256;

// A readable key for a name, not among `taken` (index: the control's place, for a name with no letters)
using KeyFn = std::function<QString(const QString &name, const QStringList &taken, int index)>;
// What a value set on a control becomes elsewhere: its address within the control ("x", "p2/y", "attack"), and the value
using Feed = std::function<void(const QString &address, double value)>;

// What a view of a parameter reads and writes. `scope` is whose it is (a layer's id in NyxMap, 0: the application's own)
class ParameterSource
{
public:
    virtual ~ParameterSource() = default;
    virtual bool info(quint64 scope, const QString &path, ParamInfo *out) const = 0; // false: there is none there
    virtual QJsonValue value(quint64 scope, const QString &path) const = 0;          // Undefined: none
    virtual bool setNumber(quint64 scope, const QString &path, double v) = 0;
    virtual bool setJson(quint64 scope, const QString &path, const QJsonValue &v) = 0;
    virtual void touched(quint64 scope, const QString &path) { Q_UNUSED(scope); Q_UNUSED(path); } // after a write that took
};

// The list
const Control *find(const std::vector<Control> &list, quint64 id);
Control *find(std::vector<Control> &list, quint64 id);
void keyAll(std::vector<Control> &list, const KeyFn &key); // a key for each that has none
void tidyGroups(std::vector<Control> &list);              // groups within the list, spans and columns within 1..4

// What a control may be when it is added, or changed: its kind's own limits
void sanitizeNew(Control &c);
void sanitizeChange(Control &c, const Control &old);

// Edits. Each changes the list only when it returns true / a non-zero id
quint64 add(std::vector<Control> &list, quint64 &nextId, const Control &control, const KeyFn &key); // 0: full
struct Updated {
    bool found = false;
    bool keyChanged = false; // the name changed, the key with it
    QString oldKey, newKey;
};
Updated update(std::vector<Control> &list, quint64 id, const Control &change, const KeyFn &key);
bool remove(std::vector<Control> &list, quint64 id); // what a group held goes where the group was
bool move(std::vector<Control> &list, quint64 id, int to);
bool setLayouts(std::vector<Control> &list, const QHash<quint64, std::array<int, 4>> &places);

// The values the page sets (not for a view of a parameter, see setBound). False: not there, or not of that kind; each change is fed
bool setValue(std::vector<Control> &list, quint64 id, double v, const Feed &feed);
bool setPoint(std::vector<Control> &list, quint64 id, double x, double y, const Feed &feed);
bool setPoints(std::vector<Control> &list, quint64 id, const std::vector<double> &xy, const Feed &feed);
bool setMulti(std::vector<Control> &list, quint64 id, const std::vector<double> &values, const Feed &feed);
bool setAdsr(std::vector<Control> &list, quint64 id, const std::array<double, 4> &adsr, const Feed &feed);
bool setColor(Control &c, double r, double g, double b, const Feed &feed); // an own color

// Views of a parameter
// A Slider on a physics setting of another control (key: "friction", "attraction", "tension", "wall", "rate"; a ball of a Points control: "2.friction"):
// not added. Empty: the control has no such setting
std::optional<Control> forPhysics(const Control &target, const QString &key);
void fillPhysics(std::vector<Control> &list);                 // the value of each control that is a view of a physics setting
bool setPhysics(std::vector<Control> &list, quint64 id, double value); // false: not one, or its target is gone
std::optional<Control> forParameter(const ParameterSource &source, quint64 scope, const QString &path, quint64 group); // not added
// A pad that is a view of two numeric parameters (X then Y). It is 0..1 on both axes: each is mapped to its parameter's own range
std::optional<Control> forParameters(const ParameterSource &source, quint64 scope, const QString &pathX, const QString &pathY, quint64 group);
bool setBoundPoints(const Control &c, ParameterSource &source, const std::vector<double> &xy); // a Points control: each ball's two parameters
bool setBoundPoint(const Control &c, ParameterSource &source, double x, double y); // a pad's two parameters
// A macro whose feedback is one of its targets: its value is what that target holds, taken back through the target's range and curve
void fillFeedback(Control &c, const ParameterSource &source);
void fillBound(Control &c, const ParameterSource &source);                  // its value, range, step and choices, now
bool setBound(const Control &c, ParameterSource &source, double value);      // the parameter is what is set
// A macro's value (a Slider, a Toggle, a Button, or one axis of a Pad), sent to each target of that axis through its curve into its range. A button sends
// nothing when it is let go if `onRelease` is false (a scene: it sends what it was set to once). True when something was sent
bool setTargets(const Control &c, ParameterSource &source, double value, int axis = 0, bool onRelease = true);
bool setBoundColor(const Control &c, ParameterSource &source, double r, double g, double b);

} // namespace pxs::controls
