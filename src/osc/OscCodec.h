#pragma once
// Qt-facing OSC API: packet encoding, decoding, and address-pattern matching are implemented by pxs-core.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>
#include <vector>

namespace osc {

struct Message {
    QString address;
    QString types;     // type tags without the leading ','
    QVariantList args; // int, float (double), QString, bool, QByteArray (blob), invalid (nil / impulse)
};

QByteArray encode(const Message &m);
// Decodes a message or a bundle (flattened, nested bundles included). Returns false if malformed.
bool decode(const QByteArray &packet, std::vector<Message> &out);
// OSC 1.0 address pattern matching (* ? [abc] [!a-z] {foo,bar}), segment by segment
bool match(const QString &pattern, const QString &address);
// Name usable as one segment of an OSC address
QString safeName(const QString &name);
// The segment of each effect of a layer (names made safe; a repeated name gets _2, _3…): the same in OSC addresses,
// timeline parameters and snapshot times
QStringList uniqueSegments(const QStringList &names);
// Segments unique within a scope (a container's path), taken in order: a repeated name gets _2, _3…, and a suffix
// another name already gave itself ("Video", "Video", "Video 2") is skipped, never shared
class SegmentNamer
{
public:
    SegmentNamer();
    ~SegmentNamer();
    SegmentNamer(const SegmentNamer &);
    SegmentNamer &operator=(const SegmentNamer &);
    SegmentNamer(SegmentNamer &&) noexcept;
    SegmentNamer &operator=(SegmentNamer &&) noexcept;
    QString take(const QString &scope, const QString &name);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace osc
