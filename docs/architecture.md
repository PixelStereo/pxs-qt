# Common architecture: the tree, the leaves, the mappings

## Layers: pxs-core and pxs-qt

The wire formats live in [`pxs-core`](https://github.com/PixelStereo/pxs-core), a C++17 library with no Qt (submodule
`third_party/pxs-core`): OSC packets, address patterns and address-segment names, Art-Net/sACN frames, WebSocket frames and
the handshake key. They are pure functions of their input, bounded against hostile packets, and verified by golden vectors,
a hostile-input corpus under the sanitizers and fuzzing. A tool without Qt links `pxs-core::osc`, `pxs-core::dmx` or
`pxs-core::websocket` directly.

This repository keeps everything that needs Qt, and the Qt-facing API of those formats (`osc::Message`, `pxs::dmx::Frame`,
`pxs::websocket::decode`, …): each is an **adapter** that converts `QString`/`QByteArray`/`QVariant` at the boundary and
calls pxs-core. No pxs-core type or header appears in a pxs-qt header, so applications did not change, and there is one
implementation of each format.

Still here because their interfaces expose Qt types, so that moving them is a redesign of the API, not an extraction:
transport (sockets, WebSocket server, mDNS), the tree and parameters, OSCQuery, `ChangeFeed`, and MIDI (`parse()` fills an
`InteractionEvent`). A module moves to pxs-core only once its public types are portable.

**Versions.** pxs-core is pinned by release tag (`scripts/check-core-pin.sh`, run by CI, refuses anything else), so a
commit that is not on pxs-core's `main` can never be pinned and become unreachable. Clone with
`git clone --recurse-submodules`; `git submodule update --init --recursive` fetches both levels (`submodules: recursive` in a
workflow). To move to a new pxs-core: see its `RELEASING.md`.

This document fixes what every Pixel Stereo application has in common to expose and drive its settings. It serves as a
contract: an application, a parameter list, a menu or a mapping behaves the same way everywhere. Points marked **to be
validated** are waiting for an answer.

## Vocabulary

| Word | Meaning | In `pxs::tree` |
|---|---|---|
| **Node** | an element of the tree, with a name and a path (`/layer/1/opacity`) | `Node` |
| **Branch** | a node that contains other nodes, with no value or action | `Kind::Container` |
| **Leaf** | a node that contains nothing and does something | `Kind::Parameter`, `Action`, `Signal` |
| **Parameter** | a leaf **with a state**: it is read, written and saved; it says when it changes | `add(path, Parameter)`, or filled in by hand |
| **Action** | a leaf **without a state**: it is received and triggers something | `addAction` |
| **Signal** (controller) | a leaf **without a state**: the application emits it, nobody writes it; its value is not remembered | `addSignal`, `fire` |

A leaf has no children: a node that is given some becomes a branch.
A hardware controller (MIDI, sensor, incoming OSC) is a **signal**.

## The contract: what each kind of node does, the same everywhere

| | Branch | Parameter | Action | Signal |
|---|---|---|---|---|
| **OSC received** | no effect | writes the value (within its limits) | runs the action | refused |
| **OSC sent** | – | the feedback (`changed`) to whoever wrote and to the destinations | – | the stream (`fire`) to the destinations only |
| **OSCQuery** | `CONTENTS` | `TYPE`, `ACCESS` 1 to 3, `RANGE`, `VALUE` | `ACCESS` 2, no `VALUE` | `ACCESS` 1, no `VALUE` |
| **Saved** | no | yes, by address (`parametersToJson`) except `transient` | no | no |
| **Reset** | – | yes, if it has a default value | – | – |
| **Lock** | – | yes: no writes at all (OSC, mapping, interface) | yes | – |
| **Mapping source** | – | no (for now) | – | **yes** |
| **Mapping target** | – | **yes** | **yes** (triggered) | – |
| **Row in the panel** | a folder that can be collapsed | a field according to the type: checkbox, slider, menu, text | a button | an activity indicator |
| **Context menu** | – | Reset · Copy OSC address · Lock · Animate · Link to a signal | Copy OSC address · Lock | Copy OSC address |

A parameter's field comes from its type and its range; the application does not redraw it.
Actions remain buttons that **the application places itself**; there is no automatic action menu.

## Mapping: NyxMap's Interaction, for every application

Mapping already exists in NyxMap: it is the **Interaction** panel (its model is in `Interactions.h`), with MIDI learn. We
reinvent nothing: we **extract** this model into `pxs`, then every application, NyxMap included, uses it from `pxs`.

An **interaction** takes the value of a **source** (keyboard, MIDI, OSC, internal, audio; a controller is a signal),
passes it through a **filter chain** (Remap, Average, Attack Release, Damper, Curve, Math, Threshold, Toggle,
Envelope…), applies the master level, and sends it to the **target**, which receives it in its own units: a number, a
switch (from 0.5), a choice (by its index), an action (triggered when crossing 0.5).
- **Pick-up or jump** when the target has been moved by something else.
- **Action mode**: conditions on the source value, and what happens when they become true or false.
- Colors and XY points: each channel has its own chain.
- **Groups** to organize the list.
- Signals are never remembered; only the interaction is.

**Decisions made**
- **Storage**: `pxs` provides the JSON (`Interaction::toJson` / `fromJson`); **the application decides where it lives**
  (project or settings).
- **Lock**: it is **saved** along with what the application saves. `pxs` provides `lockedToJson` / `lockedFromJson`
  (the list of locked addresses).
- **Animate**: the `animatable` flag and the request (the context menu offers Animate; the application decides what to do
  with it: NyxMap's timeline, nothing elsewhere).

## What exists in NyxMap and moves into `pxs`

Your four ideas are in fact things NyxMap already has. The work is an **extraction**, in steps:

| Step | Content | State |
|---|---|---|
| 1 | the interaction model and filter engine (`Interactions.h`, `Anim.h`, graph): `pxs::interaction` | **done** (tests: filters, graph) |
| 2 | MIDI input (`Midi.h`, RtMidi) and MIDI learn | to do |
| 3 | the Interaction panel and the parameter panel, made generic over the tree | to do |
| 4 | presets (snapshots) by address | **done**: `pxs::Preset` (`capture`, `recall`), see below; NyxMap adopts it in step 6 |
| 5 | the server's `LISTEN` WebSocket | **done in `OscService`** (`LISTEN`/`IGNORE`, binary OSC, `PATH_CHANGED`, `PATH_ADDED`, `PATH_REMOVED`); NyxMap keeps its own until step 6 |
| 6 | NyxMap uses the `pxs` versions and removes its copies | to do, last |

MpOsc's per-signal filter (inversion, attack, release) becomes a `pxs` interaction (step 3 for its interface).

## What is already done, and what comes next

| | State |
|---|---|
| tree, three kinds of leaves, single output, OSCQuery | done (v0.13.0) |
| network service (UDP, HTTP, mDNS) | done (v0.13.0) |
| binding of existing widgets (`pxs::bind`) | done (v0.13.0) |
| label, lock, reset, observers | done, to be released (v0.14.0) |
| interaction model (extracted from NyxMap) | done, to be released |
| MIDI, learn, panels, presets, `LISTEN` | see the steps table |

## Presets by address: `pxs::Preset`

A preset holds parameter **values** (by address, in the format of OSCQuery's `VALUE`) and **actions to run** on recall,
with their arguments. Recall sets the values first, then runs the actions in order: the media is set to play, then
rewound. A locked parameter keeps its value, a locked action is not run, an address the tree no longer has is counted
without error (`RecallResult`).

- By default: every readable, writable and saved parameter (`Parameter::isStored`), never a `transient` parameter (a
  show state) nor an action. A selection of addresses narrows it.
- **Actions are chosen by the preset**, not "all of them": recalling a preset does not run `stop` or `go` everywhere.
- The format is JSON (`Preset::toJson` / `fromJson`); the application decides where it lives. A preset's fade, and what
  is not a parameter (animations, NyxMap's interactions), stay with the application.

**Rewinding is called `rewind`, everywhere**: the action leaf of a media, a shader, a slideshow, a timecode.
"Restart" does not exist: it is `play = true` then `rewind`, which a preset holds with both.

## Protocols: what is in `pxs`, what is not

`pxs` carries what **controls and exposes parameters**: OSC and OSCQuery, MIDI, DMX. Each is a module an application links
only when it needs it. It does not carry what **transports media** (OMT, NDI, capture): that stays in the code of the
applications that draw or play, since it needs frames, decoders and libraries the other applications must not pay for.

- **`pxs::dmx`** is the codec alone (Art-Net, sACN): frames in, frames out, strictly checked. What a channel drives is
  the application's.
- **`pxs::dmxout`** is the output side, so that NyxMap, Cold-Flame and every other application describe, save and send their
  DMX the same way. An `Output` carries what consoles and QLC+ offer: Art-Net or sACN, **unicast** or the protocol's **group**
  (Art-Net broadcast on the card's network, sACN multicast), the network card, a port, a rate (1 to 44 Hz for Art-Net, the protocol's limit, up to 120 for sACN), the sACN priority and
  source name, **full or partial** frames (partial: only the channels the application hands over), and a map from the
  application's universes to the network's (Art-Net counts from 0, sACN from 1). `route()` applies it; an application that
  already knows each output's universes addresses `(output, universe)` itself. The `Sender` runs on its own thread, so lights
  get a steady stream whatever the application's frame rate: a sequence for each universe, the sACN identity, a priority for
  each channel the application owns (so a receiver takes the others from another source), and the three terminated frames when
  an output goes off. Applications are before version 1: no legacy format is kept, a project written for an older one is
  edited by hand. Still to come: the panel that edits a list of outputs, and Art-Net node discovery (ArtPoll) to pick a destination.
- **`pxs::dmxoutview`** is the panel that edits a list of `pxs::dmx::Output`: a table (on, name, protocol, what the output is
  doing, from the `Sender`'s state) and the settings of the selected one, each shown only where it applies (the addresses
  in unicast, the priority and source name for sACN, the universe map only for an application that routes). It keeps no outputs: the
  application hands them over and is told each change, with the text an undo step can carry.
- **`pxs::dmxnet`** (from DMXPulse) is what an application needs to watch a DMX network or to send onto it: a `Monitor`
  (sockets shared with other lighting software, what each source sends, the merge of the sources of a universe) and a
  `Transmitter` (the sequence, the sACN identity, multicast out of the chosen interface) that lets the monitor recognise the
  application's own frames. It carries no interface: DMXPulse draws the grid, and Cold-Flame can show the same monitor as
  an optional panel.
- **`pxs::dmxview`** is the grid of a universe and its channel editor, from DMXPulse. It holds no output of its own: the
  application pushes what to show (`setState`: what the network carries, what the other devices alone send, the output,
  which channels are set here) and is told what the user chose (`outputChosen`, `releaseRequested`). DMXPulse's main
  window is made of it; Cold-Flame can open the same widget, fed by a `pxs::dmxnet` Monitor, as an optional panel.
- **`pxs::midi`** has the messages (an interaction's event, learn, the bytes to send), the ports, and `Inputs`, which
  opens every input while a MIDI interaction exists or the application is learning and looks again for controllers. The
  ports need an RtMidi that `pxs` does not carry: the application names its target (`set(PXS_RTMIDI_TARGET rtmidi)`
  before `add_subdirectory`, as NyxMap patches its own), or a system RtMidi is found. A MIDI controller is a **signal**.

## Other ideas, not yet decided

1. **An "internal" flag** on a node, so that what only serves the interface is not exposed to the outside.
2. **Rate limiting** of the signals sent (for example at most 30 per second per address).
3. **Undo / redo** for changes made in the panel.
