#pragma once
// What an OSCQuery device says of itself (its / answer), kept as a tree the application can browse and read: each node's address, type
// tags, access, range, description and last value. A node that holds one argument can be seen as a ParamInfo (pxs-core), which is
// how a control becomes a view of a remote parameter (RemoteSource.h).
#include <pxs/core/tree/parameter.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <map>

namespace pxs::oscquery {

struct RemoteNode {
    QString name;        // its segment ("opacity"); the root's is empty
    QString path;        // "/layer/1/opacity"; the root's is "/"
    QString type;        // OSC type tags, one per argument; empty for a container
    QString description;
    QString kind;        // "action" (written, no value), "signal" (read, no value), or empty (a parameter, or a container)
    int access = 0;      // 0 none (a container), 1 read, 2 write, 3 both
    QJsonArray range;    // one entry per argument: {"MIN","MAX"} or {"VALS"}
    QJsonArray value;    // the last one known, one entry per argument (empty: none known)
    QStringList extendedType; // EXTENDED_TYPE, one per argument ("" for none): "color.rgb.r", "filepath"…
    QStringList unit;         // UNIT, one per argument ("" for none): "distance.m"
    QStringList tags;         // TAGS
    QJsonArray defaultValue;  // DEFAULT_VALUE: what a reset writes, one entry per argument (empty: none announced)
    std::map<QString, RemoteNode> children;

    // pxs-core's rule (core::tree::RemoteNode::hasValue): a type or an access is a value, whether the node holds other nodes or not
    bool hasValue() const { return !type.isEmpty() || access != 0; }
    bool isContainer() const { return !hasValue(); }
    bool isLeaf() const { return hasValue(); }
    // As one parameter: false when it has no type or several arguments (a leaf of one argument only)
    bool isMomentaryAction() const;
    bool toParamInfo(core::tree::ParamInfo *out) const;
};

class RemoteTree {
public:
    // Replaces everything with a device's answer to GET /; false when it is not a node
    bool load(const QJsonObject &root);
    void clear() { m_root = RemoteNode(); m_loaded = false; }
    bool loaded() const { return m_loaded; }
    const RemoteNode &root() const { return m_root; }
    const RemoteNode *find(const QString &path) const;
    // A value the device told (LISTEN, ?VALUE, a message); false: no such node
    bool setValue(const QString &path, const QJsonArray &value);
    // What the tree is made of, not its values: two trees with the same signature show the same
    QByteArray signature() const;
    // The addresses of every leaf, in order
    QStringList leaves() const;

private:
    RemoteNode m_root;
    bool m_loaded = false;
};

} // namespace pxs::oscquery
