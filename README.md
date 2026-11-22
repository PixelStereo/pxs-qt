# pxs-qt

**One address space, every protocol, one look.** The Qt library shared by the Pixel Stereo applications: NyxMap, AtemRemoteOSC,
ISFLab, DMXPulse, Cold-Flame, MpOsc. Declare a parameter once, and it is on the network (OSC, OSCQuery), in the
interface, in the presets, under the mappings. Qt 6.4+, C++17, AGPL-3.0.

```cpp
pxs::Tree tree;
auto opacity = std::make_unique<Parameter>("/layer/1/opacity", "Opacity", Parameter::Type::Float);
opacity->range(0, 1).bind(get, set);
tree.add("/layer/1/opacity", std::move(opacity)).description = "How opaque the layer is";

pxs::OscService osc(&tree, "MyApp");
osc.start(0, 0);   // OSC over UDP, OSCQuery over HTTP and WebSocket, announced on the network (mDNS)
```

That is all it takes for any OSCQuery client to find the app, read its tree, listen to its changes and drive it.

## What is in the box

**Expose**

| Module | What it gives you |
|---|---|
| `pxs::params` | how a parameter is declared (type, range, unit, default) and how the others are told it changed |
| `pxs::tree` | the address space: nodes, parameters, **actions** (received, stateless) and **signals** (emitted, stateless), OSC applied to it, **presets by address** |
| `pxs::osc` | Qt-facing OSC API (and the WebSocket frames); implemented over [pxs-core](https://github.com/PixelStereo/pxs-core) |
| `pxs::oscquery` | OSCQuery server on the proposal's terms, with WebSocket `LISTEN`, `PATH_ADDED`/`REMOVED`/`CHANGED`, `EXTENDED_TYPE`, `UNIT`, mDNS |
| `pxs::oscclient` | the other way round: find devices, read their tree, follow their values, write to them |

**Control**

| Module | What it gives you |
|---|---|
| `pxs::interaction` | a value driven from outside (key, MIDI, OSC, signal, audio) through a chain of filters, with pick-up and an Action mode (NyxMap's Interaction panel, for everyone) |
| `pxs::midi` | MIDI messages as events (parse, build, **learn**), the ports, the inputs to listen to; bring your own RtMidi (`PXS_RTMIDI_TARGET`) or use the system one |
| `pxs::dmx` | Art-Net and sACN frames, encoded and decoded, strictly checked; Qt Core only |
| `pxs::dmxout` | the **DMX outputs** of an application, the same everywhere: an `Output` (Art-Net or sACN, unicast or the protocol's group, network card, port, rate, sACN priority and source name, full or partial frames, a map from your universes to the network's; validated, saved as JSON), `route()` to apply it, and a `Sender` that streams the universes on a thread of its own with a sequence per universe, per-channel sACN priorities and clean end of stream |
| `pxs::dmxnet` | a **Monitor** (what every source sends, the HTP / LTP / sACN priority merge) and a **Transmitter** that recognises its own frames |
| `pxs::bind` | expose an interface's own widgets as parameters and actions |
| `pxs::controls` | a web page of controls (free grid, pages, edit mode, SVG widgets) and the server behind it |

**Look**

| Module | What it gives you |
|---|---|
| `pxs::theme` | colour tokens, the `graphite` and `nyxmap` dark palettes, the shared style sheet |
| `pxs::dmxoutview` | the **DMX outputs panel**: add, switch on and edit a list of outputs (protocol, unicast or group, network card, addresses, port, rate, sACN priority and name, full or partial frames, an optional universe map), with what each one is doing; the same in every application that sends DMX |
| `pxs::filterview` | the **filter chain of a signal**: a card per filter to fold, rename, set and drag to another place (its curve drawn for a Curve, its ADSR in the hand for an Envelope), one bar for the signal coming in and one for the signal going out, whose range follows the chain (a Remap's Out, a Clamp's ends); hooks for presets and undo |
| `pxs::curveview` | **curves on screen**: the keys of an animation track (click, drag, draw over, easings, Bézier handles, hold) and the ADSR envelope to take in the hand |
| `pxs::widgets` | number fields (drag, type, snap) and the colour picker |
| `pxs::dmxview` | the **512-channel grid** of a DMX universe with its editor: click a channel (or several) and see the network's level above the level to send, set it by click, drag, wheel or typing; or read-only, as a monitor |

**Ship**

| | |
|---|---|
| `cmake/PixelStereoBuildId.cmake`, `scripts/build_id.py` | the version in the name: `0.2.1`, `0.2.1+3`, `0.2.1+3+` |
| `packaging/windows.nsi`, `scripts/package-macos.sh` | the Windows installer, the signed macOS zip |
| `docs/conventions.md` | how the applications are named, built, packaged and released |

Control protocols live here; **media transport** (OMT, NDI, capture) does not: it stays in the applications that draw or
play. See `docs/architecture.md` for the reasoning, the vocabulary and what comes next.

## Using it

Pin a tag, never a branch:

```cmake
include(FetchContent)
FetchContent_Declare(pxs
  GIT_REPOSITORY https://github.com/PixelStereo/pxs-qt.git
  GIT_TAG v0.35.0
  GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(pxs)
include(${pxs_SOURCE_DIR}/cmake/PixelStereoBuildId.cmake)
target_link_libraries(MyApp PRIVATE pxs::tree pxs::oscquery pxs::theme pxs::widgets)
```

Every module is its own target: link only what the application needs. The Qt modules link `pxs-core` internally;
`pxs-core::osc` itself has no Qt dependency. Initialize nested modules when cloning `pxs-qt`:

```sh
git clone --recurse-submodules https://github.com/PixelStereo/pxs-qt.git
```

## Read on

| | |
|---|---|
| `docs/architecture.md` | the contract: parameter, action, signal; mapping; presets; which protocols belong here |
| `docs/parameters.md` | declaring a parameter |
| `docs/oscquery.md` | what the server answers, the WebSocket, the attributes, who can reach it |
| `docs/conventions.md` | names, builds, downloads |

## Migrating an application

An application migrates by removing its own copy of a module, linking the pxs one, and keeping its includes: `pxs::osc`
is byte for byte the codec MpOsc and NyxMap carried (`#include "OscCodec.h"`, namespace `osc::`).
