#include <pxs/Bind.h>
#include <pxs/Convert.h>
#include <pxs/core/tree/parameter.h>

#include "Fields.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QPointer>
#include <memory>
#include <optional>

namespace pxs {

namespace {
using core::tree::Node;
using core::tree::Parameter;
using core::tree::Tree;
using core::Value;

std::string leaf(const QString &path) { return convert::toUtf8(path.section('/', -1)); }
std::string address(const QString &path) { return convert::toUtf8(path); }

// A locked node cannot be written from anywhere: its widget is disabled while it is locked, and given back as it was
void followLock(Tree &tree, const QString &path, QWidget *widget)
{
    auto wasEnabled = std::make_shared<std::optional<bool>>();
    QPointer<QWidget> w(widget);
    const std::string where = address(path);
    const int id = tree.addObserver([where, w, wasEnabled](const Node &node, const core::ValueList &) {
        if (!w || node.path() != where) return;
        if (node.locked && !wasEnabled->has_value()) {
            *wasEnabled = w->isEnabled();
            w->setEnabled(false);
        } else if (!node.locked && wasEnabled->has_value()) {
            w->setEnabled(**wasEnabled);
            wasEnabled->reset();
        }
    });
    QObject::connect(widget, &QObject::destroyed, [&tree, id] { tree.removeObserver(id); });
}
}

core::tree::Node &bindToggle(core::tree::Tree &tree, const QString &path, QAbstractButton *button, const QString &description)
{
    QPointer<QAbstractButton> w(button);
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Bool);
    p->byDefault(Value(button->isChecked())); // what it was when exposed
    p->bind([w] { return Value(w && w->isChecked()); },
            [w](const Value &v) { if (w && w->isCheckable() && w->isEnabled()) w->setChecked(v.truthy()); });
    Node &node = tree.add(address(path), std::move(p));
    node.description = convert::toUtf8(description);
    followLock(tree, path, button);
    QObject::connect(button, &QAbstractButton::toggled, button, [&tree, where = address(path)] { tree.changed(where); });
    return node;
}

core::tree::Node &bindNumber(core::tree::Tree &tree, const QString &path, SliderField *field, const QString &description)
{
    QPointer<SliderField> w(field);
    const bool whole = field->decimals() == 0;
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), whole ? Parameter::Type::Int : Parameter::Type::Float);
    p->range(field->minimum(), field->maximum()).byDefault(Value(field->value()));
    p->bind([w, whole] { return w ? (whole ? Value(int(w->value())) : Value(w->value())) : Value(); },
            [w](const Value &v) {
                if (!w || !w->isEnabled()) return;
                w->setValue(v.number().value_or(0.0));
                // What the user's own edit does: the application listens to this signal
                QMetaObject::invokeMethod(w, "editingFinished", Qt::DirectConnection, Q_ARG(double, w->value()));
            });
    Node &node = tree.add(address(path), std::move(p));
    node.description = convert::toUtf8(description);
    followLock(tree, path, field);
    QObject::connect(field, &SliderField::editingFinished, field, [&tree, where = address(path)] { tree.changed(where); });
    return node;
}

core::tree::Node &bindChoice(core::tree::Tree &tree, const QString &path, QComboBox *combo, const QString &description)
{
    QPointer<QComboBox> w(combo);
    std::vector<std::string> keys;
    for (int i = 0; i < combo->count(); ++i) keys.push_back(convert::toUtf8(combo->itemText(i).toLower())); // a Choice's keys are lowercase
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Choice);
    p->choices(keys).byDefault(Value(convert::toUtf8(combo->currentText().toLower())));
    p->bind([w] { return w ? Value(convert::toUtf8(w->currentText().toLower())) : Value(); },
            [w](const Value &v) {
                if (!w || !w->isEnabled()) return;
                const int i = w->findText(convert::fromUtf8(v.text()), Qt::MatchFixedString); // case does not matter
                if (i >= 0) w->setCurrentIndex(i);
            });
    Node &node = tree.add(address(path), std::move(p));
    node.description = convert::toUtf8(description);
    followLock(tree, path, combo);
    QObject::connect(combo, &QComboBox::currentIndexChanged, combo, [&tree, where = address(path)] { tree.changed(where); });
    return node;
}

core::tree::Node &bindText(core::tree::Tree &tree, const QString &path, QLineEdit *edit, const QString &description)
{
    QPointer<QLineEdit> w(edit);
    auto p = std::make_unique<Parameter>(leaf(path), leaf(path), Parameter::Type::Text);
    p->byDefault(Value(convert::toUtf8(edit->text()))).bind([w] { return w ? Value(convert::toUtf8(w->text())) : Value(); },
            [w](const Value &v) {
                if (!w || !w->isEnabled()) return;
                w->setText(convert::fromUtf8(v.text()));
                QMetaObject::invokeMethod(w, "editingFinished", Qt::DirectConnection);
            });
    Node &node = tree.add(address(path), std::move(p));
    node.description = convert::toUtf8(description);
    followLock(tree, path, edit);
    QObject::connect(edit, &QLineEdit::editingFinished, edit, [&tree, where = address(path)] { tree.changed(where); });
    return node;
}

core::tree::Node &bindAction(core::tree::Tree &tree, const QString &path, QAbstractButton *button, const QString &description)
{
    QPointer<QAbstractButton> w(button);
    return tree.addAction(address(path), "N", [w](const core::ValueList &) {
        if (!w || !w->isEnabled()) return false;
        w->click();
        return true;
    }, convert::toUtf8(description));
}

} // namespace pxs
