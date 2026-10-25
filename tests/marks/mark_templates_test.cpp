// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkTemplates.pas (docs/specs/marks.md section 2.3). Ticket T-0091 is a slice of the
// marks module. Appearances come from the real vector::AppearanceOfMarkFactory; the name generator is a
// local fake, so the names are independent of any format. Expected values are literals.
#include "cg/core/config_provider.h"
#include "cg/core/config_writer.h"
#include "cg/marks/mark_name_generator.h"
#include "cg/marks/mark_template.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/category.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace m = cg::marks;
namespace v = cg::vector;
namespace c = cg::core;
using cg::core::Color32;

namespace {

const v::AppearanceOfMarkFactory kAppearances;

// Counts its calls; newName() advances the counter like the real generator (u_MarkNameGenerator.pas:105-107).
class FakeNameGenerator final : public m::IMarkNameGenerator {
public:
    [[nodiscard]] std::shared_ptr<c::IStringConfigDataElement> formatString() const override {
        return nullptr;
    }
    [[nodiscard]] int counter() const override { return calls_; }
    void setCounter(int value) override { calls_ = value; }
    [[nodiscard]] QString newName() override {
        ++calls_;
        return QStringLiteral("Fake ") + QString::number(calls_);
    }

protected:
    void doReadConfig(const c::IConfigProviderPtr&) override {}
    void doWriteConfig(const c::IConfigWriterPtr&) const override {}

private:
    int calls_ = 0;
};

v::AppearancePtr pointApp(Color32 textColor) {
    return kAppearances.createPointAppearance(textColor, 0xFF000002U, 11, QStringLiteral("a.png"), nullptr,
                                              32);
}

v::AppearancePtr lineApp(Color32 lineColor, int lineWidth) {
    return kAppearances.createLineAppearance(lineColor, lineWidth);
}

v::AppearancePtr polyApp(Color32 lineColor, int lineWidth, Color32 fillColor) {
    return kAppearances.createPolygonAppearance(lineColor, lineWidth, fillColor);
}

v::CategoryPtr category(const QString& name) {
    return std::make_shared<v::Category>(name);
}

} // namespace

// u_MarkTemplates.pas:138-154 (IsSameInternal) with the point, line and polygon wrappers
// (:181-184, :204-207, :234-237). Rows: appearance first, then the category (nil on both sides or
// name equal, exact and case-sensitive). The argument template is always a different object.
TEST(MarkTemplates, IsSameComparesAppearanceThenCategory) {
    struct Row {
        const char* label;
        Color32 ownColor;
        std::optional<QString> ownCat;
        Color32 otherColor;
        std::optional<QString> otherCat;
        bool expected;
    };
    const std::vector<Row> rows = {
        {"same appearance and category name", 0xFF0000A1U, QStringLiteral("Trips"), 0xFF0000A1U,
         QStringLiteral("Trips"), true},
        {"appearance differs (text colour)", 0xFF0000A1U, QStringLiteral("Trips"), 0xFF0000A3U,
         QStringLiteral("Trips"), false},
        {"category name case differs", 0xFF0000A1U, QStringLiteral("Trips"), 0xFF0000A1U,
         QStringLiteral("trips"), false},
        {"category name non-ASCII case differs", 0xFF0000A1U, QStringLiteral("Город"), 0xFF0000A1U,
         QStringLiteral("город"), false},
        {"both categories nil", 0xFF0000A1U, std::nullopt, 0xFF0000A1U, std::nullopt, true},
        {"own category nil, other named", 0xFF0000A1U, std::nullopt, 0xFF0000A1U, QStringLiteral("Trips"),
         false},
        {"own category named, other nil", 0xFF0000A1U, QStringLiteral("Trips"), 0xFF0000A1U, std::nullopt,
         false},
    };
    const auto generator = std::make_shared<FakeNameGenerator>();
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        const m::MarkTemplatePoint own(generator, row.ownCat ? category(*row.ownCat) : nullptr,
                                       pointApp(row.ownColor));
        const m::MarkTemplatePoint other(generator, row.otherCat ? category(*row.otherCat) : nullptr,
                                         pointApp(row.otherColor));
        EXPECT_EQ(own.isSame(&other), row.expected);
    }
}

// The same rule for the line and polygon templates (their own isSame overloads): one row each.
TEST(MarkTemplates, LineAndPolygonIsSameUseTheSameRule) {
    const auto generator = std::make_shared<FakeNameGenerator>();
    const std::shared_ptr<m::IMarkNameGenerator> gen = generator;
    const m::MarkTemplateLine line(gen, category(QStringLiteral("Lines")), lineApp(0xFF0000A3U, 2));
    const m::MarkTemplateLine sameLine(gen, category(QStringLiteral("Lines")), lineApp(0xFF0000A3U, 2));
    const m::MarkTemplateLine thickerLine(gen, category(QStringLiteral("Lines")), lineApp(0xFF0000A3U, 3));
    EXPECT_TRUE(line.isSame(&sameLine));
    EXPECT_FALSE(line.isSame(&thickerLine));

    const m::MarkTemplatePoly poly(gen, category(QStringLiteral("Areas")),
                                   polyApp(0xFF000004U, 3, 0xFF000005U));
    const m::MarkTemplatePoly samePoly(gen, category(QStringLiteral("Areas")),
                                       polyApp(0xFF000004U, 3, 0xFF000005U));
    const m::MarkTemplatePoly otherFill(gen, category(QStringLiteral("Areas")),
                                        polyApp(0xFF000004U, 3, 0xFF000006U));
    EXPECT_TRUE(poly.isSame(&samePoly));
    EXPECT_FALSE(poly.isSame(&otherFill));
}

// u_MarkTemplates.pas:158-169, :188-197, :211-222 (Supports): the facets of the right kind are found; a
// template built on an appearance without the facet keeps a nil facet. Assertions are disabled in the
// shipped build (L-30, SASPlanet.dproj:92), so no error is raised.
TEST(MarkTemplates, FacetsAreFoundForTheRightKindAndNilForAnother) {
    const auto generator = std::make_shared<FakeNameGenerator>();
    const std::shared_ptr<m::IMarkNameGenerator> gen = generator;
    EXPECT_NE(m::MarkTemplatePoint(gen, nullptr, pointApp(0xFF0000A1U)).captionAppearance(), nullptr);
    EXPECT_NE(m::MarkTemplatePoint(gen, nullptr, pointApp(0xFF0000A1U)).iconAppearance(), nullptr);
    EXPECT_NE(m::MarkTemplateLine(gen, nullptr, lineApp(0xFF0000A3U, 2)).lineAppearance(), nullptr);
    const m::MarkTemplatePoly ownPoly(gen, nullptr, polyApp(0xFF000004U, 3, 0xFF000005U));
    EXPECT_NE(ownPoly.borderAppearance(), nullptr);
    EXPECT_NE(ownPoly.fillAppearance(), nullptr);

    const m::MarkTemplatePoint point(gen, nullptr, lineApp(0xFF0000A3U, 2));
    EXPECT_EQ(point.captionAppearance(), nullptr);
    EXPECT_EQ(point.iconAppearance(), nullptr);

    const m::MarkTemplateLine line(gen, nullptr, pointApp(0xFF0000A1U));
    EXPECT_EQ(line.lineAppearance(), nullptr);

    const m::MarkTemplatePoly poly(gen, nullptr, pointApp(0xFF0000A1U));
    EXPECT_EQ(poly.borderAppearance(), nullptr);
    EXPECT_EQ(poly.fillAppearance(), nullptr);
}

// u_MarkTemplates.pas:133-136: newName() calls the generator and returns its name; every call is a new
// name, so the generator's counter moves once per call. A nil category is legal (:148).
TEST(MarkTemplates, NewNameCallsTheGeneratorAndNilCategoryIsKept) {
    auto generator = std::make_shared<FakeNameGenerator>();
    const m::MarkTemplatePoint tmpl(generator, nullptr, pointApp(0xFF0000A1U));
    EXPECT_EQ(tmpl.newName(), QStringLiteral("Fake 1"));
    EXPECT_EQ(tmpl.newName(), QStringLiteral("Fake 2"));
    EXPECT_EQ(generator->counter(), 2);
    EXPECT_EQ(tmpl.category(), nullptr);
}

// D-95 (T-0091): IsSame(nil) dereferences ATemplate.Appearance (u_MarkTemplates.pas:143) before the nil test
// at :147: an access violation outside any handler. The port answers false. Regression test.
TEST(MarkTemplates, IsSameOfNullArgumentIsFalse) {
    const auto generator = std::make_shared<FakeNameGenerator>();
    const m::MarkTemplatePoint tmpl(generator, category(QStringLiteral("Trips")), pointApp(0xFF0000A1U));
    EXPECT_FALSE(tmpl.isSame(nullptr));
}

// D-95 (T-0091): the constructor arguments are asserted in legacy (u_MarkTemplates.pas:114-116, :164-165),
// but assertions are off in the shipped build (L-30). A nil appearance or name generator would fail later
// with an access violation (IsEqual :143 / GetNewName :135). The port refuses them at construction.
TEST(MarkTemplates, NilAppearanceOrNameGeneratorIsRejectedAtConstruction) {
    const auto generator = std::make_shared<FakeNameGenerator>();
    const std::shared_ptr<m::IMarkNameGenerator> gen = generator;
    EXPECT_THROW(m::MarkTemplatePoint(gen, nullptr, nullptr), std::invalid_argument);
    EXPECT_THROW(m::MarkTemplateLine(nullptr, nullptr, lineApp(0xFF0000A3U, 2)), std::invalid_argument);
}
