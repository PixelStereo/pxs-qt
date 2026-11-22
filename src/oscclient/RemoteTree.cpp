#include "pxs/RemoteTree.h"

#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace pxs::oscquery {

namespace {
// The arguments of a type tag string (the brackets of an array not counted)
int argumentCount(const QString &type)
{
    int n = 0;
    for (QChar c : type)
        if (c != '[' && c != ']') ++n;
    return n;
}

// VALUE, RANGE, UNIT, EXTENDED_TYPE and CLIPMODE hold one entry per argument; the proposal lets a device write a single value
// instead of an array when every argument shares it (a null stands for "none" in an array)
QJsonArray perArgument(const QJsonValue &v, int arguments)
{
    if (v.isArray()) return v.toArray();
    if (v.isUndefined() || v.isNull()) return {};
    QJsonArray out;
    for (int k = 0; k < std::max(1, arguments); ++k) out.append(v);
    return out;
}

QStringList strings(const QJsonValue &v, int arguments)
{
    QStringList out;
    for (const QJsonValue &e : perArgument(v, arguments)) out << (e.isString() ? e.toString() : QString());
    return out;
}
} // namespace

static bool parse(const QJsonObject &o, const QString &name, RemoteNode *out, int depth)
{
    if (depth > 64) return false;
    out->name = name;
    out->path = o.value("FULL_PATH").toString();
    out->type = o.value("TYPE").toString();
    out->description = o.value("DESCRIPTION").toString();
    out->resetPath = o.value("NYXMAP_RESET").toString();
    out->access = std::clamp(o.value("ACCESS").toInt(0), 0, 3);
    // A leaf with no value is not a parameter: one that is written is a message, one that is read is a signal
    if (!o.contains("CONTENTS") && !o.contains("VALUE") && !out->type.isEmpty())
        out->kind = out->access == 2 ? QStringLiteral("action") : out->access == 1 ? QStringLiteral("signal") : QString();
    const int arguments = argumentCount(out->type);
    out->range = perArgument(o.value("RANGE"), arguments);
    out->value = perArgument(o.value("VALUE"), arguments);
    out->extendedType = strings(o.value("EXTENDED_TYPE"), arguments);
    out->unit = strings(o.value("UNIT"), arguments);
    for (const QJsonValue &tag : o.value("TAGS").toArray()) out->tags << tag.toString();
    out->defaultValue = perArgument(o.value("DEFAULT_VALUE"), arguments);
    const QJsonObject contents = o.value("CONTENTS").toObject();
    for (auto it = contents.begin(); it != contents.end(); ++it) {
        if (!it.value().isObject()) continue;
        RemoteNode child;
        if (!parse(it.value().toObject(), it.key(), &child, depth + 1)) return false;
        if (child.path.isEmpty()) child.path = (out->path == QLatin1String("/") ? QString() : out->path) + QLatin1Char('/') + it.key();
        out->children.emplace(it.key(), std::move(child));
    }
    return true;
}

bool RemoteTree::load(const QJsonObject &root)
{
    RemoteNode node;
    if (!root.contains("FULL_PATH") && !root.contains("CONTENTS")) return false;
    if (!parse(root, QString(), &node, 0)) return false;
    if (node.path.isEmpty()) node.path = QStringLiteral("/");
    m_root = std::move(node);
    m_loaded = true;
    return true;
}

const RemoteNode *RemoteTree::find(const QString &path) const
{
    const RemoteNode *n = &m_root;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const auto it = n->children.find(part);
        if (it == n->children.end()) return nullptr;
        n = &it->second;
    }
    return n;
}

bool RemoteTree::setValue(const QString &path, const QJsonArray &value)
{
    RemoteNode *n = const_cast<RemoteNode *>(find(path));
    if (!n) return false;
    n->value = value;
    return true;
}

static void collect(const RemoteNode &n, QStringList *out)
{
    if (n.isLeaf()) out->append(n.path);
    for (const auto &c : n.children) collect(c.second, out);
}

static void sign(const RemoteNode &n, QByteArray *out)
{
    *out += n.path.toUtf8() + '|' + n.type.toUtf8() + '|' + QByteArray::number(n.access) + '|' + n.kind.toUtf8() + '|' +
            QJsonDocument(n.range).toJson(QJsonDocument::Compact) + '\n';
    for (const auto &c : n.children) sign(c.second, out);
}

QByteArray RemoteTree::signature() const
{
    QByteArray out;
    sign(m_root, &out);
    return out;
}

QStringList RemoteTree::leaves() const
{
    QStringList out;
    collect(m_root, &out);
    return out;
}

namespace {
// A JSON value as the value it describes: a number, a string, a switch; nothing for the rest
core::Value valueOf(const QJsonValue &v)
{
    if (v.isBool()) return core::Value(v.toBool());
    if (v.isDouble()) return core::Value(v.toDouble());
    if (v.isString()) return core::Value(v.toString().toStdString());
    return {};
}
} // namespace

bool RemoteNode::toParamInfo(core::tree::ParamInfo *out) const
{
    using ParamInfo = core::tree::ParamInfo;
    if (type.size() != 1) return false;
    if (kind == QLatin1String("signal") && type == QLatin1String("N")) return false; // an event with nothing to show
    ParamInfo info;
    info.signal = kind == QLatin1String("signal");
    info.path = path.toStdString();
    info.label = (name.isEmpty() ? name : name.at(0).toUpper() + name.mid(1)).toStdString(); // "level" -> "Level"
    info.access = info.signal ? int(ParamInfo::Read) : access ? access : ParamInfo::ReadWrite;
    info.stored = false;
    const QChar t = type[0];
    const QJsonObject r = range.isEmpty() ? QJsonObject() : range[0].toObject();
    if (t == QLatin1Char('f') || t == QLatin1Char('d') || t == QLatin1Char('i') || t == QLatin1Char('h')) {
        info.type = t == QLatin1Char('f') || t == QLatin1Char('d') ? ParamInfo::Type::Float : ParamInfo::Type::Int;
        const bool bounded = r.contains("MIN") && r.contains("MAX") && r.value("MAX").toDouble() > r.value("MIN").toDouble();
        info.min = bounded ? r.value("MIN").toDouble() : 0;
        info.max = bounded ? r.value("MAX").toDouble() : 1;
        info.lo = info.min;
        info.hi = info.max;
        info.unbounded = !bounded;
        info.step = info.type == ParamInfo::Type::Int ? 1 : (info.max - info.min) / 100.0;
        info.animatable = true;
    } else if (t == QLatin1Char('T') || t == QLatin1Char('F')) {
        info.type = ParamInfo::Type::Bool;
    } else if (t == QLatin1Char('s')) {
        const QJsonArray vals = r.value("VALS").toArray();
        if (vals.isEmpty()) {
            info.type = ParamInfo::Type::Text;
        } else {
            info.type = ParamInfo::Type::Choice;
            for (const QJsonValue &v : vals) info.choices.push_back(v.toString().toStdString());
        }
    } else if (t == QLatin1Char('N')) {
        info.type = ParamInfo::Type::Trigger;
        info.action = true;
    } else {
        return false;
    }
    if (!defaultValue.isEmpty()) info.defaultValue = valueOf(defaultValue[0]);
    else if (!value.isEmpty()) info.defaultValue = valueOf(value[0]);
    if (!unit.isEmpty()) info.unit = unit[0].toStdString();
    if (!extendedType.isEmpty()) info.extendedType = extendedType[0].toStdString();
    *out = info;
    return true;
}

} // namespace pxs::oscquery
