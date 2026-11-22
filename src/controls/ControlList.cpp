#include "pxs/ControlList.h"

#include <algorithm>
#include <cmath>

namespace pxs::controls {

namespace {
// What pxs-core says as std::string is Qt's QString here (the controls' own types are Qt's)
QString qs(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
QStringList choicesOf(const ParamInfo &info)
{
    QStringList out;
    for (const std::string &key : info.choices) out << qs(key);
    return out;
}
} // namespace


using Kind = Control::Kind;

const Control *find(const std::vector<Control> &list, quint64 id)
{
    for (const Control &c : list)
        if (c.id == id) return &c;
    return nullptr;
}

Control *find(std::vector<Control> &list, quint64 id)
{
    for (Control &c : list)
        if (c.id == id) return &c;
    return nullptr;
}

void keyAll(std::vector<Control> &list, const KeyFn &key)
{
    QStringList taken;
    for (const Control &c : list)
        if (!c.key.isEmpty()) taken << c.key;
    int index = 0;
    for (Control &c : list) {
        ++index;
        if (!c.key.isEmpty()) continue;
        c.key = key(c.name, taken, index);
        taken << c.key;
    }
}

// A control's group must be a Group of the list that is not itself nor inside it
void tidyGroups(std::vector<Control> &list)
{
    for (Control &c : list) {
        c.span = std::clamp(c.span, 1, 4);
        c.columns = std::clamp(c.columns, 1, 4);
        const Control *g = c.group ? find(list, c.group) : nullptr;
        bool ok = g && g->kind == Kind::Group && g->id != c.id;
        for (int depth = 0; ok && g && g->group && depth < 64; ++depth) { // up the chain: it must not come back to this one
            if (g->group == c.id) ok = false;
            g = find(list, g->group);
        }
        if (!ok) c.group = 0;
    }
}

// The points and the bars: their count, and each within the range
static void tidyArrays(Control &c)
{
    if (c.kind == Kind::Points) {
        c.count = std::clamp(c.count, 1, Control::kPointsMax);
        if (!(c.max > c.min)) c.max = c.min + 1;
        c.def = std::clamp(c.def, c.min, c.max);
        if (!c.balls.empty()) {
            const size_t had = c.balls.size();
            c.balls.resize(size_t(c.count), c.balls.back());
            for (size_t i = had; i < c.balls.size(); ++i) c.balls[i].px.clear(), c.balls[i].py.clear(), c.balls[i].name.clear();
        }
        const size_t had = c.points.size();
        c.points.resize(size_t(c.count), {c.def, c.def});
        for (size_t i = 0; i < c.points.size(); ++i) {
            const auto r = c.pointRange(i);
            if (i >= had) c.points[i] = {(r[0] + r[1]) / 2, (r[2] + r[3]) / 2};
            c.points[i] = {std::clamp(c.points[i][0], r[0], r[1]), std::clamp(c.points[i][1], r[2], r[3])};
        }
    }
    if (c.kind == Kind::Multi) {
        c.count = std::clamp(c.count, 2, Control::kMultiMax);
        if (!(c.max > c.min)) c.max = c.min + 1;
        c.def = std::clamp(c.def, c.min, c.max);
        c.multi.resize(size_t(c.count), c.def);
        for (double &v : c.multi) v = std::clamp(v, c.min, c.max);
    }
}

void sanitizeNew(Control &c)
{
    if (c.name.trimmed().isEmpty()) c.name = Control::kindName(c.kind);
    c.value = c.kind == Kind::Slider ? std::clamp(c.value, std::min(c.min, c.max), std::max(c.min, c.max)) : c.clamped(c.value);
    if (c.kind == Kind::Pad) {
        c.value2 = c.clamped(c.value2);
        c.def = std::clamp(c.def, c.min, c.max);
    }
    if (c.kind == Kind::Choice && c.choices.isEmpty()) c.choices = {QStringLiteral("A"), QStringLiteral("B")};
    if (c.kind == Kind::Adsr) {
        c.min = 0;
        c.max = std::clamp(c.max > 1 ? c.max : 2.0, 0.1, 60.0); // the longest of its times
    }
    tidyArrays(c);
}

void sanitizeChange(Control &c, const Control &old)
{
    if (c.kind == Kind::Slider || c.kind == Kind::Pad) {
        if (!(c.max > c.min)) c.max = c.min + 1;
        c.def = std::clamp(c.def, c.min, c.max);
    }
    if (c.kind == Kind::Choice && c.choices.isEmpty()) c.choices = old.choices;
    c.value = c.kind == old.kind ? c.clamped(c.value) : (c.kind == Kind::Slider || c.kind == Kind::Pad ? c.def : 0.0);
    c.value2 = c.kind == Kind::Pad ? (c.kind == old.kind ? c.clamped(c.value2) : c.def) : 0.0;
    tidyArrays(c);
    if (c.kind == Kind::Adsr) {
        c.min = 0;
        c.max = std::clamp(c.max, 0.1, 60.0);
        for (int i = 0; i < 4; ++i) c.adsr[size_t(i)] = std::clamp(c.adsr[size_t(i)], 0.0, i == 2 ? 1.0 : c.max);
    }
}

quint64 add(std::vector<Control> &list, quint64 &nextId, const Control &control, const KeyFn &key)
{
    if (int(list.size()) >= kMaxControls) return 0;
    Control c = control;
    c.id = nextId++;
    c.key.clear();
    sanitizeNew(c);
    const quint64 id = c.id;
    list.push_back(std::move(c));
    keyAll(list, key);
    tidyGroups(list);
    return id;
}

Updated update(std::vector<Control> &list, quint64 id, const Control &change, const KeyFn &key)
{
    Updated out;
    auto it = std::find_if(list.begin(), list.end(), [id](const Control &c) { return c.id == id; });
    if (it == list.end()) return out;
    out.found = true;
    Control c = change;
    c.id = id;
    c.key = it->key;
    c.name = c.name.trimmed();
    if (c.name.isEmpty()) c.name = it->name;
    if (c.name != it->name) {
        QStringList taken;
        for (const Control &other : list)
            if (other.id != id) taken << other.key;
        c.key = key(c.name, taken, int(it - list.begin()) + 1);
        out.keyChanged = c.key != it->key;
        out.oldKey = it->key;
        out.newKey = c.key;
    }
    sanitizeChange(c, *it);
    *it = c;
    tidyGroups(list);
    return out;
}

bool remove(std::vector<Control> &list, quint64 id)
{
    auto it = std::find_if(list.begin(), list.end(), [id](const Control &c) { return c.id == id; });
    if (it == list.end()) return false;
    const quint64 parent = it->group;
    list.erase(it);
    for (Control &c : list)
        if (c.group == id) c.group = parent;
    return true;
}

bool move(std::vector<Control> &list, quint64 id, int to)
{
    auto it = std::find_if(list.begin(), list.end(), [id](const Control &c) { return c.id == id; });
    if (it == list.end()) return false;
    const int from = int(it - list.begin());
    to = std::clamp(to, 0, int(list.size()) - 1);
    if (from == to) return true;
    Control c = *it;
    list.erase(it);
    list.insert(list.begin() + to, std::move(c));
    return true;
}

bool setLayouts(std::vector<Control> &list, const QHash<quint64, std::array<int, 4>> &places)
{
    bool changed = false;
    for (Control &c : list) {
        const auto it = places.constFind(c.id);
        if (it == places.cend()) continue;
        const int w = std::clamp(it.value()[2], 1, Control::kGridColumns), h = std::clamp(it.value()[3], 1, Control::kGridRowsMax);
        const int x = std::clamp(it.value()[0], 0, Control::kGridColumns - w), y = std::clamp(it.value()[1], 0, Control::kGridRowsMax);
        if (c.gx == x && c.gy == y && c.gw == w && c.gh == h) continue;
        c.gx = x;
        c.gy = y;
        c.gw = w;
        c.gh = h;
        changed = true;
    }
    return changed;
}

static const QString kSlash = QStringLiteral("/");

bool setValue(std::vector<Control> &list, quint64 id, double value, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind == Kind::Color || c->kind == Kind::Group || !std::isfinite(value)) return false;
    const double next = c->clamped(value);
    if (c->value != next) {
        c->value = next;
        if (feed) feed(c->kind == Kind::Pad ? QStringLiteral("x") : QString(), next);
    }
    return true;
}

bool setPoint(std::vector<Control> &list, quint64 id, double x, double y, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Pad) return false;
    const double v[2] = {x, y};
    for (int axis = 0; axis < 2; ++axis) {
        if (!std::isfinite(v[axis])) continue;
        const double next = c->clamped(v[axis]);
        double &field = axis == 0 ? c->value : c->value2;
        if (field == next) continue;
        field = next;
        if (feed) feed(axis == 0 ? QStringLiteral("x") : QStringLiteral("y"), next);
    }
    return true;
}

bool setPoints(std::vector<Control> &list, quint64 id, const std::vector<double> &xy, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Points) return false;
    if (int(c->points.size()) != c->count) c->points.resize(size_t(c->count), {c->def, c->def});
    for (size_t i = 0; i < xy.size() && i < c->points.size() * 2; ++i) {
        if (!std::isfinite(xy[i])) continue;
        const auto r = c->pointRange(i / 2);
        const double next = i % 2 == 0 ? std::clamp(xy[i], r[0], r[1]) : std::clamp(xy[i], r[2], r[3]);
        double &field = c->points[i / 2][i % 2];
        if (field == next) continue;
        field = next;
        if (feed) feed(QStringLiteral("p%1/%2").arg(i / 2 + 1).arg(i % 2 == 0 ? 'x' : 'y'), next);
    }
    return true;
}

bool setMulti(std::vector<Control> &list, quint64 id, const std::vector<double> &values, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Multi) return false;
    if (int(c->multi.size()) != c->count) c->multi.resize(size_t(c->count), c->def);
    for (size_t i = 0; i < values.size() && i < c->multi.size(); ++i) {
        if (!std::isfinite(values[i])) continue;
        const double next = std::clamp(values[i], std::min(c->min, c->max), std::max(c->min, c->max));
        if (c->multi[i] == next) continue;
        c->multi[i] = next;
        if (feed) feed(QString::number(i + 1), next);
    }
    return true;
}

bool setAdsr(std::vector<Control> &list, quint64 id, const std::array<double, 4> &adsr, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Adsr) return false;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(adsr[size_t(i)])) continue;
        const double next = std::clamp(adsr[size_t(i)], 0.0, i == 2 ? 1.0 : std::max(0.1, c->max));
        double &field = c->adsr[size_t(i)];
        if (field == next) continue;
        field = next;
        if (feed) feed(QLatin1String(Control::kAdsrKeys[i]), next);
    }
    return true;
}

bool setColor(Control &c, double r, double g, double b, const Feed &feed)
{
    const double v[3] = {r, g, b};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(v[i])) continue;
        const double next = std::clamp(v[i], 0.0, 1.0);
        if (c.rgb[size_t(i)] == next) continue;
        c.rgb[size_t(i)] = next;
        if (feed) feed(QString(QLatin1Char("rgb"[i])), next);
    }
    return true;
}

// A physics setting of a control by its key, with its range: null when there is none
static double *physicsField(Control &c, const QString &key, double *lo, double *hi, QString *label)
{
    QString name = key;
    Physics *p = &c.physics;
    QString who;
    const int dot = key.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        const int ball = key.left(dot).toInt() - 1;
        if (c.kind != Kind::Points || ball < 0 || size_t(ball) >= c.balls.size()) return nullptr;
        p = &c.balls[size_t(ball)].physics;
        name = key.mid(dot + 1);
        who = QStringLiteral(" ball %1").arg(ball + 1);
    }
    struct Field { const char *key, *label; double Physics::*member; double lo, hi; };
    static const Field fields[] = {{"friction", "friction", &Physics::friction, 0, 30}, {"attraction", "attraction", &Physics::attraction, 0, 400},
                                   {"tension", "tension", &Physics::tension, 0, 400}, {"wall", "wall bounce", &Physics::wall, 0, 1}, {"rate", "speed", &Physics::rate, 0.5, 60}};
    for (const Field &f : fields)
        if (name == QLatin1String(f.key)) {
            *lo = f.lo, *hi = f.hi;
            if (label) *label = QString::fromLatin1(f.label) + who;
            return &(p->*f.member);
        }
    return nullptr;
}

std::optional<Control> forPhysics(const Control &target, const QString &key)
{
    Control t = target;
    double lo, hi;
    QString label;
    const double *field = physicsField(t, key, &lo, &hi, &label);
    if (!field || !target.physical()) return std::nullopt;
    Control c;
    c.kind = Kind::Slider;
    c.group = target.group;
    c.name = label.left(1).toUpper() + label.mid(1); // the physics parameter only; a wire to its control shows in edit mode
    c.min = lo, c.max = hi, c.step = hi > 50 ? 1 : 0.01, c.def = *field, c.value = *field;
    c.physTarget = target.id;
    c.physKey = key;
    c.boundLabel = QStringLiteral("Physics: ") + target.name;
    return c;
}

void fillPhysics(std::vector<Control> &list)
{
    for (Control &c : list) {
        if (!c.physTarget) continue;
        Control *target = find(list, c.physTarget);
        double lo, hi;
        const double *field = target ? physicsField(*target, c.physKey, &lo, &hi, nullptr) : nullptr;
        if (field) c.value = std::clamp(*field, c.min, c.max);
    }
}

bool setPhysics(std::vector<Control> &list, quint64 id, double value)
{
    const Control *view = find(list, id);
    if (!view || !view->physTarget || !std::isfinite(value)) return false;
    Control *target = find(list, view->physTarget);
    double lo, hi;
    double *field = target ? physicsField(*target, view->physKey, &lo, &hi, nullptr) : nullptr;
    if (!field) return false;
    *field = std::clamp(value, lo, hi);
    return true;
}

std::optional<Control> forParameter(const ParameterSource &source, quint64 scope, const QString &path, quint64 group)
{
    ParamInfo info;
    if (!source.info(scope, path, &info) || info.type == ParamInfo::Type::Text) return std::nullopt;
    const bool writable = info.access & ParamInfo::Write;
    // What cannot be set can be seen: a number, a switch or a choice that is read only is a view (a meter, a lamp, a readout)
    if (!writable && (info.colorComponent >= 0 || info.type == ParamInfo::Type::Trigger)) return std::nullopt;
    QString target = path; // what the control drives: the parameter, or a whole color
    Control c;
    c.group = group;
    c.name = qs(info.name());
    const QJsonValue now = source.value(scope, path);
    if (info.colorComponent >= 0 && path.size() > 2 && path[path.size() - 2] == QLatin1Char('/')) { // a color's channel: the color
        target = path.left(path.size() - 2);
        c.kind = Kind::Color;
        c.name = qs(info.label).section(QStringLiteral(" › "), -1).section(QLatin1Char(' '), 0, -2).trimmed();
        for (int k = 0; k < 3; ++k) {
            const QJsonValue v = source.value(scope, target + QLatin1Char('/') + QLatin1Char("rgb"[k]));
            if (v.isDouble()) c.rgb[size_t(k)] = std::clamp(v.toDouble(), 0.0, 1.0);
        }
    } else if (info.type == ParamInfo::Type::Bool) {
        c.kind = Kind::Toggle;
        c.value = now.toBool() ? 1 : 0;
    } else if (info.type == ParamInfo::Type::Choice) {
        c.kind = Kind::Choice;
        c.choices = choicesOf(info);
        c.value = std::max(0, int(c.choices.indexOf(now.toString())));
    } else if (info.type == ParamInfo::Type::Trigger) {
        c.kind = Kind::Button;
    } else {
        c.kind = Kind::Slider;
        c.min = info.min;
        c.max = info.max > info.min ? info.max : info.min + 1;
        c.step = info.type == ParamInfo::Type::Int ? std::max(1.0, info.step) : std::max(1e-6, info.step);
        c.def = std::clamp(info.defaultValue.isNil() ? c.min : info.defaultValue.number().value_or(0.0), c.min, c.max);
        c.value = std::clamp(now.toDouble(c.def), c.min, c.max);
        const QString label = qs(info.label);
        const QString unit = label.section(QLatin1Char('('), -1);
        if (label.endsWith(QLatin1Char(')')) && unit.size() < 8) c.unit = QLatin1Char(' ') + unit.chopped(1); // "Rotation (°)" -> " °"
    }
    if (c.name.isEmpty()) c.name = qs(info.name());
    c.boundScope = scope;
    c.boundParam = target;
    c.signal = info.signal;
    c.readOnly = !writable;
    if (!writable) c.widget = c.kind == Kind::Slider ? QStringLiteral("meter") : c.kind == Kind::Toggle ? QStringLiteral("led") : QStringLiteral("readout");
    return c;
}

// A number of a pad's axis, 0..1 over its parameter's range
static bool axis(const ParameterSource &source, quint64 scope, const QString &path, ParamInfo *info, double *lo, double *hi)
{
    if (!source.info(scope, path, info) || !info->isNumber()) return false;
    *lo = info->min;
    *hi = info->max > info->min ? info->max : info->min + 1;
    return true;
}

std::optional<Control> forParameters(const ParameterSource &source, quint64 scope, const QString &pathX, const QString &pathY, quint64 group)
{
    ParamInfo x, y;
    double xlo, xhi, ylo, yhi;
    if (!axis(source, scope, pathX, &x, &xlo, &xhi) || !axis(source, scope, pathY, &y, &ylo, &yhi)) return std::nullopt;
    if (!(x.access & ParamInfo::Write) || !(y.access & ParamInfo::Write) || pathX == pathY) return std::nullopt;
    Control c;
    c.physics.tension = 0; // its balls are thrown
    c.kind = Kind::Points; // a pad of one ball, bound to the two parameters; balls can be added to it
    c.group = group;
    c.name = qs(x.name()) + QStringLiteral(" / ") + qs(y.name());
    c.count = 1;
    c.min = 0;
    c.max = 1;
    c.step = 0.001;
    c.def = 0.5;
    c.boundScope = scope;
    Ball ball;
    ball.px = pathX;
    ball.py = pathY;
    ball.minX = xlo, ball.maxX = xhi, ball.minY = ylo, ball.maxY = yhi;
    c.balls.push_back(ball);
    c.points = {{(xlo + xhi) / 2, (ylo + yhi) / 2}};
    fillBound(c, source);
    return c;
}

// A Points control that is bound: each ball's two addresses are set from its point, in the ball's own range
bool setBoundPoints(const Control &c, ParameterSource &source, const std::vector<double> &xy)
{
    bool any = false;
    for (size_t i = 0; i < c.balls.size() && i * 2 + 1 < xy.size(); ++i) {
        const Ball &b = c.balls[i];
        const auto r = c.pointRange(i);
        const double v[2] = {std::clamp(xy[i * 2], r[0], r[1]), std::clamp(xy[i * 2 + 1], r[2], r[3])};
        const QString *paths[2] = {&b.px, &b.py};
        for (int k = 0; k < 2; ++k) {
            if (paths[k]->isEmpty() || !std::isfinite(v[k])) continue;
            ParamInfo info;
            if (!source.info(c.boundScope, *paths[k], &info) || !(info.access & ParamInfo::Write)) continue;
            any |= source.setNumber(c.boundScope, *paths[k], v[k]);
            source.touched(c.boundScope, *paths[k]);
        }
    }
    return any;
}

bool setBoundPoint(const Control &c, ParameterSource &source, double x, double y)
{
    ParamInfo ix, iy;
    double xlo, xhi, ylo, yhi;
    if (c.kind != Kind::Pad || c.boundParam2.isEmpty() || !axis(source, c.boundScope, c.boundParam, &ix, &xlo, &xhi) ||
        !axis(source, c.boundScope, c.boundParam2, &iy, &ylo, &yhi))
        return false;
    bool any = false;
    if (std::isfinite(x) && (ix.access & ParamInfo::Write)) {
        any |= source.setNumber(c.boundScope, c.boundParam, xlo + std::clamp(x, 0.0, 1.0) * (xhi - xlo));
        source.touched(c.boundScope, c.boundParam);
    }
    if (std::isfinite(y) && (iy.access & ParamInfo::Write)) {
        any |= source.setNumber(c.boundScope, c.boundParam2, ylo + std::clamp(y, 0.0, 1.0) * (yhi - ylo));
        source.touched(c.boundScope, c.boundParam2);
    }
    return any;
}

void fillFeedback(Control &c, const ParameterSource &source)
{
    if (c.feedback < 0 || size_t(c.feedback) >= c.targets.size()) return;
    const Target &t = c.targets[size_t(c.feedback)];
    ParamInfo info;
    const QJsonValue v = source.value(t.scope, t.path);
    if (v.isUndefined() || v.isNull() || !source.info(t.scope, t.path, &info)) return;
    double out;
    if (v.isBool()) out = v.toBool() ? 1.0 : 0.0;
    else if (v.isDouble()) out = v.toDouble();
    else return; // a choice or a text: no number to show
    const double range = t.outMax - t.outMin;
    const double shaped = std::clamp(std::abs(range) > 1e-12 ? (out - t.outMin) / range : 0.0, 0.0, 1.0);
    const double n = std::pow(shaped, 1.0 / (t.curve > 0 ? t.curve : 1.0)); // the way back of what setTargets does
    const double span = c.max - c.min;
    const double shown = c.min + n * span;
    if (c.kind == Kind::Pad && t.axis == 1) c.value2 = std::clamp(shown, std::min(c.min, c.max), std::max(c.min, c.max));
    else if (c.kind == Kind::Toggle || c.kind == Kind::Button) c.value = n >= 0.5 ? 1.0 : 0.0;
    else c.value = std::clamp(shown, std::min(c.min, c.max), std::max(c.min, c.max));
}

void fillBound(Control &c, const ParameterSource &source)
{
    if (c.kind == Kind::Points && c.boundScope) { // the points are where the parameters are
        for (size_t i = 0; i < c.balls.size() && i < c.points.size(); ++i) {
            const Ball &b = c.balls[i];
            const auto r = c.pointRange(i);
            const QJsonValue vx = b.px.isEmpty() ? QJsonValue() : source.value(c.boundScope, b.px), vy = b.py.isEmpty() ? QJsonValue() : source.value(c.boundScope, b.py);
            if (vx.isDouble()) c.points[i][0] = std::clamp(vx.toDouble(), r[0], r[1]);
            if (vy.isDouble()) c.points[i][1] = std::clamp(vy.toDouble(), r[2], r[3]);
        }
        return;
    }
    if (c.kind == Kind::Pad && !c.boundParam2.isEmpty()) {
        ParamInfo ix, iy;
        double xlo, xhi, ylo, yhi;
        if (!axis(source, c.boundScope, c.boundParam, &ix, &xlo, &xhi) || !axis(source, c.boundScope, c.boundParam2, &iy, &ylo, &yhi)) return;
        c.min = 0;
        c.max = 1;
        const QJsonValue vx = source.value(c.boundScope, c.boundParam), vy = source.value(c.boundScope, c.boundParam2);
        if (vx.isDouble()) c.value = std::clamp((vx.toDouble() - xlo) / (xhi - xlo), 0.0, 1.0);
        if (vy.isDouble()) c.value2 = std::clamp((vy.toDouble() - ylo) / (yhi - ylo), 0.0, 1.0);
        return;
    }
    if (c.kind == Kind::Color) {
        for (int k = 0; k < 3; ++k) {
            const QJsonValue v = source.value(c.boundScope, c.boundParam + QLatin1Char('/') + QLatin1Char("rgb"[k]));
            if (v.isDouble()) c.rgb[size_t(k)] = std::clamp(v.toDouble(), 0.0, 1.0);
        }
        return;
    }
    ParamInfo info;
    if (!source.info(c.boundScope, c.boundParam, &info)) return;
    const QJsonValue now = source.value(c.boundScope, c.boundParam);
    switch (c.kind) {
    case Kind::Slider:
        if (!info.isNumber()) return;
        if (!c.ownRange) { // the parameter's range, unless the operator gave the tile its own
            c.min = info.min;
            c.max = info.max > info.min ? info.max : info.min + 1;
            c.step = info.type == ParamInfo::Type::Int ? std::max(1.0, info.step) : std::max(1e-6, info.step);
        } else if (!(c.max > c.min)) {
            c.max = c.min + 1;
        }
        c.value = std::clamp(now.toDouble(), std::min(c.min, c.max), std::max(c.min, c.max));
        break;
    case Kind::Toggle: c.value = now.toBool() ? 1.0 : 0.0; break;
    case Kind::Choice:
        if (!info.choices.empty()) c.choices = choicesOf(info);
        c.value = std::max<qsizetype>(0, c.choices.indexOf(now.toString()));
        break;
    default: break;
    }
}

bool setBound(const Control &c, ParameterSource &source, double value)
{
    ParamInfo info;
    if (!source.info(c.boundScope, c.boundParam, &info) || !(info.access & ParamInfo::Write)) return false;
    bool ok = false;
    switch (c.kind) {
    case Kind::Slider: {
        double v = value;
        if (c.ownRange && !info.unbounded && info.max > info.min) v = std::clamp(v, info.min, info.max); // the tile's own range is within what the parameter takes
        ok = info.isNumber() && source.setNumber(c.boundScope, c.boundParam, v);
        break;
    }
    case Kind::Toggle: ok = source.setJson(c.boundScope, c.boundParam, value >= 0.5); break;
    case Kind::Choice: {
        const std::vector<std::string> &keys = info.choices;
        const int k = std::clamp(int(std::lround(value)), 0, int(keys.size()) - 1);
        ok = !keys.empty() && source.setJson(c.boundScope, c.boundParam, qs(keys[std::size_t(k)]));
        break;
    }
    case Kind::Button: ok = value >= 0.5 && source.setJson(c.boundScope, c.boundParam, true); break; // a trigger: sent when pressed
    default: break;
    }
    if (ok) source.touched(c.boundScope, c.boundParam);
    return ok;
}

bool setTargets(const Control &c, ParameterSource &source, double value, int axis, bool onRelease)
{
    if (!std::isfinite(value)) return false;
    const double span = c.max - c.min;
    const double n = std::clamp(span > 0 ? (value - c.min) / span : value, 0.0, 1.0);
    if (c.kind == Kind::Button && n < 0.5 && !onRelease) return false;
    bool any = false;
    for (const Target &t : c.targets) {
        if (t.axis != axis) continue;
        ParamInfo info;
        if (!source.info(t.scope, t.path, &info) || !(info.access & ParamInfo::Write)) continue;
        const double shaped = std::pow(n, t.curve > 0 ? t.curve : 1.0);
        const double out = t.outMin + shaped * (t.outMax - t.outMin);
        bool ok = false;
        switch (info.type) {
        case ParamInfo::Type::Float:
        case ParamInfo::Type::Int: ok = source.setNumber(t.scope, t.path, out); break;
        case ParamInfo::Type::Bool: ok = source.setJson(t.scope, t.path, out >= 0.5); break;
        case ParamInfo::Type::Choice: {
            const int k = std::clamp(int(std::lround(out)), 0, int(info.choices.size()) - 1);
            ok = !info.choices.empty() && source.setJson(t.scope, t.path, qs(info.choices[std::size_t(k)]));
            break;
        }
        case ParamInfo::Type::Trigger: ok = out >= 0.5 && n >= 0.5 && source.setJson(t.scope, t.path, true); break; // sent when pressed
        default: break;
        }
        if (ok) source.touched(t.scope, t.path);
        any |= ok;
    }
    return any;
}

bool setBoundColor(const Control &c, ParameterSource &source, double r, double g, double b)
{
    const double v[3] = {r, g, b};
    bool any = false;
    for (int k = 0; k < 3; ++k) {
        const QString path = c.boundParam + QLatin1Char('/') + QLatin1Char("rgb"[k]);
        if (!std::isfinite(v[k]) || !source.setNumber(c.boundScope, path, std::clamp(v[k], 0.0, 1.0))) continue;
        any = true;
        source.touched(c.boundScope, path);
    }
    return any;
}

} // namespace pxs::controls
