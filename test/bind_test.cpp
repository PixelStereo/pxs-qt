#include <pxs/Bind.h>
#include <pxs/Convert.h>
#include <pxs/core/tree/oscquery.h>

#include "Fields.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <cstdio>

using pxs::core::Value;
static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    pxs::core::tree::Tree tree;
    std::vector<pxs::core::osc::Message> out;
    tree.setOutput([&](const pxs::core::osc::Message &m) { out.push_back(m); });
    const auto json = [&](const char *path) { return pxs::convert::jsonObjectFromText(pxs::core::tree::describeJson(tree, path)); };

    QCheckBox check;
    SliderField number;
    number.setRange(0, 1);
    number.setDecimals(2);
    SliderField whole;
    whole.setRange(1, 4);
    whole.setDecimals(0);
    QComboBox combo;
    combo.addItems({"Small", "Medium", "Large"});
    QLineEdit edit;
    QPushButton button;
    int clicks = 0, finished = 0;
    QObject::connect(&button, &QPushButton::clicked, [&] { ++clicks; });
    QObject::connect(&number, &SliderField::editingFinished, [&](double) { ++finished; });

    pxs::bindToggle(tree, "/osc/send", &check, "Send");
    pxs::bindNumber(tree, "/detection/confidence", &number);
    pxs::bindNumber(tree, "/detection/hands", &whole);
    pxs::bindChoice(tree, "/analysis/size", &combo);
    pxs::bindText(tree, "/osc/destinations", &edit);
    pxs::bindAction(tree, "/video/stop", &button);

    // Written from outside: the widget changes the way a click would
    CHECK(tree.apply("/osc/send", {Value(true)}) == 1 && check.isChecked());
    CHECK(tree.apply("/detection/confidence", {Value(0.4)}) == 1 && number.value() == 0.4 && finished == 1);
    CHECK(tree.apply("/detection/hands", {Value(2.0)}) == 1 && whole.value() == 2);
    CHECK(tree.apply("/analysis/size", {Value("Large")}) == 1 && combo.currentText() == "Large");
    CHECK(tree.apply("/analysis/size", {Value("Huge")}) == 0 && combo.currentText() == "Large");
    CHECK(tree.apply("/osc/destinations", {Value("127.0.0.1:5000")}) == 1 && edit.text() == "127.0.0.1:5000");
    CHECK(tree.apply("/video/stop", {}) == 1 && clicks == 1);
    button.setEnabled(false);
    CHECK(tree.apply("/video/stop", {}) == 0 && clicks == 1);

    // The user changes a widget: the tree says it (feedback)
    out.clear();
    check.setChecked(false);
    CHECK(out.size() == 1 && out[0].address == "/osc/send");
    out.clear();
    combo.setCurrentIndex(0);
    CHECK(out.size() == 1 && out[0].address == "/analysis/size" && std::get<std::string>(out[0].args.at(0)) == "small");

    // A locked node: the widget is disabled and OSC does not write it; unlocked, it is given back as it was
    CHECK(tree.setLocked("/osc/send", true) && !check.isEnabled() && tree.apply("/osc/send", {Value(true)}) == 0);
    CHECK(tree.setLocked("/osc/send", false) && check.isEnabled());
    // Reset puts back what the widget held when it was exposed
    check.setChecked(true);
    CHECK(tree.reset("/osc/send") && !check.isChecked());

    // What OSCQuery says
    const QJsonObject c = json("/detection/confidence");
    CHECK(c["TYPE"] == "f" && c["RANGE"].toArray().at(0).toObject()["MAX"] == 1.0 && c["VALUE"].toArray().at(0).toDouble() == 0.4);
    CHECK(json("/detection/hands")["TYPE"] == "i" && json("/video/stop")["ACCESS"] == 2 && !json("/video/stop").contains("VALUE"));
    CHECK(json("/analysis/size")["RANGE"].toArray().at(0).toObject()["VALS"].toArray().size() == 3);
    return failures ? 1 : 0;
}
