#include "OscCodec.h"

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    osc::Message message{"/layer/1/opacity", "f", {0.5}};
    std::vector<osc::Message> decoded;
    CHECK(osc::decode(osc::encode(message), decoded));
    CHECK(decoded.size() == 1);
    if (decoded.size() == 1) {
        CHECK(decoded[0].address == message.address);
        CHECK(decoded[0].types == "f");
        CHECK(decoded[0].args.size() == 1 && qAbs(decoded[0].args[0].toDouble() - 0.5) < 1e-6);
    }
    // A truncated or hostile packet is refused, never read past its end
    std::vector<osc::Message> refused;
    CHECK(!osc::decode(QByteArray("/x\0\0,f\0\0\x3f", 9), refused));
    CHECK(osc::match("/layer/*/opacity", "/layer/1/opacity"));
    CHECK(!osc::match("/layer/[0-3]/opacity", "/layer/7/opacity"));
    CHECK(osc::match("/{a,b}/x", "/b/x"));

    // What the Qt layer adds over pxs-core: QString in, QString out, with nothing lost on the way.
    // A name that starts with U+FEFF keeps it (QString::fromUtf8 alone would drop it: a saved address must not change)
    const QString bom = QString(QChar(0xFEFF)) + QStringLiteral("Layer");
    CHECK(osc::safeName(bom) == bom);
    CHECK(osc::safeName(QStringLiteral("  Layer 1\u00a0")) == QStringLiteral("Layer_1"));
    CHECK(osc::safeName(QString()) == QStringLiteral("_"));
    CHECK(osc::uniqueSegments({"Video", "Video", "Video 2"}) == QStringList({"Video", "Video_2", "Video_2_2"}));
    osc::SegmentNamer namer;
    CHECK(namer.take("/l", "a") == "a" && namer.take("/l", "a") == "a_2");
    osc::SegmentNamer copy = namer; // a namer can be copied: the copy goes on from where it was
    CHECK(copy.take("/l", "a") == "a_3" && namer.take("/l", "a") == "a_3");
    // Strings and blobs keep every byte; an address with non-ASCII characters is the same QString back
    osc::Message rich{QString::fromUtf8("/é/€"), "sb", {QString::fromUtf8("héllo €"), QByteArray("\0\xff\x01", 3)}};
    std::vector<osc::Message> back;
    CHECK(osc::decode(osc::encode(rich), back) && back.size() == 1);
    if (back.size() == 1) CHECK(back[0].address == rich.address && back[0].args == rich.args);
    // A refused packet adds nothing to the output
    std::vector<osc::Message> kept{rich};
    CHECK(!osc::decode(QByteArray("/x\0\0,i\0\0\x01", 9), kept) && kept.size() == 1);
    // `?` is one character, an emoji included
    CHECK(osc::match(QString::fromUtf8("/a/?"), QString::fromUtf8("/a/\xF0\x9F\x98\x80")));
    return failures ? 1 : 0;
}
