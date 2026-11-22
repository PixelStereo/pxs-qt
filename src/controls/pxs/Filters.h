#pragma once
// The filters of a tile: the interaction filters of pxs (Average, Damper, Curve, Clamp, Steps, Delay, Hold…) put on what a slider or a switch
// sends. A tile keeps them as JSON (Control::filters: an array of stages, as an interaction's chain is saved); this runs them.
//
// The chain works on the tile's range as 0..1: what the operator sets is brought to 0..1, goes through the stages in their order, and the result
// is given back in the tile's range. A stage that moves by itself (Average, Damper, Delay, Hold…) goes on after the operator let go: the
// application calls step() every frame while active().
#include <QJsonArray>
#include <QJsonObject>
#include <QtGlobal>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <Interactions.h>

namespace pxs::controls {

inline constexpr int kFiltersMax = 8; // stages on one tile

// Every stage valid, its settings within their range, at most kFiltersMax of them (what an editor sent, or a file held)
QJsonArray cleanFilters(const QJsonArray &filters);
// true: one of them is on
bool anyFilterOn(const QJsonArray &filters);

// What the page needs to draw the editor: the filters by group (key, name, what it does, its settings), the operations of Math, and the curves of Curve
QJsonObject filterCatalog();

class FilterRunner {
public:
    // The operator set a value (0..1 in the tile's range). The value to send now (none: a stage holds it back for now)
    std::optional<double> input(quint64 id, const QJsonArray &filters, double x);
    // `dt` seconds go by: the tiles whose result moved, with it (0..1)
    std::vector<std::pair<quint64, double>> step(double dt);
    bool active() const; // some chain still moves by itself
    void forget(quint64 id) { m_runs.erase(id); }
    void clear() { m_runs.clear(); }

private:
    struct Run {
        Interaction chain; // only its filters are used
        InteractionProcessor processor;
        QByteArray signature;
        double last = 0, quiet = 0, patience = 2;
        bool live = false;
    };
    std::map<quint64, Run> m_runs;
};

} // namespace pxs::controls
