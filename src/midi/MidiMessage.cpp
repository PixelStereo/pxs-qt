#include "pxs/Midi.h"

#include <pxs/core/midi/message.h>

namespace pxs::midi {

// The bytes of the messages, and how they are read, are pxs-core's (pxs::core::midi); this layer only fills the
// interaction's event. The kinds have the same values on both sides.
static_assert(int(MidiMessageKind::ControlChange) == int(core::midi::Kind::ControlChange) &&
              int(MidiMessageKind::Note) == int(core::midi::Kind::Note) &&
              int(MidiMessageKind::PitchBend) == int(core::midi::Kind::PitchBend) &&
              int(MidiMessageKind::ProgramChange) == int(core::midi::Kind::ProgramChange));

bool parse(const uint8_t *m, size_t n, InteractionEvent &e)
{
    core::midi::Message message;
    if (!core::midi::parse(m, n, message)) return false;
    e.message = static_cast<MidiMessageKind>(message.kind);
    e.number = message.number;
    e.value = message.value;
    e.source = InteractionSource::Midi;
    e.channel = message.channel;
    return true;
}

std::vector<uint8_t> controlChange(int channel, int controller, double value) { return core::midi::controlChange(channel, controller, value); }
std::vector<uint8_t> noteOn(int channel, int note, double velocity) { return core::midi::noteOn(channel, note, velocity); }
std::vector<uint8_t> noteOff(int channel, int note) { return core::midi::noteOff(channel, note); }
std::vector<uint8_t> pitchBend(int channel, double value) { return core::midi::pitchBend(channel, value); }
std::vector<uint8_t> programChange(int channel, int program) { return core::midi::programChange(channel, program); }

void learn(Interaction &it, const InteractionEvent &e)
{
    it.source = InteractionSource::Midi;
    it.midiPort = e.port;
    it.midiChannel = e.channel;
    it.midiMessage = e.message;
    it.midiNumber = e.message == MidiMessageKind::PitchBend ? 0 : e.number;
}

Inputs::Inputs(QObject *parent) : QObject(parent)
{
    m_scan.setInterval(2000);
    connect(&m_scan, &QTimer::timeout, this, &Inputs::rescan);
}

Inputs::~Inputs() { m_inputs.clear(); }

void Inputs::setWanted(bool on)
{
    if (on == m_wanted) return;
    m_wanted = on;
    if (!on) {
        m_scan.stop();
        m_inputs.clear();
        m_names.clear();
        m_tried.clear();
        return;
    }
    m_scan.start();
    rescan();
}

void Inputs::rescan()
{
    if (!m_wanted) return;
    const QStringList names = inputNames();
    // A port that failed to open is not tried again every scan (that would close and reopen all the others, losing their
    // messages), only when the list of devices changes
    if (names == m_tried) return;
    m_inputs.clear();
    m_names.clear();
    for (const QString &name : names) {
        auto in = std::make_unique<Input>();
        QString err;
        const bool ok = in->open(name, [this, name](const uint8_t *m, size_t n) {
            InteractionEvent e;
            e.port = name;
            if (!parse(m, n, e) || !m_callback) return;
            m_callback(e);
            if (e.message == MidiMessageKind::ProgramChange) { // an action: up, then down
                e.value = 0.0;
                m_callback(e);
            }
        }, &err);
        if (!ok) continue;
        m_inputs.push_back(std::move(in));
        m_names << name;
    }
    m_tried = names;
}

} // namespace pxs::midi
