#include "pxs/TimingHistogram.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace pxs {

// Fine around the usual frame periods (120, 60, 50, 30, 25 Hz), so that the percentiles read where frames land
static constexpr double kEdges[TimingHistogram::kBuckets - 1] = {
    0.5, 1, 2, 4, 6, 8, 8.4, 10, 12, 14, 16, 16.8, 18, 20, 20.5, 22, 25, 30, 33.5, 40, 41, 50, 66.7, 100, 200, 500};

double TimingHistogram::edge(int k)
{
    return k >= 0 && k < kBuckets - 1 ? kEdges[k] : std::numeric_limits<double>::infinity();
}

void TimingHistogram::add(double ms)
{
    if (!std::isfinite(ms)) return;
    ms = std::max(0.0, ms);
    int k = 0;
    while (k < kBuckets - 1 && ms > kEdges[k]) ++k;
    ++buckets[size_t(k)];
    ++count;
    total += ms;
    max = std::max(max, ms);
}

double TimingHistogram::percentile(double p) const
{
    if (!count) return 0;
    const double want = std::max(1.0, std::ceil(std::clamp(p, 0.0, 1.0) * double(count)));
    quint64 seen = 0;
    for (int k = 0; k < kBuckets; ++k) {
        seen += buckets[size_t(k)];
        if (double(seen) >= want) return std::min(edge(k), max);
    }
    return max;
}

QJsonObject TimingHistogram::json() const
{
    auto r = [](double v) { return std::round(v * 100.0) / 100.0; };
    return QJsonObject{{"count", double(count)}, {"mean_ms", r(mean())}, {"p50_ms", r(percentile(0.5))},
                       {"p95_ms", r(percentile(0.95))}, {"p99_ms", r(percentile(0.99))}, {"max_ms", r(max)}};
}

qint64 TimingHistogram::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace pxs
