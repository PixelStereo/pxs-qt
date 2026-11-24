// OSCQlock: a device that exists to be tested against. It generates a signal (a sine, a saw, a triangle, a square or noise), and says
// everything about it in every type OSCQuery has, so that a controller, an explorer or a mapper can be tried on all of them:
//
//   /generator/…   the parameters: a switch (play), a choice (type), floats with their ranges and units, whole numbers (one among a few),
//                  text, a point and a colour that hold their own components, a 64-bit number, a double, a blob, and a state that is read only
//   /generator/…   the actions: with no value (rewind, start, stop, randomize), with one (seek, jump, say, flash), with several (go_to)
//   /signal…       the signals: what the device emits and nobody writes: the waveform (a float, streamed), the loop count (a whole number),
//                  a cycle (an impulse, no value), a threshold (a switch), seconds (a double), a point, a colour, a text, a stamp (64 bits)
//
// Run it, then explore it:  curl http://127.0.0.1:5678/   (or find "OSCQlock" on the network from any OSCQuery client).
#include <pxs/OscService.h>
#include <pxs/core/tree/parameter.h>
#include <pxs/core/tree/tree.h>

#include <QApplication>
#include <QCheckBox>
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <random>

using namespace pxs::core;
using namespace pxs::core::tree;

namespace {

constexpr double kTau = 6.283185307179586;

// What the generator is: every parameter reads and writes one of these
struct State {
    bool play = false;
    std::string type = "sine";
    double frequency = 1.0, amplitude = 1.0, offset = 0.0, phase = 0.0, hue = 0.0;
    int loops = 0, steps = 4, loopCount = 0;
    std::string label = "OSCQlock";
    float x = 0.5f, y = 0.5f;
    float r = 1.0f, g = 0.5f, b = 0.0f;
    float ox = 0, oy = 0, oz = 0;
    std::int64_t big = 1234567890123LL;
    double precise = 3.141592653589793;
    Blob data = {0xde, 0xad, 0xbe, 0xef};
    double seconds = 0, value = 0;
    bool above = false;
};

class Qlock : public QObject {
public:
    Qlock(Tree *tree) : m_tree(tree)
    {
        buildParameters();
        buildActions();
        buildSignals();
        m_clock.start();
        connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
        m_timer.start(20); // 50 Hz
    }

    State s;
    double lastValue() const { return s.value; }

private:
    // ---- the parameters

    // A parameter is declared completely before it is added: the node takes its type, range, default and access when it is made
    template <class Get, class Set, class Configure>
    void parameter(const char *path, const char *name, Parameter::Type type, Get get, Set set, Configure configure)
    {
        const std::string full = path;
        auto p = std::make_unique<Parameter>(full.substr(full.rfind('/') + 1), name, type);
        p->bind(get, set);
        configure(*p);
        m_tree->add(path, std::move(p));
    }

    void buildParameters()
    {
        // A switch, and the state of the clock
        parameter("/generator/play", "Play", Parameter::Type::Bool, [this] { return Value(s.play); },
                  [this](const Value &v) { s.play = v.truthy(); }, [](Parameter &p) { p.byDefault(Value(false)).transient(); });
        // A choice among keys
        parameter("/generator/type", "Type", Parameter::Type::Choice, [this] { return Value(s.type); },
                  [this](const Value &v) { s.type = v.text(); }, [](Parameter &p) { p.choices({"sine", "saw", "triangle", "square", "random"}).byDefault(Value("sine")); });
        // Floats: a range, a granularity, a unit
        parameter("/generator/frequency", "Frequency", Parameter::Type::Float, [this] { return Value(s.frequency); },
                  [this](const Value &v) { s.frequency = v.number().value_or(1.0); }, [](Parameter &p) { p.range(0.05, 20).step(0.05).unit("frequency.hertz").byDefault(Value(1.0)).animatable(); });
        parameter("/generator/amplitude", "Amplitude", Parameter::Type::Float, [this] { return Value(s.amplitude); },
                  [this](const Value &v) { s.amplitude = v.number().value_or(1.0); }, [](Parameter &p) { p.range(0, 1).byDefault(Value(1.0)).animatable(); });
        parameter("/generator/offset", "Offset", Parameter::Type::Float, [this] { return Value(s.offset); },
                  [this](const Value &v) { s.offset = v.number().value_or(0.0); }, [](Parameter &p) { p.range(-1, 1).byDefault(Value(0.0)).animatable(); });
        parameter("/generator/phase", "Phase", Parameter::Type::Float, [this] { return Value(s.phase); },
                  [this](const Value &v) { s.phase = v.number().value_or(0.0); }, [](Parameter &p) { p.range(0, 1).byDefault(Value(0.0)).animatable(); });
        parameter("/generator/hue", "Hue", Parameter::Type::Float, [this] { return Value(s.hue); },
                  [this](const Value &v) { s.hue = v.number().value_or(0.0); }, [](Parameter &p) { p.range(0, 360).ramp(Parameter::Ramp::Angle).unit("angle.degree").byDefault(Value(0.0)).animatable(); });
        // Whole numbers: any within limits, and one among a few
        parameter("/generator/loops", "Loops (0: endless)", Parameter::Type::Int, [this] { return Value(s.loops); },
                  [this](const Value &v) { s.loops = int(v.number().value_or(0.0)); }, [](Parameter &p) { p.range(0, 100).step(1).byDefault(Value(0)); });
        parameter("/generator/steps", "Steps", Parameter::Type::Int, [this] { return Value(s.steps); },
                  [this](const Value &v) { s.steps = int(v.number().value_or(4.0)); }, [](Parameter &p) { p.values({1, 2, 4, 8, 16}).byDefault(Value(4)); });
        // Text
        parameter("/generator/label", "Label", Parameter::Type::Text, [this] { return Value(s.label); },
                  [this](const Value &v) { s.label = v.text(); }, [](Parameter &p) { p.byDefault(Value("OSCQlock")); });
        // A state that is only read
        parameter("/generator/loop_count", "Loops done", Parameter::Type::Int, [this] { return Value(s.loopCount); }, nullptr, [](Parameter &p) { p.readOnly().range(0, 100000); });

        // A point and a colour that hold their own components: a value that holds nodes
        value("/generator/position", "ff", "Position", {range(0, 1), range(0, 1)}, [this] { return ValueList{Value(s.x), Value(s.y)}; },
              [this](const ValueList &v) { return take(v, {&s.x, &s.y}); }, {Value(0.5f), Value(0.5f)});
        component("/generator/position/x", "Position X", 0, 1, [this] { return Value(s.x); }, [this](const Value &v) { s.x = float(v.number().value_or(0.5)); }, 0.5);
        component("/generator/position/y", "Position Y", 0, 1, [this] { return Value(s.y); }, [this](const Value &v) { s.y = float(v.number().value_or(0.5)); }, 0.5);
        value("/generator/color", "fff", "Color", {range(0, 1), range(0, 1), range(0, 1)},
              [this] { return ValueList{Value(s.r), Value(s.g), Value(s.b)}; }, [this](const ValueList &v) { return take(v, {&s.r, &s.g, &s.b}); },
              {Value(1.0f), Value(0.5f), Value(0.0f)})
            .extendedType = {"color.rgb.r", "color.rgb.g", "color.rgb.b"};
        component("/generator/color/r", "Color R", 0, 1, [this] { return Value(s.r); }, [this](const Value &v) { s.r = float(v.number().value_or(1)); }, 1.0);
        component("/generator/color/g", "Color G", 0, 1, [this] { return Value(s.g); }, [this](const Value &v) { s.g = float(v.number().value_or(0.5)); }, 0.5);
        component("/generator/color/b", "Color B", 0, 1, [this] { return Value(s.b); }, [this](const Value &v) { s.b = float(v.number().value_or(0)); }, 0.0);
        // Three numbers with no component of their own
        value("/generator/origin", "fff", "Origin", {range(-10, 10), range(-10, 10), range(-10, 10)},
              [this] { return ValueList{Value(s.ox), Value(s.oy), Value(s.oz)}; }, [this](const ValueList &v) { return take(v, {&s.ox, &s.oy, &s.oz}); },
              {Value(0.0f), Value(0.0f), Value(0.0f)});
        // A 64-bit number, a double, a blob
        Node &big = value("/generator/big", "h", "64-bit number", {range(0, 9e15)}, [this] { return ValueList{Value(s.big)}; },
                          [this](const ValueList &v) { s.big = std::int64_t(v.at(0).number().value_or(0)); return true; }, {Value(std::int64_t(0))});
        (void)big;
        value("/generator/precise", "d", "Double", {range(-10, 10)}, [this] { return ValueList{Value(s.precise)}; },
              [this](const ValueList &v) { s.precise = v.at(0).number().value_or(0); return true; }, {Value(3.141592653589793)});
        Node &blob = m_tree->add("/generator/data");
        blob.type = "b";
        blob.access = 3;
        blob.description = "A blob (4 bytes)";
        blob.get = [this] { return ValueList{Value(s.data)}; };
        blob.set = [this](const ValueList &v) {
            if (v.empty() || !std::holds_alternative<Blob>(v[0].variant())) return false;
            s.data = std::get<Blob>(v[0].variant());
            return true;
        };
    }

    static ArgRange range(double lo, double hi)
    {
        ArgRange r;
        r.min = Value(lo);
        r.max = Value(hi);
        return r;
    }

    // A node of several arguments, with its range and the defaults a reset writes
    Node &value(const char *path, const char *type, const char *description, std::vector<ArgRange> ranges, std::function<ValueList()> get,
                std::function<bool(const ValueList &)> set, ValueList defaults)
    {
        Node &n = m_tree->add(path);
        n.type = type;
        n.access = 3;
        n.description = description;
        n.range = std::move(ranges);
        n.clip = "both";
        n.get = std::move(get);
        n.set = std::move(set);
        n.defaultValue = [defaults] { return defaults; };
        return n;
    }

    void component(const char *path, const char *name, double lo, double hi, std::function<Value()> get, std::function<void(const Value &)> set, double def)
    {
        const std::string p = path;
        auto param = std::make_unique<Parameter>(p.substr(p.rfind('/') + 1), name, Parameter::Type::Float);
        param->range(lo, hi).byDefault(Value(def)).bind(std::move(get), std::move(set)).animatable();
        m_tree->add(path, std::move(param));
    }

    static bool take(const ValueList &v, std::initializer_list<float *> targets)
    {
        std::size_t i = 0;
        if (v.size() < targets.size()) return false;
        for (float *t : targets) *t = float(v[i++].number().value_or(0.0));
        return true;
    }

    // ---- the actions

    void buildActions()
    {
        // With no value
        m_tree->addAction("/generator/rewind", "N", [this](const ValueList &) { s.phase = 0; s.loopCount = 0; return true; }, "Back to the start");
        m_tree->addAction("/generator/start", "N", [this](const ValueList &) { s.play = true; m_tree->changed("/generator/play"); return true; }, "Play");
        m_tree->addAction("/generator/stop", "N", [this](const ValueList &) { s.play = false; m_tree->changed("/generator/play"); return true; }, "Stop");
        m_tree->addAction("/generator/randomize", "N", [this](const ValueList &) { randomize(); return true; }, "Random frequency, amplitude and phase");
        // With one value, of each kind
        m_tree->addAction("/generator/seek", "f", [this](const ValueList &v) { s.phase = std::clamp(v.at(0).number().value_or(0.0), 0.0, 1.0); return true; }, "Go to a phase (0..1)");
        m_tree->addAction("/generator/jump", "i", [this](const ValueList &v) { s.loopCount = std::max(0, int(v.at(0).number().value_or(0.0))); return true; }, "Go to a loop");
        m_tree->addAction("/generator/say", "s", [this](const ValueList &v) { s.label = v.at(0).text(); m_tree->changed("/generator/label"); return true; }, "Set the label");
        m_tree->addAction("/generator/flash", "T", [this](const ValueList &v) { m_flash = v.at(0).truthy(); return true; }, "Hold the signal at its maximum while true");
        // With several
        m_tree->addAction("/generator/go_to", "if", [this](const ValueList &v) {
            s.loopCount = std::max(0, int(v.at(0).number().value_or(0.0)));
            s.phase = std::clamp(v.at(1).number().value_or(0.0), 0.0, 1.0);
            return true;
        }, "Go to a loop and a phase");
        m_tree->addAction("/generator/set_color", "fff", [this](const ValueList &v) { return take(v, {&s.r, &s.g, &s.b}); }, "Set the color");
    }

    void randomize()
    {
        std::uniform_real_distribution<double> d(0, 1);
        s.frequency = 0.1 + d(m_rng) * 4;
        s.amplitude = 0.3 + d(m_rng) * 0.7;
        s.phase = d(m_rng);
    }

    // ---- the signals

    void buildSignals()
    {
        m_tree->addSignal("/signal", "f", "The waveform, 50 times a second");
        m_tree->addSignal("/signal/loop", "i", "Loops done, at each loop");
        m_tree->addSignal("/signal/cycle", "N", "A loop starts (an impulse: no value)");
        m_tree->addSignal("/signal/done", "N", "The loops asked for are done");
        m_tree->addSignal("/signal/above", "T", "The waveform crossed 0.5");
        m_tree->addSignal("/signal/phase", "f", "Where it is in the loop (0..1)");
        m_tree->addSignal("/signal/seconds", "d", "Seconds since it started");
        m_tree->addSignal("/signal/xy", "ff", "Waveform and its opposite");
        m_tree->addSignal("/signal/rgb", "fff", "A color that follows the waveform");
        m_tree->addSignal("/signal/text", "s", "A text with the loop count");
        m_tree->addSignal("/signal/stamp", "h", "Milliseconds since 1970 (64 bits)");
    }

    double wave(double phase)
    {
        const std::string &t = s.type;
        double v;
        if (t == "saw") v = phase;
        else if (t == "triangle") v = phase < 0.5 ? phase * 2 : 2 - phase * 2;
        else if (t == "square") v = phase < 0.5 ? 1 : 0;
        else if (t == "random") v = std::uniform_real_distribution<double>(0, 1)(m_rng);
        else v = 0.5 + 0.5 * std::sin(kTau * phase);
        if (s.steps > 1 && t != "random") v = std::round(v * (s.steps * 8)) / (s.steps * 8); // the steps: a stair, finer when it is more
        return std::clamp(s.offset + s.amplitude * v, -1.0, 2.0);
    }

    void tick()
    {
        const double dt = m_clock.restart() / 1000.0;
        if (s.play) {
            s.seconds += dt;
            s.phase += s.frequency * dt;
            if (s.phase >= 1.0) {
                s.phase -= std::floor(s.phase);
                ++s.loopCount;
                m_tree->fire("/signal/loop", {Value(s.loopCount)});
                m_tree->fire("/signal/cycle");
                m_tree->fire("/signal/text", {Value("loop " + std::to_string(s.loopCount))});
                m_tree->changed("/generator/loop_count");
                if (s.loops > 0 && s.loopCount >= s.loops) {
                    s.play = false;
                    m_tree->changed("/generator/play");
                    m_tree->fire("/signal/done");
                }
            }
        }
        s.value = m_flash ? 1.0 : wave(s.phase);
        m_tree->fire("/signal", {Value(s.value)});
        m_tree->fire("/signal/phase", {Value(s.phase)});
        m_tree->fire("/signal/seconds", {Value(s.seconds)});
        m_tree->fire("/signal/xy", {Value(float(s.value)), Value(float(1 - s.value))});
        m_tree->fire("/signal/rgb", {Value(float(s.value * s.r)), Value(float(s.value * s.g)), Value(float(s.value * s.b))});
        m_tree->fire("/signal/stamp", {Value(std::int64_t(QDateTime::currentMSecsSinceEpoch()))});
        const bool above = s.value > 0.5;
        if (above != s.above) {
            s.above = above;
            m_tree->fire("/signal/above", {Value(above)});
        }
    }

    Tree *m_tree;
    QTimer m_timer;
    QElapsedTimer m_clock;
    std::mt19937 m_rng{std::random_device{}()};
    bool m_flash = false;
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("OSCQlock");

    QCommandLineParser cli;
    cli.setApplicationDescription("A device to test OSC and OSCQuery controllers on: a generator with every kind of parameter, action and signal.");
    cli.addHelpOption();
    QCommandLineOption name({"n", "name"}, "The name it announces on the network.", "name", "OSCQlock");
    QCommandLineOption oscPort({"o", "osc-port"}, "UDP port for OSC (0: any free one).", "port", "9000");
    QCommandLineOption httpPort({"p", "http-port"}, "TCP port for OSCQuery HTTP and WebSocket (0: any free one).", "port", "5678");
    QCommandLineOption local("local", "Reachable from this computer only, and not announced.");
    QCommandLineOption headless("no-window", "No window: only the device.");
    cli.addOptions({name, oscPort, httpPort, local, headless});
    cli.process(app);

    Tree tree;
    Qlock qlock(&tree);
    for (const std::string &problem : tree.check()) qWarning("OSCQlock: the tree lacks something: %s", problem.c_str()); // what every pxs tree must have
    pxs::OscService service(&tree, cli.value(name));
    service.setLocalOnly(cli.isSet(local));
    if (!service.start(cli.value(oscPort).toUShort(), cli.value(httpPort).toUShort(), !cli.isSet(local))) {
        qCritical("OSCQlock: could not open its ports");
        return 1;
    }
    qInfo("OSCQlock: OSCQuery on http://127.0.0.1:%d/, OSC on udp %d", service.httpPort(), service.oscPort());

    QWidget window;
    QPlainTextEdit *log = new QPlainTextEdit;
    if (!cli.isSet(headless)) {
        window.setWindowTitle("OSCQlock");
        auto *layout = new QVBoxLayout(&window);
        layout->addWidget(new QLabel(QString("<b>%1</b> — OSCQuery http://127.0.0.1:%2/ — OSC udp %3")
                                         .arg(cli.value(name)).arg(service.httpPort()).arg(service.oscPort())));
        auto *play = new QCheckBox("play");
        auto *bar = new QProgressBar;
        bar->setRange(0, 1000);
        auto *status = new QLabel;
        layout->addWidget(play);
        layout->addWidget(bar);
        layout->addWidget(status);
        log->setReadOnly(true);
        log->setMaximumBlockCount(400);
        layout->addWidget(log);
        QObject::connect(play, &QCheckBox::toggled, &window, [&](bool on) {
            qlock.s.play = on;
            tree.changed("/generator/play");
        });
        auto *refresh = new QTimer(&window);
        QObject::connect(refresh, &QTimer::timeout, &window, [&, play, bar, status] {
            const QSignalBlocker block(play);
            play->setChecked(qlock.s.play);
            bar->setValue(int(std::clamp(qlock.s.value, 0.0, 1.0) * 1000));
            status->setText(QString("%1  %2 Hz  loop %3  phase %4")
                                .arg(QString::fromStdString(qlock.s.type)).arg(qlock.s.frequency, 0, 'f', 2).arg(qlock.s.loopCount).arg(qlock.s.phase, 0, 'f', 3));
        });
        refresh->start(50);
        window.resize(560, 420);
        window.show();
    }
    QObject::connect(&service, &pxs::OscService::messageReceived, log, [log](const ::osc::Message &m, int accepted) {
        QStringList args;
        for (const QVariant &v : m.args) args << v.toString();
        log->appendPlainText(QString("%1 %2 %3%4").arg(m.address, m.types, args.join(' '), accepted ? "" : "  (refused)"));
    });

    return app.exec();
}
