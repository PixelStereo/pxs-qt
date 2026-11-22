#include "pxs/Convert.h"

#include <QJsonDocument>
#include <QMetaType>

namespace pxs::convert {

std::string toUtf8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), std::size_t(b.size()));
}

QString fromUtf8(const std::string &s)
{
    std::size_t bom = 0; // leading U+FEFF (EF BB BF), as many as there are
    while (s.size() >= (bom + 1) * 3 && s.compare(bom * 3, 3, "\xEF\xBB\xBF") == 0) ++bom;
    QString out(qsizetype(bom), QChar(0xFEFF));
    out += QString::fromUtf8(s.data() + bom * 3, qsizetype(s.size() - bom * 3));
    return out;
}

core::Value toValue(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Bool: return core::Value(v.toBool());
    case QMetaType::Int:
    case QMetaType::Short:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::UShort: return core::Value(v.toInt());
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: return core::Value(static_cast<std::int64_t>(v.toLongLong()));
    case QMetaType::Float: return core::Value(v.toFloat());
    case QMetaType::Double: return core::Value(v.toDouble());
    case QMetaType::QString: return core::Value(toUtf8(v.toString()));
    case QMetaType::QByteArray: {
        const QByteArray b = v.toByteArray();
        const auto *first = reinterpret_cast<const std::uint8_t *>(b.constData());
        return core::Value(core::Blob(first, first + b.size()));
    }
    default: return core::Value(); // an invalid QVariant, and what has no counterpart
    }
}

QVariant toVariant(const core::Value &v)
{
    using K = core::Value::Kind;
    switch (v.kind()) {
    case K::Nothing: return {};
    case K::Boolean: return *v.get<bool>();
    case K::Integer32: return int(*v.get<std::int32_t>());
    case K::Integer64: return qlonglong(*v.get<std::int64_t>());
    case K::Float: return *v.get<float>();
    case K::Double: return *v.get<double>();
    case K::String: return fromUtf8(*v.get<std::string>());
    case K::Vector2: { QVariantList l; for (float f : *v.get<core::Vec2>()) l << f; return l; }
    case K::Vector3: { QVariantList l; for (float f : *v.get<core::Vec3>()) l << f; return l; }
    case K::Vector4: { QVariantList l; for (float f : *v.get<core::Vec4>()) l << f; return l; }
    case K::Bytes: {
        const auto &b = *v.get<core::Blob>();
        return QByteArray(reinterpret_cast<const char *>(b.data()), qsizetype(b.size()));
    }
    }
    return {};
}

core::ValueList toValues(const QVariantList &list)
{
    core::ValueList out;
    out.reserve(std::size_t(list.size()));
    for (const QVariant &v : list) out.push_back(toValue(v));
    return out;
}

QVariantList toVariants(const core::ValueList &list)
{
    QVariantList out;
    out.reserve(qsizetype(list.size()));
    for (const core::Value &v : list) out << toVariant(v);
    return out;
}

namespace {

// The wire type of an argument is its tag's; what is carried between the two APIs is the argument as the Value of its kind
core::osc::Value wire(const core::Value &v)
{
    using K = core::Value::Kind;
    switch (v.kind()) {
    case K::Boolean: return core::osc::Value(*v.get<bool>());
    case K::Integer32: return core::osc::Value(*v.get<std::int32_t>());
    case K::Integer64: return core::osc::Value(*v.get<std::int64_t>());
    case K::Float: return core::osc::Value(*v.get<float>());
    case K::Double: return core::osc::Value(*v.get<double>());
    case K::String: return core::osc::Value(*v.get<std::string>());
    case K::Bytes: {
        const auto &b = *v.get<core::Blob>();
        return core::osc::Value(core::osc::Blob(b.begin(), b.end()));
    }
    default: return core::osc::Value(); // Nil, and a vector (a message carries its components one by one)
    }
}

core::Value unwire(const core::osc::Value &a)
{
    struct V {
        core::Value operator()(const std::monostate &) const { return {}; }
        core::Value operator()(bool x) const { return core::Value(x); }
        core::Value operator()(std::int32_t x) const { return core::Value(x); }
        core::Value operator()(std::int64_t x) const { return core::Value(x); }
        core::Value operator()(float x) const { return core::Value(x); }
        core::Value operator()(double x) const { return core::Value(x); }
        core::Value operator()(const std::string &x) const { return core::Value(x); }
        core::Value operator()(const core::osc::Blob &x) const { return core::Value(core::Blob(x)); }
        core::Value operator()(std::uint32_t x) const { return core::Value(static_cast<std::int64_t>(x)); }
    };
    return std::visit(V{}, a);
}

} // namespace

core::osc::Message toCore(const osc::Message &m)
{
    core::osc::Message out;
    out.address = toUtf8(m.address);
    out.types = m.types.toLatin1().toStdString();
    out.args.reserve(std::size_t(m.args.size()));
    for (const QVariant &a : m.args) out.args.push_back(wire(toValue(a)));
    return out;
}

osc::Message fromCore(const core::osc::Message &m)
{
    osc::Message out;
    out.address = fromUtf8(m.address);
    out.types = QString::fromLatin1(m.types.data(), qsizetype(m.types.size()));
    for (const core::osc::Value &a : m.args) out.args << toVariant(unwire(a));
    return out;
}

QJsonValue toJson(const core::Value &v)
{
    using K = core::Value::Kind;
    switch (v.kind()) {
    case K::Boolean: return *v.get<bool>();
    case K::Integer32: return int(*v.get<std::int32_t>());
    case K::Integer64: return qlonglong(*v.get<std::int64_t>());
    case K::Float: return double(*v.get<float>());
    case K::Double: return *v.get<double>();
    case K::String: return fromUtf8(*v.get<std::string>());
    default: return QJsonValue(); // Nil, and what a file does not keep
    }
}

core::Value fromJson(const QJsonValue &v)
{
    if (v.isBool()) return core::Value(v.toBool());
    if (v.isDouble()) return core::Value(v.toDouble());
    if (v.isString()) return core::Value(toUtf8(v.toString()));
    return {};
}

std::string toJsonText(const QJsonValue &v)
{
    // QJsonDocument holds only an object or an array: a scalar is wrapped and unwrapped
    const QByteArray b = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return std::string(b.constData() + 1, std::size_t(b.size() - 2)); // inside the [ ]
}
std::string toJsonText(const QJsonObject &o)
{
    const QByteArray b = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return std::string(b.constData(), std::size_t(b.size()));
}
std::string toJsonText(const QJsonArray &a)
{
    const QByteArray b = QJsonDocument(a).toJson(QJsonDocument::Compact);
    return std::string(b.constData(), std::size_t(b.size()));
}

QJsonValue jsonValueFromText(const std::string &text)
{
    const QByteArray wrapped = "[" + QByteArray(text.data(), qsizetype(text.size())) + "]";
    QJsonParseError error;
    const QJsonDocument d = QJsonDocument::fromJson(wrapped, &error);
    if (error.error != QJsonParseError::NoError || !d.isArray() || d.array().size() != 1) return QJsonValue(QJsonValue::Undefined);
    return d.array().first();
}
QJsonObject jsonObjectFromText(const std::string &text)
{
    return QJsonDocument::fromJson(QByteArray(text.data(), qsizetype(text.size()))).object();
}
QJsonArray jsonArrayFromText(const std::string &text)
{
    return QJsonDocument::fromJson(QByteArray(text.data(), qsizetype(text.size()))).array();
}

} // namespace pxs::convert
