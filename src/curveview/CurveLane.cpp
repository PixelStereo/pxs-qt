#include "pxs/CurveLane.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace pxs::curveview {

static const QColor kCurve(86, 196, 255);
static const QColor kWave(224, 180, 58);
static const QColor kHandle(235, 235, 240);
static const QColor kPlayhead(76, 217, 100);
static const QColor kCurrentStart(246, 196, 83);

QString animCurveName(int c)
{
    static const char *const easings[] = {"Linear", "Ease In", "Ease Out", "Ease In-Out", "Ease In Cubic", "Ease Out Cubic"};
    if (c == kAnimBezier) return QStringLiteral("Bézier (handles)");
    if (c == kAnimHold) return QStringLiteral("Hold (steps)");
    return c >= 0 && c < 6 ? QString::fromUtf8(easings[c]) : QString();
}

static double tickStep(double duration, double width, double px)
{
    const double minimum = std::max(0.0, duration) * px / std::max(1.0, width);
    for (double s : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0,
                     1200.0, 1800.0, 3600.0, 7200.0, 14400.0, 21600.0, 43200.0, 86400.0})
        if (s >= minimum) return s;
    double step = 86400;
    while (step < minimum && std::isfinite(step * 2)) step *= 2;
    return step;
}

static QString num(double v)
{
    return std::abs(v) >= 100 ? QString::number(v, 'f', 0) : QString::number(v, 'f', std::abs(v) >= 10 ? 1 : 2);
}

CurveLane::CurveLane(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(70);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

static bool startsFromCurrent(const AnimTrack &t) { return !t.oscillator && !t.keys.empty() && t.keys.front().isCurrentValue; }

static void sortKeys(std::vector<AnimKey> &keys)
{
    std::stable_sort(keys.begin(), keys.end(), [](const AnimKey &a, const AnimKey &b) { return a.t < b.t; });
}

void CurveLane::setTrack(const AnimTrack &t, double duration, double lo, double hi)
{
    m_track = t;
    m_duration = std::max(0.05, duration);
    // The range shows the parameter's, and anything beyond it the track holds
    if (t.oscillator) {
        lo = std::min(lo, t.center - std::abs(t.amplitude));
        hi = std::max(hi, t.center + std::abs(t.amplitude));
    } else {
        for (size_t k = 0; k < t.keys.size(); ++k) {
            if (k == 0 && t.keys[0].isCurrentValue) continue; // a placeholder
            lo = std::min(lo, t.keys[k].v);
            hi = std::max(hi, t.keys[k].v);
        }
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    m_lo = lo;
    m_hi = hi;
    update();
}

void CurveLane::setView(double t0, double t1, double vlo, double vhi)
{
    if (t1 - t0 < 1e-6) t1 = t0 + 1e-6;
    if (vhi - vlo < 1e-12) vhi = vlo + 1e-12;
    if (t0 == m_t0 && t1 == m_t1 && vlo == m_vlo && vhi == m_vhi) return;
    m_t0 = t0;
    m_t1 = t1;
    m_vlo = vlo;
    m_vhi = vhi;
    update();
}

void CurveLane::setLive(double v)
{
    if (!startsFromCurrent(m_track)) return;
    if (v == m_live || (std::isnan(v) && std::isnan(m_live))) return;
    m_live = v;
    if (m_dragKey < 0 && m_dragHandle < 0) update();
}

void CurveLane::setProbe(double value, bool shown)
{
    if (shown == m_showProbe && (!shown || std::abs(value - m_probe) < 1e-6)) return;
    m_probe = value;
    m_showProbe = shown;
    update();
}

void CurveLane::setPlayhead(double position, bool shown, double played, bool backwards)
{
    if (!std::isfinite(played)) played = position;
    if (std::abs(position - m_playhead) < 1e-6 && std::abs(played - m_played) < 1e-6 &&
        backwards == m_backwards && shown == m_showPlayhead) return;
    m_playhead = position;
    m_played = played;
    m_backwards = backwards;
    m_showPlayhead = shown;
    update();
}

double CurveLane::sampleAtPlayhead() const
{
    if (m_track.oscillator) return m_track.oscillatorOf().valueAt(m_played);
    return pxs::core::timeline::curveAt(m_track.keys, m_playhead, m_live);
}

std::pair<double, double> CurveLane::signalSpan(std::pair<double, double> fallback) const
{
    if (m_track.oscillator && std::abs(m_track.amplitude) > 1e-12)
        return {m_track.center - std::abs(m_track.amplitude), m_track.center + std::abs(m_track.amplitude)};

    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    const auto include = [&](double value) {
        if (!std::isfinite(value)) return;
        lo = std::min(lo, value);
        hi = std::max(hi, value);
    };
    if (m_track.oscillator) include(m_track.center);
    else for (size_t i = 0; i < m_track.keys.size(); ++i) {
        include(shownValue(i));
        if (i + 1 >= m_track.keys.size() || m_track.keys[i].curve != kAnimBezier) continue;
        double t1, v1, t2, v2;
        pxs::core::timeline::bezierHandles(m_track.keys, i, m_live, &t1, &v1, &t2, &v2);
        include(v1);
        include(v2);
    }
    if (hi - lo > 1e-12) return {lo, hi};
    return {std::min(lo, fallback.first), std::max(hi, fallback.second)};
}

double CurveLane::xOf(double t) const { return kMargin + (t - m_t0) / (m_t1 - m_t0) * (width() - 2 * kMargin); }
double CurveLane::tOf(double x) const
{
    return std::clamp(m_t0 + (x - kMargin) / std::max(1, width() - 2 * kMargin) * (m_t1 - m_t0), 0.0, m_duration);
}
double CurveLane::yOf(double v) const { return 8 + (m_vhi - v) / (m_vhi - m_vlo) * (height() - 16); }
double CurveLane::vOf(double y) const
{
    const double v = m_vhi - (y - 8) / std::max(1, height() - 16) * (m_vhi - m_vlo);
    return std::clamp(v, std::min(m_lo, m_vlo), std::max(m_hi, m_vhi));
}

double CurveLane::shownValue(size_t k) const
{
    if (k == 0 && startsFromCurrent(m_track) && std::isfinite(m_live)) return m_live;
    return m_track.keys[k].v;
}

AnimTrack CurveLane::shown() const
{
    AnimTrack t = m_track;
    if (startsFromCurrent(t)) t.captured = m_live;
    return t;
}

int CurveLane::keyAt(QPointF p) const
{
    if (m_track.oscillator) return -1;
    int best = -1;
    double bestD = 8;
    for (size_t k = 0; k < m_track.keys.size(); ++k) {
        const double d = std::hypot(xOf(m_track.keys[k].t) - p.x(), yOf(shownValue(k)) - p.y());
        if (d <= bestD) best = int(k), bestD = d;
    }
    return best;
}

bool CurveLane::handleAt(QPointF p, int *key, bool *out) const
{
    if (m_track.oscillator) return false;
    const AnimTrack t = shown();
    double bestD = 7;
    bool found = false;
    for (size_t k = 0; k + 1 < t.keys.size(); ++k) {
        if (t.keys[k].curve != kAnimBezier) continue;
        double t1, v1, t2, v2;
        t.bezierHandles(k, &t1, &v1, &t2, &v2);
        const double d1 = std::hypot(xOf(t1) - p.x(), yOf(v1) - p.y()), d2 = std::hypot(xOf(t2) - p.x(), yOf(v2) - p.y());
        if (d1 <= bestD) bestD = d1, *key = int(k), *out = true, found = true;
        if (d2 <= bestD) bestD = d2, *key = int(k + 1), *out = false, found = true;
    }
    return found;
}

void CurveLane::emitEdited(int gesture) { emit edited(m_track, gesture); }

void CurveLane::setOverlays(std::vector<Overlay> overlays, bool selectOnClick)
{
    m_overlays = std::move(overlays);
    m_selectOverlay = selectOnClick;
    update();
}

double CurveLane::playedAt(double time) const
{
    return m_showPlayhead ? std::max(0.0, m_played + (m_backwards ? m_playhead - time : time - m_playhead)) : time;
}

void CurveLane::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(26, 26, 30));
    // Beyond the animation (zoomed out past its ends): darker
    const double xs = xOf(0), xe = xOf(m_duration);
    if (xs > 0) p.fillRect(QRectF(0, 0, xs, height()), QColor(18, 18, 21));
    if (xe < width()) p.fillRect(QRectF(xe, 0, width() - xe, height()), QColor(18, 18, 21));
    const double x0 = std::max(xs, 0.0), x1 = std::min(xe, double(width()));
    // Seconds, and the values shown (their bounds and their middle, or zero)
    p.setPen(QColor(44, 44, 50));
    const double step = tickStep(m_t1 - m_t0, width() - 2 * kMargin, 50);
    for (long long i = std::max(0LL, (long long)std::ceil(m_t0 / step)); i * step <= std::min(m_t1, m_duration) + 1e-9; ++i)
        p.drawLine(QPointF(xOf(i * step), 0), QPointF(xOf(i * step), height()));
    const double mid = (m_vlo < 0 && m_vhi > 0) ? 0.0 : (m_vlo + m_vhi) / 2;
    for (double v : {m_vlo, mid, m_vhi}) p.drawLine(QPointF(x0, yOf(v)), QPointF(x1, yOf(v)));
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.8);
    p.setFont(f);
    p.setPen(QColor(110, 110, 118));
    p.drawText(QRectF(x0 + 3, 1, 80, 12), Qt::AlignLeft | Qt::AlignTop, num(m_vhi));
    p.drawText(QRectF(x0 + 3, height() - 13, 80, 12), Qt::AlignLeft | Qt::AlignBottom, num(m_vlo));

    // The values over one pass
    const AnimTrack drawn = shown();
    const bool any = m_track.oscillator || !m_track.keys.empty();
    const auto drawCurve = [&](const AnimTrack &track, QColor color, double stroke) {
        QPainterPath path;
        bool started = false;
        for (int x = int(x0); x <= int(std::ceil(x1)); ++x) {
            const double time = tOf(x);
            const double value = track.valueAt(time, playedAt(time));
            if (!std::isfinite(value)) {
                started = false;
                continue;
            }
            const QPointF point(x, yOf(value));
            if (started) path.lineTo(point);
            else path.moveTo(point);
            started = true;
        }
        if (!track.enabled) color.setAlphaF(color.alphaF() * 0.45f);
        p.save();
        p.setClipRect(QRectF(x0, 0, x1 - x0, height()));
        p.setPen(QPen(color, stroke));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.restore();
    };
    for (const auto &overlay : m_overlays) drawCurve(overlay.track, overlay.color, 1.5);
    if (any) drawCurve(drawn, m_track.oscillator ? kWave : kCurve, 1.8);
    else if (m_overlays.empty()) {
        p.setPen(QColor(110, 110, 118));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Click to add a key · drag to draw a pattern"));
    }
    if (!m_track.oscillator) {
        // Bézier handles: a line from the key, a small ring at its end
        for (size_t k = 0; k + 1 < drawn.keys.size(); ++k) {
            if (drawn.keys[k].curve != kAnimBezier) continue;
            double t1, v1, t2, v2;
            drawn.bezierHandles(k, &t1, &v1, &t2, &v2);
            const QPointF a(xOf(drawn.keys[k].t), yOf(shownValue(k))), b(xOf(drawn.keys[k + 1].t), yOf(shownValue(k + 1)));
            const QPointF h1(xOf(t1), yOf(v1)), h2(xOf(t2), yOf(v2));
            p.setPen(QPen(QColor(200, 200, 210, 140), 1));
            p.drawLine(a, h1);
            p.drawLine(b, h2);
            for (auto [h, key, out] : {std::tuple{h1, int(k), true}, std::tuple{h2, int(k + 1), false}}) {
                const bool active = key == m_dragHandle && out == m_dragOut;
                p.setPen(QPen(active ? QColor(Qt::white) : kHandle, 1.4));
                p.setBrush(QColor(26, 26, 30));
                p.drawEllipse(h, 3.5, 3.5);
            }
        }
        // The keys (a held one is a square; a first key "current value" is a ring)
        for (size_t k = 0; k < m_track.keys.size(); ++k) {
            const AnimKey &key = m_track.keys[k];
            const QPointF c(xOf(key.t), yOf(shownValue(k)));
            if (c.x() < -6 || c.x() > width() + 6) continue;
            const QColor fill = int(k) == m_dragKey ? QColor(Qt::white) : kCurve;
            if (k == 0 && key.isCurrentValue) {
                p.setPen(QPen(kCurrentStart, 2.5));
                p.setBrush(QColor(26, 26, 30));
                p.drawEllipse(c, 7, 7);
                p.drawEllipse(c, 3, 3);
                p.setPen(kCurrentStart);
                QFont bold = p.font(); bold.setBold(true); p.setFont(bold);
                p.drawText(QRectF(c.x() + 11, c.y() - 20, 90, 16), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("CURRENT"));
                continue;
            }
            p.setPen(QPen(QColor(20, 20, 24), 1));
            p.setBrush(fill);
            if (key.curve == kAnimHold) p.drawRect(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 7));
            else if (key.curve == kAnimBezier) p.drawPolygon(QPolygonF({c + QPointF(0, -5), c + QPointF(5, 0), c + QPointF(0, 5), c + QPointF(-5, 0)}));
            else p.drawEllipse(c, 4, 4);
        }
    }
    if (m_showPlayhead) {
        p.setPen(QPen(kPlayhead, 1.5));
        p.drawLine(QPointF(xOf(m_playhead), 0), QPointF(xOf(m_playhead), height()));
        if (m_showProbe) { // what the curve makes of it: across to the value axis, and a dot on the curve
            const QPointF at(xOf(m_playhead), yOf(m_probe));
            p.setPen(QPen(kPlayhead, 1, Qt::DashLine));
            p.drawLine(QPointF(0, at.y()), at);
            p.setPen(QPen(QColor(20, 20, 24), 1));
            p.setBrush(kPlayhead);
            p.drawEllipse(at, 5, 5);
        }
    }
}


void CurveLane::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton) { // the view moves (time and values)
        m_panning = true;
        m_panFrom = e->position();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (m_selectOverlay && !m_overlays.empty() && keyAt(e->position()) < 0) {
        int handle = -1;
        bool out = false;
        const double time = tOf(e->position().x()), played = playedAt(time);
        const double active = shown().valueAt(time, played);
        double distance = std::isfinite(active) ? std::min(7.0, std::abs(yOf(active) - e->position().y())) : 7.0;
        int best = -1;
        if (!handleAt(e->position(), &handle, &out)) {
            for (const auto &overlay : m_overlays) {
                const double value = overlay.track.valueAt(time, played);
                if (!std::isfinite(value)) continue;
                const double candidate = std::abs(yOf(value) - e->position().y());
                if (candidate < distance) distance = candidate, best = overlay.index;
            }
        }
        if (best >= 0) {
            emit overlaySelected(best);
            return;
        }
    }
    if (m_track.oscillator) return;
    int hk = -1;
    bool out = false;
    if (handleAt(e->position(), &hk, &out)) {
        m_dragHandle = hk;
        m_dragOut = out;
        m_gesture = ++m_drags;
        update();
        return;
    }
    const int k = keyAt(e->position());
    if (k >= 0) {
        m_dragKey = k;
        m_gesture = ++m_drags;
        m_press = e->position();
        m_pressKey = m_track.keys[size_t(k)];
        update();
        return;
    }
    // Drawing: a key now, then one every few pixels while dragging
    m_drawing = true;
    m_gesture = ++m_drags;
    m_base = m_track.keys;
    m_stroke = {AnimKey{tOf(e->position().x()), vOf(e->position().y()), 3}};
    m_lastDrawX = e->position().x();
    m_track.keys = m_base;
    m_track.keys.push_back(m_stroke.front());
    sortKeys(m_track.keys);
    update();
    emitEdited(m_gesture);
}

// The handle of a key follows the cursor; the key's other handle turns with it (the same slope: the curve stays smooth
// through the key) unless `alone`
void CurveLane::dragHandle(QPointF pos, bool alone)
{
    const int k = m_dragHandle;
    const int n = int(m_track.keys.size());
    if (k < 0 || k >= n) return;
    AnimKey &key = m_track.keys[size_t(k)];
    const double t = m_t0 + (pos.x() - kMargin) / std::max(1, width() - 2 * kMargin) * (m_t1 - m_t0);
    const double v = m_vhi - (pos.y() - 8) / std::max(1, height() - 16) * (m_vhi - m_vlo);
    const double kv = shownValue(size_t(k));
    double dt = 0, dv = v - kv;
    if (m_dragOut) {
        if (k + 1 >= n) return;
        const double span = m_track.keys[size_t(k + 1)].t - key.t;
        dt = std::clamp(t - key.t, 1e-6, std::max(1e-6, span));
        key.outDt = dt;
        key.outDv = dv;
    } else {
        if (k < 1) return;
        const double span = key.t - m_track.keys[size_t(k - 1)].t;
        dt = std::clamp(t - key.t, -std::max(1e-6, span), -1e-6);
        key.inDt = dt;
        key.inDv = dv;
    }
    if (alone) return;
    const double slope = dv / dt;
    if (m_dragOut && k > 0 && m_track.keys[size_t(k - 1)].curve == kAnimBezier) {
        const double span = key.t - m_track.keys[size_t(k - 1)].t;
        const double odt = key.hasIn() ? key.inDt : -span / 3;
        key.inDt = std::min(-1e-6, odt);
        key.inDv = key.inDt * slope;
    } else if (!m_dragOut && key.curve == kAnimBezier && k + 1 < n) {
        const double span = m_track.keys[size_t(k + 1)].t - key.t;
        const double odt = key.hasOut() ? key.outDt : span / 3;
        key.outDt = std::max(1e-6, odt);
        key.outDv = key.outDt * slope;
    }
}

void CurveLane::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (m_panning) {
        const QPointF d = pos - m_panFrom;
        m_panFrom = pos;
        emit panTime(-d.x() / std::max(1, width() - 2 * kMargin) * (m_t1 - m_t0));
        emit panValue(d.y() / std::max(1, height() - 16) * (m_vhi - m_vlo));
        return;
    }
    if (m_dragHandle >= 0) {
        dragHandle(pos, e->modifiers() & Qt::AltModifier);
        update();
        emitEdited(m_gesture);
        return;
    }
    if (m_dragKey >= 0 && m_dragKey < int(m_track.keys.size())) {
        AnimKey &k = m_track.keys[size_t(m_dragKey)];
        // Between its neighbours: the keys keep their order. Shift: along one axis only (the one moved most)
        const double lo = m_dragKey > 0 ? m_track.keys[size_t(m_dragKey - 1)].t : 0.0;
        const double hi = m_dragKey + 1 < int(m_track.keys.size()) ? m_track.keys[size_t(m_dragKey + 1)].t : m_duration;
        const QPointF d = pos - m_press;
        const bool shift = e->modifiers() & Qt::ShiftModifier;
        const bool timeOnly = (shift && std::abs(d.x()) >= std::abs(d.y())) || (m_dragKey == 0 && k.isCurrentValue);
        const bool valueOnly = shift && !timeOnly;
        k.t = valueOnly ? m_pressKey.t : std::clamp(tOf(xOf(m_pressKey.t) + d.x()), lo, hi);
        k.v = timeOnly ? m_pressKey.v : vOf(yOf(m_pressKey.v) + d.y());
        const QString v = m_dragKey == 0 && k.isCurrentValue ? QStringLiteral("current value") : num(k.v);
        QStatusTipEvent help(QStringLiteral("%1 s → %2").arg(k.t, 0, 'f', 2).arg(v));
        QCoreApplication::sendEvent(this, &help);
        update();
        emitEdited(m_gesture);
        return;
    }
    if (m_drawing) {
        if (std::abs(pos.x() - m_lastDrawX) < 6) return;
        m_lastDrawX = pos.x();
        m_stroke.push_back(AnimKey{tOf(pos.x()), vOf(pos.y()), 0});
        // The keys under the stroke give way to the drawn ones, linear from point to point
        double a = m_stroke.front().t, b = a;
        for (const AnimKey &k : m_stroke) a = std::min(a, k.t), b = std::max(b, k.t);
        std::vector<AnimKey> keys;
        for (const AnimKey &k : m_base)
            if (k.t < a - 1e-9 || k.t > b + 1e-9) keys.push_back(k);
        for (AnimKey k : m_stroke) {
            k.curve = 0;
            keys.push_back(k);
        }
        sortKeys(keys);
        // One key per time (going back over the stroke: the latest)
        std::vector<AnimKey> unique;
        for (const AnimKey &k : keys) {
            if (!unique.empty() && std::abs(unique.back().t - k.t) < 1e-6) unique.back() = k;
            else unique.push_back(k);
        }
        for (size_t k = 1; k < unique.size(); ++k) unique[k].isCurrentValue = false; // only a first key
        m_track.keys = unique;
        update();
        emitEdited(m_gesture);
        return;
    }
    int hk = -1;
    bool out = false;
    if (handleAt(pos, &hk, &out)) {
        setCursor(Qt::SizeAllCursor);
        setToolTip(QStringLiteral("Bézier handle: drag to shape the curve (the key's other handle turns with it; Alt: on its own)"));
        return;
    }
    const int k = keyAt(pos);
    setCursor(m_track.oscillator ? Qt::ArrowCursor : k >= 0 ? Qt::SizeAllCursor : Qt::CrossCursor);
    if (k >= 0) {
        const AnimKey &key = m_track.keys[size_t(k)];
        const QString v = k == 0 && key.isCurrentValue ? QStringLiteral("current value") : num(key.v);
        setToolTip(QStringLiteral("%1 s → %2 · %3\nDrag: move · Shift+drag: along one axis · double-click: delete")
                       .arg(key.t, 0, 'f', 2).arg(v, animCurveName(key.curve)));
    } else {
        setToolTip(m_track.oscillator ? QStringLiteral("A wave: its settings are with the track")
                                      : QStringLiteral("Click: a key · drag a key: move it · drag elsewhere: draw · "
                                                       "double-click a key: delete · right-click: curve (Bézier…), value\n"
                                                       "⌘/Ctrl+wheel: zoom time · Alt+wheel: zoom values · "
                                                       "Shift+wheel or middle drag: move the view"));
    }
}

void CurveLane::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragKey >= 0) {
        QStatusTipEvent help{QString()};
        QCoreApplication::sendEvent(this, &help);
    }
    if (m_panning) {
        m_panning = false;
        unsetCursor();
    }
    m_dragKey = -1;
    m_dragHandle = -1;
    m_drawing = false;
    m_base.clear();
    m_stroke.clear();
    update();
}

void CurveLane::mouseDoubleClickEvent(QMouseEvent *e)
{
    const int k = keyAt(e->position());
    if (k < 0) return;
    m_track.keys.erase(m_track.keys.begin() + k);
    m_dragKey = -1;
    update();
    emitEdited();
}

void CurveLane::wheelEvent(QWheelEvent *e)
{
    const QPoint a = e->angleDelta();
    const double steps = (a.y() != 0 ? a.y() : a.x()) / 120.0;
    const Qt::KeyboardModifiers m = e->modifiers();
    if (m & Qt::ControlModifier) {
        emit zoomTime(std::pow(1.2, steps), tOf(e->position().x()));
    } else if (m & Qt::AltModifier) {
        const double v = m_vhi - (e->position().y() - 8) / std::max(1, height() - 16) * (m_vhi - m_vlo);
        emit zoomValue(std::pow(1.2, steps), v);
    } else if ((m & Qt::ShiftModifier) || (a.x() != 0 && std::abs(a.x()) > std::abs(a.y()))) {
        emit panTime(-steps * 0.1 * (m_t1 - m_t0));
    } else {
        e->ignore(); // the tracks scroll
        return;
    }
    e->accept();
}

void CurveLane::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_track.oscillator) return;
    const int k = keyAt(e->pos());
    // Shown without waiting here: the menu goes with the lane, should the rows be rebuilt meanwhile
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (k >= 0) {
        if (k == 0) {
            QAction *cur = menu->addAction(QStringLiteral("Start from the current value"), this, [this](bool on) {
                m_track.keys[0].isCurrentValue = on;
                if (on && m_current) m_track.keys[0].v = m_current(); // the placeholder, should it be turned off
                update();
                emitEdited();
            });
            cur->setCheckable(true);
            cur->setChecked(m_track.keys[0].isCurrentValue);
            cur->setToolTip(QStringLiteral("The number starts from where it is when the animation starts: no jump"));
            menu->addSeparator();
        }
        QMenu *curve = menu->addMenu(QStringLiteral("Curve to the next key"));
        for (int c : {0, 1, 2, 3, 4, 5, int(kAnimBezier), int(kAnimHold)}) {
            QAction *a = curve->addAction(animCurveName(c), this, [this, k, c] {
                m_track.keys[size_t(k)].curve = c;
                update();
                emitEdited();
            });
            a->setCheckable(true);
            a->setChecked(m_track.keys[size_t(k)].curve == c);
        }
        const AnimKey &key = m_track.keys[size_t(k)];
        if (key.hasIn() || key.hasOut()) {
            menu->addAction(QStringLiteral("Automatic handles"), this, [this, k] {
                AnimKey &x = m_track.keys[size_t(k)];
                x.inDt = x.inDv = x.outDt = x.outDv = 0;
                update();
                emitEdited();
            });
        }
        QAction *value = menu->addAction(QStringLiteral("Value…"), this, [this, k] {
            if (k >= int(m_track.keys.size())) return; // reloaded meanwhile
            bool ok = false;
            const QPointer<CurveLane> guard(this);
            const double v = QInputDialog::getDouble(this, QStringLiteral("Key"), QStringLiteral("Value"), m_track.keys[size_t(k)].v,
                                                     -1e9, 1e9, 3, &ok);
            if (!guard || !ok || k >= int(m_track.keys.size())) return;
            m_track.keys[size_t(k)].v = v;
            update();
            emitEdited();
        });
        value->setEnabled(!(k == 0 && m_track.keys[0].isCurrentValue));
        menu->addAction(QStringLiteral("Time…"), this, [this, k] {
            if (k >= int(m_track.keys.size())) return; // reloaded meanwhile
            bool ok = false;
            const double lo = k > 0 ? m_track.keys[size_t(k - 1)].t : 0.0;
            const double hi = k + 1 < int(m_track.keys.size()) ? m_track.keys[size_t(k + 1)].t : m_duration;
            const QPointer<CurveLane> guard(this);
            const double t = QInputDialog::getDouble(this, QStringLiteral("Key"), QStringLiteral("Time (s)"), m_track.keys[size_t(k)].t,
                                                     lo, hi, 3, &ok);
            if (!guard || !ok || k >= int(m_track.keys.size())) return;
            m_track.keys[size_t(k)].t = t;
            update();
            emitEdited();
        });
        menu->addAction(QStringLiteral("Delete"), this, [this, k] {
            m_track.keys.erase(m_track.keys.begin() + k);
            if (!m_track.keys.empty() && k == 0) m_track.keys[0].isCurrentValue = false;
            update();
            emitEdited();
        });
    } else {
        const double t = tOf(e->pos().x());
        QAction *cur = menu->addAction(QStringLiteral("Key here with the current value"), this, [this, t] {
            m_track.keys.push_back(AnimKey{t, m_current(), 3});
            sortKeys(m_track.keys);
            for (size_t k = 1; k < m_track.keys.size(); ++k) m_track.keys[k].isCurrentValue = false;
            update();
            emitEdited();
        });
        cur->setEnabled(bool(m_current));
        QAction *smooth = menu->addAction(QStringLiteral("Every key: Bézier"), this, [this] {
            for (AnimKey &k : m_track.keys) k.curve = kAnimBezier;
            update();
            emitEdited();
        });
        smooth->setEnabled(m_track.keys.size() >= 2);
        QAction *clear = menu->addAction(QStringLiteral("Clear the keys"), this, [this] {
            m_track.keys.clear();
            update();
            emitEdited();
        });
        clear->setEnabled(!m_track.keys.empty());
    }
}

} // namespace pxs::curveview
