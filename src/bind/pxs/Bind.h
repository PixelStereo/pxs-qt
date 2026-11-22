#pragma once

// Exposing what the interface already has: a check box, a number field, a menu, a text field, a button become a parameter
// or a message of the tree in one line. Writing it from outside (OSC) does what the user's own click does, because it goes
// through the widget; when the user changes the widget the tree says so (Tree::changed), which is the feedback.
//
//     pxs::bindToggle(tree, "/osc/send", sendButton, "Send the signals");   // tree: a pxs::core::tree::Tree
//     pxs::bindNumber(tree, "/detection/confidence", minDetection, "Confidence needed to acquire a subject");
//     pxs::bindChoice(tree, "/analysis/size", sizeCombo);
//     pxs::bindAction(tree, "/video/stop", stopButton);          // a message: no state
//
// All on the GUI thread (the OscService runs there). A widget destroyed before the tree is simply no longer exposed.

#include <pxs/core/tree/tree.h>

#include <QString>

class QAbstractButton;
class QComboBox;
class QLineEdit;
class SliderField;

namespace pxs {

core::tree::Node &bindToggle(core::tree::Tree &tree, const QString &path, QAbstractButton *button, const QString &description = {});
// Float, or Int when the field has no decimals; the range is the field's own
core::tree::Node &bindNumber(core::tree::Tree &tree, const QString &path, SliderField *field, const QString &description = {});
// The items' texts, in lowercase, are the keys (a number is also accepted: the index)
core::tree::Node &bindChoice(core::tree::Tree &tree, const QString &path, QComboBox *combo, const QString &description = {});
core::tree::Node &bindText(core::tree::Tree &tree, const QString &path, QLineEdit *edit, const QString &description = {});
// A message that clicks the button (when it is enabled)
core::tree::Node &bindAction(core::tree::Tree &tree, const QString &path, QAbstractButton *button, const QString &description = {});

} // namespace pxs
