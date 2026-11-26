#pragma once
// An animation track's curve on screen: keys to click, drag, draw over and bend (easings, Bézier handles, hold).
#include <Anim.h>

#include <QWidget>
#include <QColor>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace pxs::curveview {

// One track's curve (or its oscillator's wave) over the duration. A click adds a key, a key is dragged; dragging over an
// empty place draws: the keys under the stroke are replaced by the drawn ones. Double-click on a key: deleted;
// right-click: its curve towards the next key (an easing, Bézier, hold), its value, its time, delete. A Bézier curve
// shows its handles: dragged, they keep the key's tangent smooth (Alt: each on its own).
class CurveLane : public QWidget
{
    Q_OBJECT
public:
    explicit CurveLane(QWidget *parent = nullptr);
    // The track, the duration and the range of its number (widened to the keys)
    void setTrack(const AnimTrack &t, double duration, double lo, double hi);
    // What is shown: seconds t0..t1, values vlo..vhi (the zoom)
    void setView(double t0, double t1, double vlo, double vhi);
    void setPlayhead(double position, bool shown, double played = NAN, bool backwards = false);
    // The value the curve gives for the signal at the playhead (`shown`: false hides it): a dot on the curve with a guide to the value axis
    void setProbe(double value, bool shown);
    struct Overlay { int index; AnimTrack track; QColor color; };
    void setOverlays(std::vector<Overlay> overlays, bool selectOnClick);
    void setCurrentValue(std::function<double()> f) { m_current = std::move(f); }
    void setLive(double v); // the value a "current value" first key starts from (captured, or the number now)
    double fullLo() const { return m_lo; }
    double fullHi() const { return m_hi; }
    const AnimTrack &track() const { return m_track; }
    static constexpr int kMargin = 10; // left and right: the ruler uses the same
    QSize sizeHint() const override { return QSize(500, 96); }

signals:
    // gesture: the edits of one drag share a number, counted in this lane (the application joins it to what names the lane, for one undo step); 0: an edit on its own
    void overlaySelected(int index);
    void edited(const AnimTrack &t, int gesture);
    void zoomTime(double factor, double anchor); // ⌘/Ctrl + wheel
    void panTime(double seconds);                // Shift + wheel, horizontal wheel, middle drag
    void zoomValue(double factor, double anchor); // Alt + wheel
    void panValue(double delta);                  // middle drag

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    double playedAt(double time) const;
    double xOf(double t) const;
    double yOf(double v) const;
    double tOf(double x) const;
    double vOf(double y) const;
    double shownValue(size_t k) const; // a key's value as drawn
    AnimTrack shown() const;           // the track as drawn (a "current value" key at the live value)
    int keyAt(QPointF p) const;
    // A Bézier handle under p: its key, and which side (true: out, towards the next key)
    bool handleAt(QPointF p, int *key, bool *out) const;
    void dragHandle(QPointF pos, bool alone);
    void emitEdited(int gesture = 0);
    std::vector<Overlay> m_overlays;
    bool m_selectOverlay = false;
    AnimTrack m_track;
    double m_duration = 4, m_lo = 0, m_hi = 1;    // the whole
    double m_t0 = 0, m_t1 = 4, m_vlo = 0, m_vhi = 1; // what is shown
    double m_playhead = 0, m_played = 0;
    bool m_backwards = false;
    double m_live = std::numeric_limits<double>::quiet_NaN();
    bool m_showPlayhead = false;
    double m_probe = 0;
    bool m_showProbe = false;
    int m_dragKey = -1;
    int m_dragHandle = -1; // the key whose handle is dragged
    bool m_dragOut = false;
    int m_gesture = 0, m_drags = 0; // the number of the drag going on, and how many there have been in this lane
    QPointF m_press;         // where a key drag started
    AnimKey m_pressKey;
    bool m_drawing = false;
    bool m_panning = false;
    QPointF m_panFrom;
    double m_lastDrawX = 0;
    std::vector<AnimKey> m_base, m_stroke; // drawing: the keys before, and the ones drawn
    std::function<double()> m_current;
};

// The name of a key's curve towards the next key: an easing, Bézier, hold
QString animCurveName(int curve);

} // namespace pxs::curveview
