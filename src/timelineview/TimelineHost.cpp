#include <pxs/TimelineHost.h>

void TimelineHost::waveAround(quint64 holder, const QString &path, double *center, double *amplitude) const
{
    double lo = 0, hi = 1;
    for (const ParamInfo &p : animatableParams(holder))
        if (p.path == path) lo = p.min, hi = p.max;
    *center = (lo + hi) / 2;
    *amplitude = (hi - lo) / 2;
}

bool TimelineHost::parameterInfo(quint64 holder, const QString &path, ParamInfo *info) const
{
    for (const ParamInfo &p : animatableParams(holder))
        if (p.path == path) {
            *info = p;
            return true;
        }
    return false;
}
