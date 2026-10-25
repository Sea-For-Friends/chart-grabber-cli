// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (the T-0129 row assigns none). Ports u_VectorDataFactoryForMap.pas;
// docs/specs/vector.md §2.10.2, §8.1 LV-3, §7.2.
#include "cg/vector/html_to_hint_text_converter_stuped.h"
#include "cg/vector/vector_data_factory_for_map.h"

#include <gtest/gtest.h>

using namespace cg::vector;
using cg::core::HashValue;

namespace {

// A prefix that can be changed after the items were built (legacy GetValue is read at call time).
class FakeStringProvider final : public IStringProvider {
public:
    QString text;
    [[nodiscard]] QString value() const override { return text; }
};

std::shared_ptr<FakeStringProvider> prefix(const QString& text) {
    auto p = std::make_shared<FakeStringProvider>();
    p->text = text;
    return p;
}

VectorDataItemMainInfoFactoryForMap mapFactory() {
    return VectorDataItemMainInfoFactoryForMap(std::make_shared<HtmlToHintTextConverterStuped>());
}

// Answers the name-only question with a canned reply and the name-and-description question with
// "H:<name>|<desc>", so each hintText branch is visible in the result.
class FakeConverter final : public IHtmlToHintTextConverter {
public:
    explicit FakeConverter(QString replyWithoutDesc) : replyWithoutDesc_(std::move(replyWithoutDesc)) {}
    [[nodiscard]] QString convert(const QString& name, const QString& description) const override {
        if (description.isEmpty()) {
            return replyWithoutDesc_;
        }
        return QStringLiteral("H:") + name + QStringLiteral("|") + description;
    }

private:
    QString replyWithoutDesc_;
};

} // namespace

TEST(VectorDataFactoryForMap, IndexCountsFromZeroPerIdDataAndInfoUrlReadsThePrefixAtCallTime) {
    const auto p = prefix(QStringLiteral("карта/"));
    IdData first;
    first.urlPrefix = p;
    IdData second;
    second.urlPrefix = p;
    const auto factory = mapFactory();

    const auto a = factory.buildMainInfo(&first, QStringLiteral("A"), QString());
    const auto b = factory.buildMainInfo(&first, QStringLiteral("B"), QString());
    const auto c = factory.buildMainInfo(&second, QStringLiteral("C"), QString());
    EXPECT_EQ(a->infoUrl(), QStringLiteral("карта/0/"));
    EXPECT_EQ(b->infoUrl(), QStringLiteral("карта/1/"));
    EXPECT_EQ(c->infoUrl(), QStringLiteral("карта/0/")); // the other IdData starts again at 0
    EXPECT_EQ(first.nextIndex.load(), 2);
    EXPECT_EQ(second.nextIndex.load(), 1);

    // Legacy GetInfoUrl (u_VectorDataFactoryForMap.pas:147-150) is evaluated per call.
    p->text = QStringLiteral("Карта/");
    EXPECT_EQ(a->infoUrl(), QStringLiteral("Карта/0/"));
    EXPECT_EQ(b->name(), QStringLiteral("B"));
}

TEST(VectorDataFactoryForMap, HashIsNameThenDescAndIndexIsNotHashed) {
    IdData idData;
    idData.urlPrefix = prefix(QStringLiteral("p/"));
    const auto factory = mapFactory();
    const QString name = QStringLiteral("NameЖ");
    const QString desc = QStringLiteral("Desc");

    // Expected value from the raw primitives, not from the factory under test.
    HashValue expected = cg::core::hashOf(QStringView{name});
    cg::core::hashUpdate(expected, QStringView{desc});

    const auto first = factory.buildMainInfo(&idData, name, desc);
    const auto second = factory.buildMainInfo(&idData, name, desc);
    EXPECT_EQ(first->hash(), expected);
    EXPECT_EQ(second->hash(), expected); // a different index, the same hash
    EXPECT_EQ(first->name(), name);
    EXPECT_EQ(first->desc(), desc);
}

TEST(VectorDataFactoryForMap, IsEqualIgnoresIndexAndUrlLV3) {
    IdData one;
    one.urlPrefix = prefix(QStringLiteral("http://one/"));
    IdData two;
    two.urlPrefix = prefix(QStringLiteral("http://two/"));
    const auto factory = mapFactory();
    const auto a = factory.buildMainInfo(&one, QStringLiteral("Name"), QStringLiteral("d"));
    const auto b = factory.buildMainInfo(&two, QStringLiteral("Name"), QStringLiteral("d"));
    ASSERT_NE(a->infoUrl(), b->infoUrl());
    // Equal name and desc, different index and URL: equal (LV-3, u_VectorDataFactoryForMap.pas:157-182).
    EXPECT_TRUE(a->isEqual(b.get()));
    EXPECT_TRUE(b->isEqual(a.get()));
    EXPECT_TRUE(a->isEqual(a.get()));

    const auto otherName = factory.buildMainInfo(&two, QStringLiteral("name"), QStringLiteral("d"));
    const auto otherDesc = factory.buildMainInfo(&two, QStringLiteral("Name"), QStringLiteral("D"));
    EXPECT_FALSE(a->isEqual(otherName.get()));
    EXPECT_FALSE(a->isEqual(otherDesc.get()));
    EXPECT_FALSE(a->isEqual(nullptr));
}

// hintText, infoHtml and infoCaption are the map class's own code (u_VectorDataFactoryForMap.pas:
// 124-135 and 137-145). Literals from the Pascal; the name is non-ASCII.
TEST(VectorDataFactoryForMap, HintTextInfoHtmlAndInfoCaptionOfMapItem) {
    struct Row {
        const char* what;
        QString replyWithoutDesc; // what the converter answers to (name, '')
        QString desc;
        QString expectedHint;
        QString expectedHtml;
    };
    const QString name = QStringLiteral("Город");
    const Row rows[] = {
        // name-only answer empty: the converter is asked again with the description
        {"empty first answer, non-empty desc", QString(), QStringLiteral("Описание"),
         QStringLiteral("H:Город|Описание"), QStringLiteral("<HTML><BODY>Описание</BODY></HTML>")},
        // name-only answer non-empty: it is the hint, the second question is not used
        {"non-empty first answer, non-empty desc", QStringLiteral("Короткое"), QStringLiteral("Описание"),
         QStringLiteral("Короткое"), QStringLiteral("<HTML><BODY>Описание</BODY></HTML>")},
        // empty description: no HTML at all
        {"empty first answer, empty desc", QString(), QString(), QString(), QString()},
        {"non-empty first answer, empty desc", QStringLiteral("Короткое"), QString(),
         QStringLiteral("Короткое"), QString()},
    };
    for (const auto& r : rows) {
        const auto factory =
            VectorDataItemMainInfoFactoryForMap(std::make_shared<FakeConverter>(r.replyWithoutDesc));
        IdData idData;
        idData.urlPrefix = prefix(QStringLiteral("p/"));
        const auto item = factory.buildMainInfo(&idData, name, r.desc);
        EXPECT_EQ(item->hintText(), r.expectedHint) << r.what;
        EXPECT_EQ(item->infoHtml(), r.expectedHtml) << r.what;
        EXPECT_EQ(item->infoCaption(), name) << r.what;
    }
}
