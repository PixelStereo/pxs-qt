// Timelines, the Qt face: the values come from pxs-core, and what is saved reads back the same
#include "Anim.h"

#include <QJsonDocument>

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

static QJsonObject saved(const AnimTrack &t)
{
    QJsonObject o;
    animTrackToJson(t, o);
    return o;
}

int main()
{
    // A curve and an oscillator: the values pxs-core gives
    AnimTrack curve;
    curve.keys = {AnimKey{0, 0, 0}, AnimKey{2, 10, 0}};
    CHECK(near(curve.valueAt(1, 0), 5));
    AnimTrack osc;
    osc.oscillator = true;
    osc.wave = AnimWave::Sine;
    osc.center = 0;
    osc.amplitude = 1;
    osc.period = 2;
    CHECK(near(osc.valueAt(0, 0.5), 1));
    // The transport is pxs-core's
    Animation a;
    a.duration = 2;
    a.loop = AnimLoop::PingPong;
    a.clock = 3;
    CHECK(near(a.position(), 1));
    CHECK(a.backwardsAt(3));

    // Saved and read back: keys with every curve, handles, a current value; an oscillator; filters
    AnimTrack t;
    t.keys = {AnimKey{0, 1, 2}, AnimKey{1, 2, kAnimBezier}, AnimKey{2, 3, kAnimHold}, AnimKey{3, 4, 3}};
    t.keys[0].isCurrentValue = true;
    t.keys[1].outDt = 0.2;
    t.keys[1].outDv = 0.5;
    t.keys[2].inDt = -0.3;
    t.keys[2].inDv = 1.5;
    t.dataFilters = QJsonArray{QJsonObject{{"type", "smooth"}}};
    AnimTrack back;
    animTrackFromJson(back, saved(t), 10);
    CHECK(back.keys.size() == 4);
    for (size_t k = 0; k < 4 && k < back.keys.size(); ++k) {
        CHECK(near(back.keys[k].t, t.keys[k].t) && near(back.keys[k].v, t.keys[k].v) && back.keys[k].curve == t.keys[k].curve);
        CHECK(near(back.keys[k].outDt, t.keys[k].outDt) && near(back.keys[k].inDv, t.keys[k].inDv));
    }
    CHECK(back.keys[0].isCurrentValue);
    CHECK(back.dataFilters == t.dataFilters);
    CHECK(saved(back) == saved(t));

    AnimTrack w;
    w.oscillator = true;
    w.wave = AnimWave::SmoothRandom;
    w.period = 3;
    w.center = 0.2;
    w.amplitude = 0.7;
    w.phase = 0.25;
    w.seed = 12345;
    AnimTrack wb;
    animTrackFromJson(wb, saved(w), 4);
    CHECK(wb.oscillator && wb.wave == AnimWave::SmoothRandom && near(wb.period, 3) && near(wb.center, 0.2) && near(wb.amplitude, 0.7));
    CHECK(near(wb.phase, 0.25) && wb.seed == 12345u);
    CHECK(saved(wb) == saved(w));
    // The keys of NyxMap's projects
    CHECK(saved(w).value("oscillator").toObject().value("wave").toString() == "smooth_random");

    // A colour track, an event track, a point: each its own form
    AnimTrack c;
    c.color = true;
    AnimKey k0{0, 0, 0}, k1{1, 0, 0};
    k0.rgb = {0, 0.5, 1};
    k1.rgb = {1, 0, 0};
    c.keys = {k0, k1};
    AnimTrack cb;
    animTrackFromJson(cb, saved(c), 2);
    CHECK(cb.color && cb.keys.size() == 2 && near(cb.keys[0].rgb[1], 0.5) && near(cb.keys[1].rgb[0], 1));
    AnimTrack e;
    e.event = true;
    e.keys = {AnimKey{0.5, 1, 0}, AnimKey{1.5, 0, 0}};
    AnimTrack eb;
    animTrackFromJson(eb, saved(e), 2);
    CHECK(eb.event && eb.keys.size() == 2 && near(eb.keys[1].t, 1.5) && near(eb.keys[0].v, 1));
    AnimTrack p;
    p.param = "position";
    p.axes.resize(2);
    p.axes[0].keys = {AnimKey{0, 0.1, 0}};
    p.axes[1].oscillator = true;
    AnimTrack pb;
    pb.param = "position";
    animTrackFromJson(pb, saved(p), 2);
    CHECK(pb.isPoint() && pb.axes[0].param == "position/x" && pb.axes[1].oscillator);

    // Keys are kept within the duration, and in time order
    AnimTrack late;
    late.keys = {AnimKey{5, 1, 0}, AnimKey{1, 0, 0}};
    AnimTrack lb;
    animTrackFromJson(lb, saved(late), 2);
    CHECK(lb.keys.size() == 2 && near(lb.keys[0].t, 1) && near(lb.keys[1].t, 2));

    // The transport's settings
    Animation tr;
    tr.duration = 3;
    tr.loop = AnimLoop::PingPong;
    tr.repeat = 4;
    tr.speed = 2;
    QJsonObject o;
    animTransportToJson(tr, o);
    CHECK(o.value("loop").toString() == "pingpong");
    Animation trb;
    animTransportFromJson(trb, o);
    CHECK(near(trb.duration, 3) && trb.loop == AnimLoop::PingPong && trb.repeat == 4 && near(trb.speed, 2));
    Animation fallback;
    animTransportFromJson(fallback, QJsonObject{});
    CHECK(near(fallback.duration, 4) && fallback.loop == AnimLoop::Loop && fallback.repeat == 0 && near(fallback.speed, 1));

    if (failures) std::fprintf(stderr, "anim: %d check(s) failed\n", failures);
    else std::printf("anim: ok\n");
    return failures ? 1 : 0;
}
