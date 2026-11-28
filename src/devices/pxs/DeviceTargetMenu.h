#pragma once

#include <pxs/Parameter.h>

#include <functional>

class QMenu;
class QObject;

namespace pxs::devices {

class Devices;

// Adds a device submenu for each registered device, with writable parameters grouped by address.
void addDeviceTargets(QMenu *menu, const Devices &devices, QObject *context,
                      const std::function<void(quint64, const ParamInfo &)> &select,
                      quint64 selectedDevice = 0, const QString &selectedPath = {});

} // namespace pxs::devices
