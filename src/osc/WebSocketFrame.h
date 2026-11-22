#pragma once
// The frames of a WebSocket (RFC 6455) as a small server needs them: text and control frames, no fragmentation, no extensions.
// A client's frames are masked, a server's are not. Qt-facing API of pxs-core's pxs::core::websocket, which does the work.

#include <QByteArray>
#include <QtGlobal>

namespace pxs::websocket {
enum Opcode { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

// The Sec-WebSocket-Accept of a handshake, from the client's Sec-WebSocket-Key
QByteArray acceptKey(const QByteArray &key);

// A server frame to send (`opcode`, `payload`)
QByteArray encode(int opcode, const QByteArray &payload);

// The first frame of `buffer`, taken out of it. Returns 1 when one was read (opcode, payload), 0 when more bytes are needed, -1
// when the peer must be dropped (a frame above `maxPayload`, a fragmented message, an unmasked client frame, reserved bits,
// an undefined opcode, a control frame of more than 125 bytes)
int decode(QByteArray &buffer, int *opcode, QByteArray *payload, quint64 maxPayload);
} // namespace pxs::websocket
