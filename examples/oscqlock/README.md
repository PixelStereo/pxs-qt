# OSCQlock

A device made to be tested against: a generator that serves an OSCQuery tree with **every kind of parameter, action and signal**, so that a controller, an
explorer or a mapper (NyxMap, OSCKadran, Kadran, MpOsc…) can be tried on all of them.

```sh
cmake -S . -B build -DPXS_BUILD_EXAMPLES=ON && cmake --build build --target oscqlock
build/examples/oscqlock/OSCQlock                      # a window, OSCQuery on :5678, OSC on udp 9000, announced as "OSCQlock"
build/examples/oscqlock/OSCQlock --no-window --local  # only the device, reachable from this computer
curl http://127.0.0.1:5678/
```

| Under | What |
|---|---|
| `/generator/play` | a switch (`T`), `type` a choice (sine, saw, triangle, square, random) |
| `/generator/frequency`, `amplitude`, `offset`, `phase`, `hue` | floats with a range, a granularity, a unit (`frequency.hertz`, `angle.degree`), animatable |
| `/generator/loops`, `steps` | whole numbers: any within limits, and one among `1 2 4 8 16`; `loop_count` is a state that is only read |
| `/generator/label` | text |
| `/generator/position` (`ff`), `color` (`fff`) | a value that holds its components (`x y`, `r g b`): a node with a value and children |
| `/generator/origin` (`fff`), `big` (`h`, 64 bits), `precise` (`d`), `data` (`b`, a blob) | the rest of the argument types |
| `/generator/rewind`, `start`, `stop`, `randomize` | actions with no value |
| `/generator/seek` (`f`), `jump` (`i`), `say` (`s`), `flash` (`T`) | actions with one value |
| `/generator/go_to` (`if`), `set_color` (`fff`) | actions with several |
| `/signal` (`f`) | the waveform, 50 times a second; it holds the other signals |
| `/signal/loop` (`i`), `cycle` (`N`, an impulse), `done` (`N`), `above` (`T`), `phase` (`f`), `seconds` (`d`), `xy` (`ff`), `rgb` (`fff`), `text` (`s`), `stamp` (`h`) | signals of each type: a count of loops, events with no value, a threshold, text, 64 bits |

Every numeric parameter says its range and default (a reset is writing `DEFAULT_VALUE`); the tree is checked at start (`Tree::check()`).
