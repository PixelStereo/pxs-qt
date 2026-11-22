#include "pxs/Midi.h"

#include <QCoreApplication>
#include <cmath>
#include <cstdio>

using namespace pxs::midi;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)
static bool near(double a, double b) { return std::abs(a - b) < 1e-3; }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // What is built is read back
    {
        const auto cc = controlChange(3, 7, 1.0);
        InteractionEvent e;
        CHECK(parse(cc.data(), cc.size(), e) && e.source == InteractionSource::Midi && e.channel == 3 &&
              e.message == MidiMessageKind::ControlChange && e.number == 7 && near(e.value, 1.0));
        const auto on = noteOn(16, 60, 64 / 127.0);
        CHECK(parse(on.data(), on.size(), e) && e.channel == 16 && e.message == MidiMessageKind::Note && e.number == 60 &&
              near(e.value, 64 / 127.0));
        const auto off = noteOff(1, 60);
        CHECK(parse(off.data(), off.size(), e) && e.message == MidiMessageKind::Note && e.value == 0.0);
        const auto bend = pitchBend(2, 0.5);
        CHECK(parse(bend.data(), bend.size(), e) && e.message == MidiMessageKind::PitchBend && near(e.value, 0.5));
        const auto program = programChange(1, 12);
        CHECK(parse(program.data(), program.size(), e) && e.message == MidiMessageKind::ProgramChange && e.number == 12 && e.value == 1.0);
    }

    // A note on at velocity 0 is an off; out-of-range arguments are held within MIDI's
    {
        const uint8_t quiet[] = {0x90, 60, 0};
        InteractionEvent e;
        CHECK(parse(quiet, 3, e) && e.value == 0.0);
        const auto high = controlChange(99, 500, 5.0);
        CHECK(high[0] == 0xBF && high[1] == 127 && high[2] == 127);
    }

    // A value that is not a number is 0, one beyond the range is held (what is sent never leaves MIDI's range)
    {
        const double nan = std::nan(""), inf = HUGE_VAL;
        CHECK(controlChange(1, 7, nan)[2] == 0 && controlChange(1, 7, inf)[2] == 127 && controlChange(1, 7, -inf)[2] == 0);
        CHECK(pitchBend(1, nan)[1] == 0 && pitchBend(1, nan)[2] == 0 && pitchBend(1, inf)[1] == 127 && pitchBend(1, inf)[2] == 127);
    }

    // Not DMX-like noise: clock, SysEx, aftertouch, short messages
    {
        InteractionEvent e;
        const uint8_t clock[] = {0xF8}, sysex[] = {0xF0, 0x7F, 0xF7}, touch[] = {0xD0, 5}, shortCc[] = {0xB0, 7};
        CHECK(!parse(clock, 1, e) && !parse(sysex, 3, e) && !parse(touch, 2, e) && !parse(shortCc, 2, e) && !parse(nullptr, 0, e));
    }

    // Learn: the interaction listens for what came in, and matches it
    {
        InteractionEvent e;
        e.port = "Pad";
        const auto cc = controlChange(2, 21, 0.25);
        CHECK(parse(cc.data(), cc.size(), e));
        Interaction it = Interaction::made(InteractionSource::Osc);
        learn(it, e);
        CHECK(it.source == InteractionSource::Midi && it.midiPort == "Pad" && it.midiChannel == 2 && it.midiNumber == 21);
        CHECK(it.matches(e));
        e.number = 22;
        CHECK(!it.matches(e));
    }

    // Without a backend (or without a port) the lists are empty and the inputs open nothing; with one they just work
    {
        setClientName("pxs-test");
        Inputs inputs;
        int heard = 0;
        inputs.setCallback([&](const InteractionEvent &) { ++heard; });
        CHECK(!inputs.wanted());
        inputs.setWanted(true);
        CHECK(inputs.wanted());
        if (!available()) CHECK(inputs.open().isEmpty() && inputNames().isEmpty() && outputNames().isEmpty());
        inputs.setWanted(false);
        CHECK(!inputs.wanted() && inputs.open().isEmpty());
        Output out;
        QString err;
        if (!available()) CHECK(!out.open({}, &err) && !err.isEmpty());
    }

    if (failures) return 1;
    std::printf("midi passed%s\n", available() ? " (with a MIDI backend)" : " (no MIDI backend)");
    return 0;
}
