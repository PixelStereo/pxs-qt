#pragma once
// A device's parameters as the controls' views see them (pxs/ControlList.h): a control bound to an address of the device reads
// the value last known (Client::listen keeps it up) and writes over OSC. One device per source: the scope is not used.
#include "Client.h"

#include <pxs/ControlList.h>

namespace pxs::oscquery {

class RemoteSource final : public controls::ParameterSource {
public:
    explicit RemoteSource(Client *client) : m_client(client) {}

    bool info(quint64, const QString &path, controls::ParamInfo *out) const override
    {
        const RemoteNode *n = m_client->tree().find(path);
        return n && n->toParamInfo(out);
    }
    QJsonValue value(quint64, const QString &path) const override
    {
        const RemoteNode *n = m_client->tree().find(path);
        return n && !n->value.isEmpty() ? n->value[0] : QJsonValue(QJsonValue::Undefined);
    }
    bool setNumber(quint64, const QString &path, double v) override { return m_client->write(path, QJsonArray{v}); }
    bool setJson(quint64, const QString &path, const QJsonValue &v) override
    {
        const RemoteNode *n = m_client->tree().find(path);
        if (n && n->isMomentaryAction()) return m_client->write(path, {});
        return m_client->write(path, QJsonArray{v});
    }

private:
    Client *m_client;
};

} // namespace pxs::oscquery
