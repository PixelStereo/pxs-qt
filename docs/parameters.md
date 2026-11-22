# Declaring a parameter

`pxs::core::tree::Parameter` (pxs-core: no Qt; `pxs::params` is the CMake target that brings it) is how an application says what can be read, written, saved and driven from outside (OSC, a timeline, a
snapshot), and how the others are told when it changes. It comes from NyxMap, where every control is declared once and
everything else (the interface, OSC, OSCQuery, files) derives from that declaration.

## Declare it where it is made

```cpp
double opacity = 0.5;                                    // where the value lives
Parameter p("opacity", "Opacity", Parameter::Type::Float); // address in its holder, label, kind
p.range(0, 1).byDefault(Value(0.5)).animatable()
 .bind([&] { return Value(opacity); },                    // read
       [&](const Value &v) { opacity = v.number().value_or(0.0); }); // write
```

| Call | Says |
|---|---|
| `Type` | `Float`, `Int`, `Bool`, `Choice`, `Text`, `Trigger` |
| `range(min, max)`, `limits(lo, hi)` | what is shown (bars, OSC RANGE) and what a value is kept within |
| `step(s)`, `values({…})`, `choices({…})` | granularity, the whole numbers an Int takes, the keys of a Choice |
| `byDefault(v)` / `byDefault(fn)` | what Reset puts back (a function when it depends on where the holder is) |
| `ramp(Cut / Linear / Angle / Typed)` | how it goes from one value to another in a fade |
| `readOnly()`, `writeOnly()`, `transient()` | access; transient: never saved (a state of the show, not of a cue) |
| `animatable()` | a timeline or an animation can drive it |
| `colorComponent(i)`, `pointComponent(i)` | member of a color (r, g, b, a) or of an XY point |
| `bind(get, set)` | where its value lives |

`info()` copies the declaration out (`ParamInfo`) for whoever draws it or exposes it: an interface works on those copies,
because a holder's parameters may be made again when its source or its effects change.

## Writing it tells the others

`setValue()` writes within the limits and the granularity, then calls `ChangeFeed`. Nothing polls: what writes says so,
the one listener (if any) hears it and reads only what was named.

```cpp
struct Feedback : pxs::core::ChangeFeed::Observer {
    void parameterWritten(std::uint64_t holder, std::string_view path) override { /* send /<holder>/<path> over OSC */ }
    void layerChanged(std::uint64_t) override {}
    void addressChanged(std::string_view) override {}
};
pxs::core::ChangeFeed feed;       // the application's own (an instance, not a global): parameters are given it with `p.feed(&feed)`
feed.observe(&feedback);          // one listener at a time
```

The calls come from any thread, often with a lock held: an observer only notes the change and returns.

## Saved by address

`pxs::convert::parametersToJson(list)` / `parametersFromJson(list, json, &unknown)` (pxs/Convert.h) save every stored parameter under its address, and
read them back in their order; an address the holder no longer has goes to `unknown`.

## Rules

- One declaration per parameter, next to the state it controls; the interface, OSC and files read it, they do not repeat it.
- Addresses are hierarchical and lowercase; a switch is the child `enable` of what it switches.
- A new application declares its parameters this way before writing its own OSC or file code.

Not in `pxs` yet: the OSC namespace and OSCQuery server (NyxMap's `OscNamespace`, AtemRemoteOSC's `OscQueryTree`), which
are to be derived from `ParamInfo`.
