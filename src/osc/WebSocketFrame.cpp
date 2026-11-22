#include "WebSocketFrame.h"

#include <pxs/core/websocket/frame.h>

namespace pxs::websocket {

namespace {
const std::uint8_t *bytesOf(const QByteArray &b) { return reinterpret_cast<const std::uint8_t *>(b.constData()); }
} // namespace

QByteArray acceptKey(const QByteArray &key)
{
    const std::string accept = core::websocket::acceptKey(std::string_view(key.constData(), std::size_t(key.size())));
    return QByteArray(accept.data(), qsizetype(accept.size()));
}

QByteArray encode(int opcode, const QByteArray &payload)
{
    const auto frame = core::websocket::encode(opcode, bytesOf(payload), std::size_t(payload.size()));
    return QByteArray(reinterpret_cast<const char *>(frame.data()), qsizetype(frame.size()));
}

int decode(QByteArray &buffer, int *opcode, QByteArray *payload, quint64 maxPayload)
{
    core::websocket::Frame frame;
    std::size_t consumed = 0;
    switch (core::websocket::decode(bytesOf(buffer), std::size_t(buffer.size()), maxPayload, frame, consumed)) {
    case core::websocket::DecodeStatus::NeedMore: return 0;
    case core::websocket::DecodeStatus::Drop: return -1;
    case core::websocket::DecodeStatus::Frame: break;
    }
    *opcode = frame.opcode;
    *payload = QByteArray(reinterpret_cast<const char *>(frame.payload.data()), qsizetype(frame.payload.size()));
    buffer.remove(0, qsizetype(consumed));
    return 1;
}

} // namespace pxs::websocket
