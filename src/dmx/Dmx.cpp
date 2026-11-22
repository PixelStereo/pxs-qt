#include "pxs/Dmx.h"

#include <pxs/core/dmx/frame.h>

namespace pxs::dmx {
namespace {

QByteArray toQt(const std::vector<std::uint8_t> &bytes)
{
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}

std::vector<std::uint8_t> fromQt(const QByteArray &bytes)
{
    const auto *first = reinterpret_cast<const std::uint8_t *>(bytes.constData());
    return std::vector<std::uint8_t>(first, first + bytes.size());
}

} // namespace

QByteArray encode(const Frame &f)
{
    core::dmx::Frame core;
    core.sacn = f.sacn;
    core.universe = f.universe;
    core.priority = f.priority;
    core.sequence = f.sequence;
    core.cid = fromQt(f.cid);
    core.values = fromQt(f.values);
    core.length = f.length;
    core.startCode = f.startCode;
    const QByteArray name = f.name.toUtf8();
    core.name.assign(name.constData(), std::size_t(name.size()));
    core.terminated = f.terminated;
    return toQt(core::dmx::encode(core));
}

bool decode(const QByteArray &d, bool sacn, Frame &f)
{
    core::dmx::Frame core;
    const bool ok = core::dmx::decode(reinterpret_cast<const std::uint8_t *>(d.constData()), std::size_t(d.size()), sacn, core);
    f = Frame{};
    f.sacn = sacn;
    if (!ok) return false;
    f.universe = core.universe;
    f.priority = core.priority;
    f.sequence = core.sequence;
    f.cid = toQt(core.cid);
    f.values = toQt(core.values);
    f.length = core.length;
    f.startCode = core.startCode;
    f.name = QString::fromUtf8(core.name.data(), qsizetype(core.name.size()));
    f.terminated = core.terminated;
    return true;
}

QString sacnGroup(int universe) { return QString::fromStdString(core::dmx::sacnGroup(universe)); }

} // namespace pxs::dmx
