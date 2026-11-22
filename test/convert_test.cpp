#include "pxs/Convert.h"

#include <QCoreApplication>
#include <QDate>
#include <memory>
#include <vector>
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Kinds are kept, both ways
    CHECK(convert::toValue(QVariant()).isNil());
    CHECK(convert::toValue(true) == core::Value(true));
    CHECK(convert::toValue(7) == core::Value(7));
    CHECK(convert::toValue(short(7)) == core::Value(7));
    CHECK(convert::toValue(qlonglong(1) << 40) == core::Value(std::int64_t(1) << 40));
    CHECK(convert::toValue(4000000000u) == core::Value(std::int64_t(4000000000LL)));
    CHECK(convert::toValue(0.5f).is<float>() && convert::toValue(0.5).is<double>());
    CHECK(convert::toValue(QStringLiteral("hé")) == core::Value("h\xC3\xA9"));
    CHECK(convert::toValue(QByteArray("\x01\x02", 2)) == core::Value(core::Blob{1, 2}));
    CHECK(convert::toValue(QVariant(QDate(2020, 1, 1))).isNil()); // no counterpart: Nil
    const QVariant samples[] = {QVariant(), true, 7, qlonglong(1) << 40, 0.5f, 0.25, QStringLiteral("été"), QByteArray("\x00\xff", 2)};
    for (const QVariant &v : samples) {
        const QVariant back = convert::toVariant(convert::toValue(v));
        CHECK(back.metaType() == v.metaType() && back == v);
    }
    const QVariantList vec3{1.0f, 2.0f, 3.0f};
    CHECK(convert::toVariant(core::Value(core::Vec3{1, 2, 3})) == QVariant(vec3));

    // Lists of arguments
    const QVariantList args{1, 0.5, QStringLiteral("x"), QVariant()};
    CHECK(convert::toVariants(convert::toValues(args)) == args);
    const QVariantList noCounterpart{1, 2};
    CHECK(convert::toValue(noCounterpart).isNil());

    // A string keeps a leading byte order mark (QString::fromUtf8 alone drops it) and every character
    const QString bom = QString(QChar(0xFEFF)) + QStringLiteral("Layer");
    CHECK(convert::fromUtf8(convert::toUtf8(bom)) == bom);
    CHECK(convert::fromUtf8(convert::toUtf8(QString(2, QChar(0xFEFF)))) == QString(2, QChar(0xFEFF)));
    CHECK(convert::fromUtf8("") == QString() && convert::toUtf8(QString()).empty());
    CHECK(convert::fromUtf8(convert::toUtf8(QStringLiteral("\U0001F600 €"))) == QStringLiteral("\U0001F600 €"));

    // OSC messages: the tags decide the wire type, so the arguments only have to keep their kind
    osc::Message m{QStringLiteral("/é/x"), QStringLiteral("ifsb"), {3, 0.5, QStringLiteral("a"), QByteArray("\x01", 1)}};
    const core::osc::Message c = convert::toCore(m);
    CHECK(c.address == "/\xC3\xA9/x" && c.types == "ifsb" && c.args.size() == 4);
    const osc::Message back = convert::fromCore(c);
    CHECK(back.address == m.address && back.types == m.types && back.args == m.args);
    // and what is encoded by pxs-core reads back through the Qt codec
    std::vector<osc::Message> decoded;
    const auto bytes = core::osc::encode(core::osc::Message{"/x", "if", {std::int32_t(5), 0.5f}});
    CHECK(osc::decode(QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())), decoded) && decoded.size() == 1);

    // JSON text
    const QJsonObject sample{{"b", 1}, {"a", QJsonArray{true, "z"}}};
    CHECK(convert::toJsonText(sample) == "{\"a\":[true,\"z\"],\"b\":1}");
    CHECK(convert::toJsonText(QJsonArray{1, 2.5}) == "[1,2.5]");
    CHECK(convert::toJsonText(QJsonValue(3.5)) == "3.5" && convert::toJsonText(QJsonValue("s")) == "\"s\"" && convert::toJsonText(QJsonValue(true)) == "true");
    CHECK(convert::toJsonText(QJsonValue(QJsonValue::Null)) == "null");
    CHECK(convert::jsonValueFromText("3.5") == QJsonValue(3.5) && convert::jsonValueFromText("\"s\"") == QJsonValue("s"));
    CHECK(convert::jsonValueFromText("{nope").isUndefined() && convert::jsonValueFromText("1,2").isUndefined() && convert::jsonValueFromText("").isUndefined());
    const QJsonObject expectedObject{{"k", QJsonArray{1}}};
    const QJsonArray expectedArray{1, "a"};
    CHECK(convert::jsonObjectFromText("{\"k\":[1]}") == expectedObject);
    CHECK(convert::jsonObjectFromText("[1]").isEmpty());
    CHECK(convert::jsonArrayFromText("[1,\"a\"]") == expectedArray);
    CHECK(convert::jsonArrayFromText("nope").isEmpty());

    // The values of parameters in a file, by address
    {
        double level = 0.5;
        bool on = true;
        std::string mode = "b";
        std::vector<std::unique_ptr<core::tree::Parameter>> params;
        auto add = [&](const char *path, core::tree::ParamInfo::Type type) { params.push_back(std::make_unique<core::tree::Parameter>(path, path, type)); return params.back().get(); };
        add("level", core::tree::ParamInfo::Type::Float)->range(0, 1).bind([&] { return core::Value(level); }, [&](const core::Value &v) { level = v.number().value_or(0.0); });
        add("on", core::tree::ParamInfo::Type::Bool)->bind([&] { return core::Value(on); }, [&](const core::Value &v) { on = v.truthy(); });
        auto *choice = add("mode", core::tree::ParamInfo::Type::Choice);
        choice->choices({"a", "b"}).bind([&] { return core::Value(mode); }, [&](const core::Value &v) { mode = v.text(); });
        add("go", core::tree::ParamInfo::Type::Trigger)->bind([] { return core::Value(); }, [](const core::Value &) {});
        add("meter", core::tree::ParamInfo::Type::Float)->readOnly().bind([] { return core::Value(1.0); }, [](const core::Value &) {});
        const QJsonObject saved = convert::parametersToJson(params);
        CHECK(saved.size() == 3 && saved["level"] == 0.5 && saved["on"] == true && saved["mode"] == "b"); // not the trigger, not the meter
        level = 0.9; on = false; mode = "a";
        QJsonObject unknown;
        QJsonObject file = saved;
        file["gone"] = 1;
        convert::parametersFromJson(params, file, &unknown);
        CHECK(level == 0.5 && on && mode == "b" && unknown.size() == 1 && unknown.contains("gone"));
        CHECK(convert::toJson(core::Value(7)) == QJsonValue(7) && convert::toJson(core::Value()).isNull() && convert::fromJson(QJsonValue(true)) == core::Value(true));
        CHECK(convert::fromJson(QJsonValue("t")) == core::Value("t") && convert::fromJson(QJsonValue(QJsonObject())).isNil() && convert::fromJson(QJsonValue(2.5)) == core::Value(2.5));
    }

    return failures ? 1 : 0;
}
