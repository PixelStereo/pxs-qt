#include <pxs/DeviceTargetMenu.h>
#include <pxs/Devices.h>

#include <QMenu>

namespace pxs::devices {

void addDeviceTargets(QMenu *menu, const Devices &devices, QObject *context,
                      const std::function<void(quint64, const ParamInfo &)> &select,
                      quint64 selectedDevice, const QString &selectedPath)
{
    using namespace pxs::core::device;
    const auto entries = devices.all();
    if (entries.empty())
        menu->addAction(QStringLiteral("Add a device in Devices"))->setEnabled(false);

    for (const auto *device : entries) {
        const quint64 id = device->id;
        auto *sub = menu->addMenu(devices.nameOf(id));
        if (!devices.online(id)) sub->setTitle(sub->title() + QStringLiteral(" (offline)"));
        const auto params = devices.params(id);
        if (params.empty()) {
            sub->addAction(QStringLiteral("No writable parameters"))->setEnabled(false);
            continue;
        }

        std::vector<std::string> paths;
        paths.reserve(params.size());
        for (const auto &p : params) paths.push_back(p.path.toStdString());

        const auto addParameter = [&](QMenu *into, QString title, std::size_t index) {
            const auto &p = params[index];
            auto *action = into->addAction(title, context, [select, id, p] { select(id, p); });
            action->setCheckable(true);
            action->setChecked(id == selectedDevice && p.path == selectedPath);
        };
        const auto addBranch = [&](const auto &self, QMenu *into, const ParameterNode &node) -> void {
            if (node.parameter) addParameter(into, params[*node.parameter].path, *node.parameter);
            for (const auto &[segment, child] : node.children) {
                const auto title = segment.empty() ? QStringLiteral("/") : QString::fromStdString(segment);
                if (child.children.empty() && child.parameter)
                    addParameter(into, title, *child.parameter);
                else
                    self(self, into->addMenu(title), child);
            }
        };
        addBranch(addBranch, sub, parameterTree(paths));
    }
}

} // namespace pxs::devices
