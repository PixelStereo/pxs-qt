# The address space and OSCQuery

`pxs::core::tree::Tree` (pxs-core, no Qt; `pxs::tree` is the CMake target that brings it with the bridge to Qt's types) is a tree of named nodes. A node is a container, or it carries a `Parameter` (see `parameters.md`). Two things
are derived from it, so nothing is written twice: what an incoming OSC message changes, and what OSCQuery says.

```cpp
using namespace pxs::core;
using namespace pxs::core::tree;
Tree tree;
auto opacity = std::make_unique<Parameter>("opacity", "Opacity", Parameter::Type::Float);
opacity->range(0, 1).bind(get, set);
tree.add("/layer/1/opacity", std::move(opacity)).description = "How opaque the layer is";

tree.apply("/layer/*/opacity", {Value(0.5)});   // every node the pattern names, as its access and limits allow
describeJson(tree);                              // the OSCQuery JSON of the whole tree (text)
attributeJson(tree, "/layer/1/opacity", "VALUE", out);  // one attribute (?VALUE); out is "{}" when it has no value
Tree::message(*tree.find("/layer/1/enable"));    // the OSC message that says what a parameter holds (the feedback)
```

## Three kinds of leaf, the same in every application

| Kind | State | Made with | OSCQuery |
|---|---|---|---|
| **parameter** | yes: read, written, saved, tells when it changes | `add(path, Parameter)`, or filled by hand | ACCESS 1 to 3, `VALUE`, `RANGE` |
| **action** | none: received, does something | `addAction(path, "i", handler)` | ACCESS 2, `TYPE`, no `VALUE` |
| **signal** (controller output) | none: the application emits it, nobody writes it | `addSignal(path, "f")`, then `fire(path, {args})` | ACCESS 1, `TYPE`, no `VALUE` |

```cpp
tree.addAction("/show/go", "i", [&](const ValueList &a) { goTo(int(a.front().number().value_or(0))); return true; }, "Go to a cue");
tree.addSignal("/detect/peak", "f", "A peak was heard");
tree.setOutput([&](const osc::Message &m) { sendToClients(m); });   // pxs::core::osc::Message  // set once, by the service that sends
tree.fire("/detect/peak", {Value(0.8)});     // a signal goes to the output
tree.changed("/layer/1/opacity");     // a parameter's new value goes to the output (the feedback)
```

No extension tells them apart: OSCQuery's own `ACCESS` and `VALUE` do. A leaf with a `VALUE` is a parameter, so a
parameter always has a value that can be read (ACCESS 1 or 3). A leaf with no `VALUE` is an action when it is written
(ACCESS 2) and a signal when it is read (ACCESS 1). A setter with no getter is therefore declared as an action, never as
a parameter. Any OSCQuery client reads the three the same way.

An action with no argument has the type `N`. Everything an application says (a signal, a parameter's feedback) goes
through the same output, so a client that listens hears all three the same way. A `Trigger` parameter is an action.

A node can also be filled in by hand, for an application that already has its own way to read and write a value:

```cpp
auto &xy = tree.add("/layer/1/position");
xy.type = "ff"; xy.access = 3; xy.description = "Position";
xy.range = {...}; xy.get = [&] { return ValueList{Value(x), Value(y)}; }; xy.set = [&](const ValueList &v) { ...; return true; };
xy.attributes["MYAPP_DEFAULT"] = [] { return std::optional<std::string>("[0.5,0.5]"); };   // JSON as text; an extension of OSCQuery, asked with ?MYAPP_DEFAULT
```

## What it takes from Ossia

Ossia (libossia) holds a device as a tree of nodes with a parameter on the leaves, and exposes it through protocols
(OSC, OSCQuery). That is kept: the tree is the single source, OSCQuery and OSC are views of it, an address is a path, a
pattern names several nodes.

## What it leaves out, on purpose

Ossia has more than these applications use, and a part of it is complexity that costs more than it gives:

- **Bounding modes** (clip, wrap, fold…): a parameter's limits keep the value within them, and `ramp` already says how a
  fade goes round an angle.
- **Units** (a colour as RGB or HSV, an angle in degrees or radians): a colour is its components
  (`colorComponent`, `pointComponent`), the label says the unit.
- **Repetition filters and callbacks on every node:** a write tells the others once, through `ChangeFeed`; the one
  listener (the feedback) reads what was named.
- **Tags, priorities, critical values.**

## The OSCQuery description

`json(path)` gives `FULL_PATH`, `CONTENTS` (the children), and for a parameter `TYPE` (`f`, `i`, `T`, `s`, `N` for an
action), `ACCESS` (1 read, 2 write, 3 both), `RANGE` (`MIN` and `MAX`, or `VALS` for a choice), `VALUE` (when readable)
and `DESCRIPTION`. `hostInfo(name, port, extensions)` gives the `/?HOST_INFO` answer (each application says which extensions it supports). The HTTP server and the announcement (mDNS)
belong to the application; the tree gives them their content.

## What the HTTP server answers

Following the OSCQuery proposal: the node's JSON for `/path`, one attribute for `/path?NAME` (`{"VALUE": [..]}`), the host for
`/?HOST_INFO`, 404 for a path that does not exist. A node that has no such attribute answers an **empty object**
(`/show?TYPE` on a container, `/detect/peak?VALUE` on a signal). **204** is for a `VALUE` asked of what cannot be read
(ACCESS 0 or 2), and for an attribute the server does not have at all (not in `EXTENSIONS`). A bounded parameter that is
written says `CLIPMODE: both`: its value is kept within its limits.

`VALUE` and `RANGE` follow the type tags, arrays included (`i[ff]s` gives `[3, [0.5, 1.5], "x"]`); `r` is a colour
(`"#RRGGBBAA"`), a blob and MIDI give `null`.

A node name cannot hold a space or any of `# * , ? [ ] { }` (they would break the address patterns): each becomes `_`, in
`add()` and in `find()`, so the same name finds the same node.

## What else a node can say

`EXTENDED_TYPE` (what each argument is: `color.rgb.r`, `position.cartesian.x`, `filepath`…) and `UNIT` (`distance.m`,
`angle.degree`, `gain.db`…) follow the proposal's lists (`extended_types.md`, `units.md`); `TAGS` for a client to filter by;
`DEFAULT_VALUE` (libossia's name) for what a reset puts back. A `Parameter` says them with `unit("gain.db")` and
`extendedType("filepath")`; a color or a point component says what it is by itself (`colorComponent(0)` is `color.rgb.r`,
`pointComponent(1)` is `position.cartesian.y`). A node filled in by hand has `extendedType`, `unit` (one per argument, `""`
for none: it says `null`) and `tags`. libossia's client reads the unit, the default and the tags (tried). The client
(`RemoteNode`) reads them too, and the proposal's shorthands: one value for every argument instead of an array, `null`
for none.

## Who can reach it

`OscService::setLocalOnly(true)` for an application that is for this computer only: UDP and HTTP/WebSocket are bound to the
loopback, nothing is announced on the network, and no page of another site may read the answers (no CORS header).
Otherwise any interface answers, and a page of another site may read (`setCorsOpen(false)` to refuse that alone). At most
128 connections; a request that has not said what it wants within 10 s is dropped.

## The WebSocket (the proposal's bi-directional side)

`OscService` serves it on the HTTP port (`HOST_INFO` says `WS_PORT` and, in `EXTENSIONS`, `LISTEN` and `PATH_CHANGED`):

- `{"COMMAND": "LISTEN", "DATA": "/layer/1/opacity"}` streams that address as binary OSC packets: a parameter that was written
  or told changed (from OSC, a mapping, the panel, or the client itself), a signal that was fired. The address is exact:
  `/layer` does not give `/layer/1/opacity`. `IGNORE` stops it. Actions say nothing.
- A binary frame is an OSC packet for the application: it is applied like a UDP one (`messageReceived` too).
- `PATH_ADDED` (`DATA`: the path, and the node's own description merged into the message) when a node is made, and
  `PATH_REMOVED` (`DATA`: the path) when one is removed; what happens within 50 ms is coalesced (nodes inside one that was
  made are said with it). `PATH_CHANGED` (the highest path that changed) when the tree is cleared or more than 64 things
  changed at once. libossia's client follows `PATH_ADDED` and `PATH_REMOVED` and ignores `PATH_CHANGED` (its handler is
  empty), so a node made later is seen by it only through those two; pxs's own client and NyxMap follow all three.
- A WebSocket can write, so a page of another origin is refused (403): only a native client (no `Origin`) or a page of this
  machine (localhost, a loopback or own address) gets in. At most 32 clients; a client more than 1 MB behind misses
  frames instead of making the application wait.

What is not there: `PATH_RENAMED` (the tree has no rename), `CRITICAL`, and TCP for the OSC itself.

Threads: an observer runs on the thread that wrote; `OscService` hands what a socket must send to its own thread.

## Applying a message

`apply()` writes a parameter when its access allows it and the argument fits its type: a number for `Float` and `Int` (an
`Int` is rounded), a switch for `Bool`, a key (or its index) for `Choice`, a string for `Text`, any message for a
`Trigger`. A value that does not fit is ignored, never half applied; the parameter keeps it within its limits.

## Making an application reachable: `pxs::oscquery` and `pxs::bind`

Every application is reachable the same way. `OscService` opens the UDP port (what arrives is applied to the tree), the
OSCQuery HTTP side and the mDNS announcement. What the application says goes back over UDP: a parameter's feedback
(`changed`) to the senders that wrote to it and to the targets, a signal (`fire`) to the targets only (`addTarget()`:
the application's own destinations), so that writing a parameter never subscribes anyone to a stream.

```cpp
pxs::core::tree::Tree tree;
pxs::OscService service(&tree, "MyApp");
service.start(oscPort, httpPort);                 // 0: any free port
```

An interface that already has its widgets exposes them in one line each (`pxs/Bind.h`); writing from outside goes
through the widget, so it does what a click does, and a user's change tells the tree (the feedback):

```cpp
pxs::bindToggle(tree, "/osc/send", sendButton, "Send the signals");     // parameter
pxs::bindNumber(tree, "/detection/confidence", field, "Confidence");    // parameter, range from the field
pxs::bindChoice(tree, "/analysis/size", combo);                         // parameter, keys = item texts in lowercase
pxs::bindAction(tree, "/video/stop", stopButton);                       // action
tree.addSignal("/face/head/yaw", "f");                                  // signal, then tree.fire(...)
```

What an application exposes is its choice, but never less than what a user can do in the interface: every control is a
parameter or an action, every value the application computes for the outside is a signal.
