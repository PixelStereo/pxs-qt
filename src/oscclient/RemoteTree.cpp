#include "pxs/RemoteTree.h"

#include <pxs/Convert.h>
#include <pxs/core/tree/remote.h>

#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace pxs::oscquery {

// What a device says of itself is read by pxs-core (core::tree::RemoteTree): what a node is, what a parameter's range and default are, is decided
// there, once, for every application. This keeps the Qt shape of it (QString, QJsonArray) for the applications that browse it.
namespace {

QJsonObject rangeObject(const core::tree::ArgRange &r)
{
    QJsonObject o;
    if (r.min) o.insert("MIN", r.min->number().value_or(0.0));
    if (r.max) o.insert("MAX", r.max->number().value_or(0.0));
    if (!r.vals.empty()) {
        QJsonArray vals;
        for (const core::Value &v : r.vals) vals.append(convert::fromUtf8(v.text()));
        o.insert("VALS", vals);
    }
    return o;
}

QStringList strings(const std::vector<std::string> &in)
{
    QStringList out;
    for (const std::string &s : in) out << convert::fromUtf8(s);
    return out;
}

void fill(const core::tree::RemoteNode &c, RemoteNode *out)
{
    out->name = convert::fromUtf8(c.name);
    out->path = convert::fromUtf8(c.path);
    out->type = convert::fromUtf8(c.type);
    out->description = convert::fromUtf8(c.description);
    out->kind = convert::fromUtf8(c.kind);
    out->access = c.access;
    for (const core::tree::ArgRange &r : c.range) out->range.append(rangeObject(r));
    for (const core::Value &v : c.value) out->value.append(convert::toJson(v));
    for (const core::Value &v : c.defaultValue) out->defaultValue.append(convert::toJson(v));
    out->extendedType = strings(c.extendedType);
    out->unit = strings(c.unit);
    out->tags = strings(c.tags);
    for (const auto &child : c.children) fill(child.second, &out->children[convert::fromUtf8(child.first)]);
}

core::tree::ArgRange argRange(const QJsonValue &v)
{
    core::tree::ArgRange r;
    const QJsonObject o = v.toObject();
    if (o.contains("MIN")) r.min = core::Value(o.value("MIN").toDouble());
    if (o.contains("MAX")) r.max = core::Value(o.value("MAX").toDouble());
    for (const QJsonValue &x : o.value("VALS").toArray()) r.vals.emplace_back(convert::toUtf8(x.toString()));
    return r;
}

std::vector<std::string> utf8(const QStringList &in)
{
    std::vector<std::string> out;
    for (const QString &s : in) out.push_back(convert::toUtf8(s));
    return out;
}
} // namespace

bool RemoteTree::load(const QJsonObject &root)
{
    core::tree::RemoteTree parsed;
    if (!parsed.load(convert::toJsonText(root))) return false;
    RemoteNode node;
    fill(parsed.root(), &node);
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

bool RemoteNode::isMomentaryAction() const
{
    return core::tree::isMomentaryAction(convert::toUtf8(type), access);
}

bool RemoteNode::toParamInfo(core::tree::ParamInfo *out) const
{
    core::tree::RemoteNode c;
    c.name = convert::toUtf8(name);
    c.path = convert::toUtf8(path);
    c.type = convert::toUtf8(type);
    c.kind = convert::toUtf8(kind);
    c.access = access;
    for (const QJsonValue &r : range) c.range.push_back(argRange(r));
    for (const QJsonValue &v : defaultValue) c.defaultValue.push_back(convert::fromJson(v));
    c.unit = utf8(unit);
    c.extendedType = utf8(extendedType);
    return c.toParamInfo(out);
}

} // namespace pxs::oscquery
