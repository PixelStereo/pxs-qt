#include "TimelineTree.h"
#include "Interactions.h"

#include <pxs/core/device/device.h>
#include <QJsonDocument>
#include <cmath>
#include <limits>
#include <map>

namespace pxs::timeline {
namespace {
using core::Value;
using core::tree::Parameter;

using Read = std::function<const AnimTrack *()>;
using Write = std::function<void(AnimTrack)>;

void declareFields(core::tree::Tree &tree, const std::string &path, const AnimTrack &layout,
                   const Read &read, const Write &write)
{
    const auto parameter = [&](const std::string &relative, const char *label, Parameter::Type type,
                               std::function<Value(const AnimTrack &)> get,
                               std::function<void(AnimTrack &, const Value &)> set = {},
                               std::function<void(Parameter &)> configure = {}) {
        auto field = std::make_unique<Parameter>(relative.substr(relative.rfind('/') + 1), label, type);
        if (!set) field->readOnly();
        if (type == Parameter::Type::Float) {
            const double limit = std::numeric_limits<double>::max();
            field->range(-1e6, 1e6).limits(-limit, limit);
            if (set) field->byDefault(get(layout));
        }
        if (configure) configure(*field);
        field->bind([read, get] {
            const auto *track = read();
            return track ? get(*track) : Value();
        }, [read, write, set](const Value &value) {
            const auto *track = read();
            if (!track || !set) return;
            auto edited = *track;
            set(edited, value);
            write(std::move(edited));
        });
        tree.add(path + "/" + relative, std::move(field));
    };
    const auto numeric = [&](const std::string &relative, const char *name, double AnimTrack::*member,
                             std::function<void(Parameter &)> configure = {}) {
        parameter(relative, name, Parameter::Type::Float, [member](const AnimTrack &track) { return Value(track.*member); },
                         [member](AnimTrack &track, const Value &value) { track.*member = value.number().value_or(track.*member); }, configure);
    };
    parameter("enabled", "Enabled", Parameter::Type::Bool, [](const auto &t) { return Value(t.enabled); },
              [](auto &t, const Value &v) { t.enabled = v.truthy(); });
    parameter("target/address", "Target address", Parameter::Type::Text, [](const auto &t) { return Value(t.param.toStdString()); },
              [](auto &t, const Value &v) { t.param = QString::fromStdString(v.text()); });
    parameter("target/device-id", "Target device ID", Parameter::Type::Text,
              [](const auto &t) { return Value(std::to_string(t.layer)); },
              [](auto &t, const Value &v) {
                  bool ok = false;
                  const auto id = QString::fromStdString(v.text()).toULongLong(&ok);
                  if (ok) t.layer = id;
              });
    parameter("curve/unit-scale", "0–1 curve scale", Parameter::Type::Bool, [](const auto &t) { return Value(t.unitCurve); },
              [](auto &t, const Value &v) { t.unitCurve = v.truthy(); });
    parameter("curve/keys", "Curve keys (JSON)", Parameter::Type::Text, [](const auto &t) {
        QJsonObject data;
        animTrackToJson(t, data);
        return Value(QJsonDocument(data.value("keys").toArray()).toJson(QJsonDocument::Compact).toStdString());
    }, [](auto &t, const Value &v) {
        const auto keys = QJsonDocument::fromJson(QByteArray::fromStdString(v.text()));
        if (!keys.isArray()) return;
        QJsonObject data;
        animTrackToJson(t, data);
        data["keys"] = keys.array();
        if (t.event) {
            auto values = data.value("values").toArray();
            while (values.size() < keys.array().size()) values.append(0);
            data["values"] = values;
        }
        const auto axes = t.axes;
        animTrackFromJson(t, data, 36000);
        if (t.isPoint()) t.axes = axes;
    });
    std::set<std::string> points;
    for (size_t index = 0; index < layout.keys.size(); ++index) {
        const auto stem = "curve/points/" + claimSegment("point", points);
        const auto keyField = [&](const std::string &name, const char *label, Parameter::Type type,
                                  std::function<Value(const AnimKey &)> get,
                                  std::function<void(AnimKey &, const Value &)> set,
                                  std::function<void(Parameter &)> configure = {}) {
            parameter(stem + "/" + name, label, type, [index, get](const auto &t) {
                return index < t.keys.size() ? get(t.keys[index]) : Value();
            }, [index, set](auto &t, const Value &v) {
                if (index < t.keys.size()) set(t.keys[index], v);
            }, configure);
        };
        const auto keyNumber = [&](const std::string &name, const char *label, double AnimKey::*member,
                                   std::function<void(Parameter &)> configure = {}) {
            keyField(name, label, Parameter::Type::Float, [member](const AnimKey &key) { return Value(key.*member); },
                     [member](AnimKey &key, const Value &v) { key.*member = v.number().value_or(key.*member); }, configure);
        };
        keyNumber("time", "Time", &AnimKey::t, [](Parameter &p) { p.range(0, 36000).unit("time.second"); });
        if (layout.color) {
            const std::array<std::string, 3> channels{"red", "green", "blue"};
            for (size_t c = 0; c < channels.size(); ++c)
                keyField(channels[c], channels[c].c_str(), Parameter::Type::Float,
                         [c](const AnimKey &key) { return Value(key.rgb[c]); },
                         [c](AnimKey &key, const Value &v) { key.rgb[c] = v.number().value_or(key.rgb[c]); },
                         [](Parameter &p) { p.range(0, 1); });
        } else {
            keyNumber("value", "Value", &AnimKey::v);
        }
        if (layout.event) continue;
        const std::map<std::string, int> modes{
            {"linear", 0}, {"ease-in", 1}, {"ease-out", 2}, {"ease-in-out", 3},
            {"cubic-in", 4}, {"cubic-out", 5}, {"bezier", kAnimBezier}, {"hold", kAnimHold}};
        keyField("easing", "Easing", Parameter::Type::Choice, [modes](const AnimKey &key) {
            for (const auto &[name, value] : modes) if (value == key.curve) return Value(name);
            return Value("linear");
        }, [modes](AnimKey &key, const Value &v) {
            if (const auto it = modes.find(v.text()); it != modes.end()) key.curve = it->second;
        }, [modes, color = layout.color](Parameter &p) {
            std::vector<std::string> choices;
            for (const auto &[name, value] : modes) if (!color || value != kAnimBezier) choices.push_back(name);
            p.choices(std::move(choices));
        });
        if (index == 0)
            keyField("current", "Use current value", Parameter::Type::Bool,
                     [](const AnimKey &key) { return Value(key.isCurrentValue); },
                     [](AnimKey &key, const Value &v) { key.isCurrentValue = v.truthy(); });
        if (!layout.color) {
            keyNumber("bezier/in-time", "In time", &AnimKey::inDt);
            keyNumber("bezier/in-value", "In value", &AnimKey::inDv);
            keyNumber("bezier/out-time", "Out time", &AnimKey::outDt);
            keyNumber("bezier/out-value", "Out value", &AnimKey::outDv);
        }
    }
    parameter("oscillator/enabled", "Oscillator", Parameter::Type::Bool, [](const auto &t) { return Value(t.oscillator); },
              [](auto &t, const Value &v) { t.oscillator = v.truthy(); });
    const auto waves = animWaveKeys();
    parameter("oscillator/wave", "Wave", Parameter::Type::Text, [waves](const auto &t) {
        return Value(waves.value(int(t.wave)).toStdString());
    }, [waves](auto &t, const Value &v) {
        const auto index = waves.indexOf(QString::fromStdString(v.text()));
        if (index >= 0) t.wave = AnimWave(index);
    });
    numeric("oscillator/period", "Period", &AnimTrack::period, [](Parameter &p) { p.range(0.01, 36000).unit("time.second"); });
    numeric("oscillator/phase", "Phase", &AnimTrack::phase, [](Parameter &p) { p.range(0, 1); });
    numeric("oscillator/center", "Center", &AnimTrack::center);
    numeric("oscillator/amplitude", "Amplitude", &AnimTrack::amplitude);
    parameter("oscillator/seed", "Random seed", Parameter::Type::Text, [](const auto &t) { return Value(std::to_string(t.seed)); },
              [](auto &t, const Value &v) {
                  bool ok = false;
                  const auto seed = QString::fromStdString(v.text()).toUInt(&ok);
                  if (ok) t.seed = seed;
              });

    parameter("filters/definition", "Filter chain (JSON)", Parameter::Type::Text, [](const auto &t) {
        return Value(QJsonDocument(t.dataFilters).toJson(QJsonDocument::Compact).toStdString());
    }, [](auto &t, const Value &v) {
        const auto filters = QJsonDocument::fromJson(QByteArray::fromStdString(v.text()));
        if (filters.isArray()) t.dataFilters = Interaction::filtersToJson(Interaction::filtersFromJson({{"filters", filters.array()}}));
    });
    std::set<std::string> filters{"definition"};
    for (int i = 0; i < layout.dataFilters.size(); ++i) {
        const auto configured = InteractionStage::fromJson(layout.dataFilters[i].toObject());
        const auto stem = "filters/" + claimSegment(configured.displayName().toStdString(), filters);
        tree.add(path + "/" + stem).label = configured.displayName().toStdString();
        const auto filter = [i](const AnimTrack &t) {
            return InteractionStage::fromJson(i < t.dataFilters.size() ? t.dataFilters.at(i).toObject() : QJsonObject{});
        };
        const auto change = [i, filter](AnimTrack &t, const auto &edit) {
            if (i >= t.dataFilters.size()) return;
            auto stage = filter(t);
            edit(stage);
            t.dataFilters[i] = stage.toJson();
        };
        parameter(stem + "/type", "Filter type", Parameter::Type::Text,
                  [filter](const auto &t) { return Value(interactionFilterKey(filter(t).kind).toStdString()); });
        parameter(stem + "/enabled", "Enabled", Parameter::Type::Bool,
                  [filter](const auto &t) { return Value(filter(t).enabled); },
                  [change](auto &t, const auto &v) { change(t, [&v](auto &f) { f.enabled = v.truthy(); }); });
        for (int field = 0; field < kFilterSettingCount; ++field) {
            const auto setting = interactionFilterSetting(configured.kind, field);
            if (setting.name.isEmpty()) continue;
            const auto name = setting.name.toStdString();
            parameter(stem + "/" + core::device::namespaceSegment(name), name.c_str(), Parameter::Type::Float,
                      [filter, field](const auto &t) { return Value(filter(t).setting(field)); },
                      [change, field](auto &t, const auto &v) {
                          change(t, [field, &v](auto &f) { f.setting(field) = v.number().value_or(f.setting(field)); });
                      });
        }
    }
    for (size_t axis = 0; axis < layout.axes.size(); ++axis) {
        declareFields(tree, path + (axis == 0 ? "/x" : "/y"), layout.axes[axis],
            [read, axis]() -> const AnimTrack * {
                const auto *track = read();
                return track && axis < track->axes.size() ? &track->axes[axis] : nullptr;
            }, [read, write, axis](AnimTrack edited) {
                const auto *track = read();
                if (!track || axis >= track->axes.size()) return;
                auto parent = *track;
                parent.axes[axis] = std::move(edited);
                write(std::move(parent));
            });
    }
}

bool sameFields(const AnimTrack &a, const AnimTrack &b)
{
    if (a.layer != b.layer || a.param != b.param || a.axes.size() != b.axes.size() ||
        a.keys.size() != b.keys.size() || a.color != b.color || a.event != b.event || a.dataFilters.size() != b.dataFilters.size())
        return false;
    for (int i = 0; i < a.dataFilters.size(); ++i) {
        const auto left = InteractionStage::fromJson(a.dataFilters[i].toObject());
        const auto right = InteractionStage::fromJson(b.dataFilters[i].toObject());
        if (left.kind != right.kind || left.displayName() != right.displayName()) return false;
    }
    for (size_t i = 0; i < a.axes.size(); ++i)
        if (!sameFields(a.axes[i], b.axes[i])) return false;
    return true;
}
}

std::string claimSegment(std::string_view label, std::set<std::string> &used)
{
    const auto base = core::device::namespaceSegment(label);
    std::string segment = base;
    std::string suffix = "a";
    while (!used.insert(segment).second) {
        segment = base + "-" + suffix;
        auto pos = suffix.find_last_not_of('z');
        if (pos == std::string::npos) suffix.assign(suffix.size() + 1, 'a');
        else { ++suffix[pos]; suffix.replace(pos + 1, std::string::npos, suffix.size() - pos - 1, 'a'); }
    }
    return segment;
}

std::vector<std::string> declareTrackTree(core::tree::Tree &tree, const std::string &path,
    const Animation &layout, const std::function<const Animation *()> &read,
    const std::function<void(size_t, AnimTrack)> &write,
    const std::function<std::string(const AnimTrack &)> &label,
    const std::function<double(size_t)> &output)
{
    tree.add(path).label = "Tracks";
    std::set<std::string> used;
    std::vector<std::string> paths;
    for (size_t i = 0; i < layout.tracks.size(); ++i) {
        const auto title = label(layout.tracks[i]);
        auto trackPath = path + "/" + claimSegment(title, used);
        tree.add(trackPath).label = title;
        declareFields(tree, trackPath, layout.tracks[i], [read, i]() -> const AnimTrack * {
            const auto *animation = read();
            return animation && i < animation->tracks.size() ? &animation->tracks[i] : nullptr;
        }, [write, i](AnimTrack track) { write(i, std::move(track)); });
        auto meter = std::make_unique<Parameter>("output", "Output after filters", Parameter::Type::Float);
        meter->readOnly().range(-1e6, 1e6).transient().bind([output, i] {
            const double value = output(i);
            return std::isfinite(value) ? Value(value) : Value();
        }, {});
        paths.push_back(trackPath + "/output");
        tree.add(paths.back(), std::move(meter));
    }
    return paths;
}

bool sameTrackTree(const Animation &left, const Animation &right)
{
    if (left.tracks.size() != right.tracks.size()) return false;
    for (size_t i = 0; i < left.tracks.size(); ++i)
        if (!sameFields(left.tracks[i], right.tracks[i])) return false;
    return true;
}
}
