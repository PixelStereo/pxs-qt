#pragma once
// Durations in milliseconds counted in fixed buckets (fine around 8.3, 16.7, 20, 33.3 and 40 ms; the last: beyond): how late a cue
// fired, how long its work took, how long a frame took. The steady clock they are read from.
#include <QJsonObject>
#include <QtGlobal>
#include <array>

namespace pxs {

struct TimingHistogram {
    static constexpr int kBuckets = 27;
    std::array<quint64, kBuckets> buckets{};
    quint64 count = 0;
    double total = 0, max = 0;
    void add(double ms);
    double percentile(double p) const; // upper edge of its bucket, never above max (0: none)
    double mean() const { return count ? total / double(count) : 0; }
    QJsonObject json() const;
    static double edge(int k); // upper edge of bucket k (ms; infinity for the last)
    static qint64 nowNs();     // the steady clock
};

} // namespace pxs
