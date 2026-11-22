#include "OscCodec.h"

#include <pxs/core/osc/codec.h>
#include <pxs/core/osc/names.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace osc {
namespace {

pxs::core::osc::Value toCore(char type, const QVariant &value)
{
    using pxs::core::osc::Blob;
    using pxs::core::osc::Value;
    switch (type) {
    case 'i': return std::int32_t(value.toInt());
    case 'h':
    case 't': return std::int64_t(value.toLongLong());
    case 'f': return float(value.toFloat());
    case 'd': return value.toDouble();
    case 's':
    case 'S': {
        const QByteArray utf8 = value.toString().toUtf8();
        const char *data = utf8.constData();
        return std::string(data ? data : "", std::size_t(utf8.size()));
    }
    case 'b': {
        const QByteArray bytes = value.toByteArray();
        Blob blob(std::size_t(bytes.size()));
        if (!blob.empty()) std::copy_n(reinterpret_cast<const std::uint8_t *>(bytes.constData()), blob.size(), blob.begin());
        return blob;
    }
    case 'c': {
        const QString text = value.toString();
        return std::uint32_t(text.isEmpty() ? 0 : text.at(0).unicode());
    }
    case 'r':
    case 'm': return std::uint32_t(value.toLongLong());
    case 'T': return true;
    case 'F': return false;
    default: return Value{};
    }
}

QVariant fromCore(char type, const pxs::core::osc::Value &value)
{
    switch (type) {
    case 'i': if (const auto *v = std::get_if<std::int32_t>(&value)) return int(*v); break;
    case 'h': if (const auto *v = std::get_if<std::int64_t>(&value)) return qlonglong(*v); break;
    case 'f': if (const auto *v = std::get_if<float>(&value)) return double(*v); break;
    case 'd': if (const auto *v = std::get_if<double>(&value)) return *v; break;
    case 's':
    case 'S':
        if (const auto *v = std::get_if<std::string>(&value)) return QString::fromUtf8(v->data(), qsizetype(v->size()));
        break;
    case 'b':
        if (const auto *v = std::get_if<pxs::core::osc::Blob>(&value)) {
            if (v->empty()) return QByteArray();
            return QByteArray(reinterpret_cast<const char *>(v->data()), qsizetype(v->size()));
        }
        break;
    case 'c':
        if (const auto *v = std::get_if<std::uint32_t>(&value)) return QString(QChar(char16_t(*v & 0xffff)));
        break;
    case 'r':
    case 'm':
        if (const auto *v = std::get_if<std::uint32_t>(&value)) return qlonglong(*v);
        break;
    case 't': if (const auto *v = std::get_if<std::int64_t>(&value)) return qlonglong(*v); break;
    case 'T': return true;
    case 'F': return false;
    default: return {};
    }
    return {};
}

} // namespace

QByteArray encode(const Message &message)
{
    pxs::core::osc::Message portable;
    portable.address = message.address.toUtf8().toStdString();
    portable.types = message.types.toLatin1().toStdString();
    portable.args.reserve(std::size_t(message.types.size()));
    for (qsizetype i = 0; i < message.types.size(); ++i)
        portable.args.push_back(toCore(message.types.at(i).toLatin1(), message.args.value(i)));
    const auto bytes = pxs::core::osc::encode(portable);
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}

bool decode(const QByteArray &packet, std::vector<Message> &out)
{
    std::vector<pxs::core::osc::Message> portable;
    if (!pxs::core::osc::decode(reinterpret_cast<const std::uint8_t *>(packet.constData()), std::size_t(packet.size()), portable))
        return false;
    for (const auto &item : portable) {
        Message message;
        message.address = QString::fromUtf8(item.address.data(), qsizetype(item.address.size()));
        message.types = QString::fromLatin1(item.types.data(), qsizetype(item.types.size()));
        message.args.reserve(qsizetype(item.args.size()));
        for (std::size_t i = 0; i < item.types.size(); ++i) message.args << fromCore(item.types[i], item.args[i]);
        out.push_back(std::move(message));
    }
    return true;
}

bool match(const QString &pattern, const QString &address)
{
    const QByteArray p = pattern.toUtf8();
    const QByteArray a = address.toUtf8();
    const char *pData = p.constData();
    const char *aData = a.constData();
    return pxs::core::osc::match(std::string(pData ? pData : "", std::size_t(p.size())),
                                 std::string(aData ? aData : "", std::size_t(a.size())));
}

namespace {
std::string utf8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), std::size_t(b.size()));
}
// Not QString::fromUtf8 alone: it drops a leading U+FEFF (a byte order mark), and a name that starts with that character
// must come back as it went in.
QString fromUtf8(const std::string &s)
{
    std::size_t bom = 0; // leading U+FEFF (EF BB BF), as many as there are
    while (s.compare(bom * 3, 3, "\xEF\xBB\xBF") == 0 && s.size() >= (bom + 1) * 3) ++bom;
    QString out(qsizetype(bom), QChar(0xFEFF));
    out += QString::fromUtf8(s.data() + bom * 3, qsizetype(s.size() - bom * 3));
    return out;
}
} // namespace

QString safeName(const QString &name) { return fromUtf8(pxs::core::osc::safeName(utf8(name))); }

struct SegmentNamer::Impl {
    pxs::core::osc::SegmentNamer namer;
};

SegmentNamer::SegmentNamer() : d(std::make_unique<Impl>()) {}
SegmentNamer::~SegmentNamer() = default;
SegmentNamer::SegmentNamer(const SegmentNamer &other) : d(std::make_unique<Impl>(*other.d)) {}
SegmentNamer &SegmentNamer::operator=(const SegmentNamer &other)
{
    if (this != &other) d = std::make_unique<Impl>(*other.d);
    return *this;
}
SegmentNamer::SegmentNamer(SegmentNamer &&) noexcept = default;
SegmentNamer &SegmentNamer::operator=(SegmentNamer &&) noexcept = default;

QString SegmentNamer::take(const QString &scope, const QString &name) { return fromUtf8(d->namer.take(utf8(scope), utf8(name))); }

QStringList uniqueSegments(const QStringList &names)
{
    std::vector<std::string> in;
    in.reserve(std::size_t(names.size()));
    for (const QString &n : names) in.push_back(utf8(n));
    QStringList out;
    for (const std::string &s : pxs::core::osc::uniqueSegments(in)) out << fromUtf8(s);
    return out;
}

} // namespace osc
