#pragma once
// The bridge between this layer's Qt types and pxs-core's portable ones: QVariant and Value, QString and UTF-8, QJson and JSON
// text, the OSC message of the Qt API and the one of pxs-core. An application that moves from the Qt tree to pxs::core::tree uses
// these at its edges (a QSettings, a widget, a QJsonDocument) and nothing else of Qt in what it keeps.
//
// The conversions keep the *kind*: an int is an int32, a qlonglong an int64, a float a float, a double a double, a QString a string,
// a QByteArray a blob, an invalid QVariant a Nil. What QVariant converts on its own (a string that reads as a number, a double
// that rounds to an int) is not done here: pxs::core::Value's own conversions (number, truthy, text) do it, the same on every
// platform.

#include <pxs/core/osc/codec.h>
#include <pxs/core/tree/parameter.h>
#include <pxs/core/value.h>

#include <OscCodec.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <string>

namespace pxs::convert {

// A Qt string as UTF-8, and back. Back keeps a leading U+FEFF (QString::fromUtf8 alone drops it).
std::string toUtf8(const QString &s);
QString fromUtf8(const std::string &s);

// bool, int (and the smaller integers), unsigned int and the larger integers (an int64), float, double, QString, QByteArray (a blob) and
// an invalid QVariant (Nil) are converted; anything else is Nil. A QVariantList is not a Value: use toValues for the arguments of a
// message.
core::Value toValue(const QVariant &v);
// Nil is an invalid QVariant; a Vec2/Vec3/Vec4 is a QVariantList of floats; a blob is a QByteArray.
QVariant toVariant(const core::Value &v);
core::ValueList toValues(const QVariantList &list);
QVariantList toVariants(const core::ValueList &list);

// The OSC message of the Qt API and pxs-core's: the type tags decide what each argument is on the wire (what the Qt codec did too),
// so an argument is carried as the Value of its kind and re-typed by its tag when it is encoded.
core::osc::Message toCore(const osc::Message &m);
osc::Message fromCore(const core::osc::Message &m);

// JSON: Qt's value types and the text pxs-core reads and writes (an array or object that is not valid JSON text reads as empty).
std::string toJsonText(const QJsonValue &v);
std::string toJsonText(const QJsonObject &o);
std::string toJsonText(const QJsonArray &a);
QJsonValue jsonValueFromText(const std::string &text); // Undefined when the text is not JSON
QJsonObject jsonObjectFromText(const std::string &text);
QJsonArray jsonArrayFromText(const std::string &text);

// A value as a file keeps it (a project, a snapshot): a number, a switch, a text; null for what has none. Back: what a parameter stores.
QJsonValue toJson(const core::Value &v);
core::Value fromJson(const QJsonValue &v); // null, undefined, an object or an array: Nil

// The values of a holder's parameters in a file, by address (only those that are stored: read and written, not an action); read back in
// their order, an address the holder no longer has going to `unknown`, when given. `List` holds pointers (raw or smart) to
// pxs::core::tree::Parameter.
template <class List> QJsonObject parametersToJson(const List &params)
{
    QJsonObject o;
    for (const auto &p : params)
        if (p->isStored()) o.insert(fromUtf8(p->path()), toJson(p->stored()));
    return o;
}
template <class List> void parametersFromJson(const List &params, const QJsonObject &o, QJsonObject *unknown = nullptr)
{
    QJsonObject left = o;
    for (const auto &p : params) {
        const QString key = fromUtf8(p->path());
        const auto it = left.constFind(key);
        if (it == left.constEnd()) continue;
        if (p->isStored()) p->setStored(fromJson(it.value()));
        left.remove(key);
    }
    if (unknown) *unknown = left;
}

} // namespace pxs::convert
