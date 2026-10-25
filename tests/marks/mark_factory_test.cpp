// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkFactory.pas (docs/specs/marks.md section 2.3). Hash expectations are assembled from
// byte buffers and the raw CityHash primitive, not from the factory.
#include "cg/core/hash.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_factory.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/vector_data_item_base.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace g = cg::geo;
namespace t = cg::marks::testutil;
using cg::core::HashValue;

namespace {

// Geometry stand-ins for the kinds the factory dispatches on.
template <class Marker> class FakeGeometry final : public Marker {
public:
    explicit FakeGeometry(HashValue hash) : hash_(hash) {}
    HashValue hash() const noexcept override { return hash_; }
    g::LonLatRect bounds() const noexcept override { return {}; }
    g::LonLat goToPoint() const override { return {}; }
    bool isSameGeometry(const g::IGeometryLonLat& other) const override { return &other == this; }

private:
    HashValue hash_;
};
using FakeLine = FakeGeometry<g::IGeometryLonLatLine>;
using FakePoly = FakeGeometry<g::IGeometryLonLatPolygon>;

g::GeometryLonLatPtr point(HashValue hash) {
    return std::make_shared<g::GeometryLonLatPoint>(hash, g::LonLat{1.0, 2.0});
}

// Templates name themselves by a counter, as the name generator does.
class TplBase {
public:
    TplBase(QString prefix, v::AppearancePtr appearance, v::CategoryPtr category)
        : prefix_(std::move(prefix)), appearance_(std::move(appearance)), category_(std::move(category)) {}
    QString nextName() const { return prefix_ + QString::number(++counter_); }
    v::AppearancePtr app() const { return appearance_; }
    v::CategoryPtr cat() const { return category_; }

private:
    QString prefix_;
    v::AppearancePtr appearance_;
    v::CategoryPtr category_;
    mutable int counter_ = 0;
};
class TplPoint final : public m::IMarkTemplatePoint, TplBase {
public:
    using TplBase::TplBase;
    QString newName() const override { return nextName(); }
    v::AppearancePtr appearance() const override { return app(); }
    v::CategoryPtr category() const override { return cat(); }
    v::AppearancePointCaptionPtr captionAppearance() const override { return nullptr; }
    v::AppearancePointIconPtr iconAppearance() const override { return nullptr; }
    bool isSame(const m::IMarkTemplatePoint*) const override { return false; }
};
class TplLine final : public m::IMarkTemplateLine, TplBase {
public:
    using TplBase::TplBase;
    QString newName() const override { return nextName(); }
    v::AppearancePtr appearance() const override { return app(); }
    v::CategoryPtr category() const override { return cat(); }
    v::AppearanceLinePtr lineAppearance() const override { return nullptr; }
    bool isSame(const m::IMarkTemplateLine*) const override { return false; }
};
class TplPoly final : public m::IMarkTemplatePoly, TplBase {
public:
    using TplBase::TplBase;
    QString newName() const override { return nextName(); }
    v::AppearancePtr appearance() const override { return app(); }
    v::CategoryPtr category() const override { return cat(); }
    v::AppearancePolygonBorderPtr borderAppearance() const override { return nullptr; }
    v::AppearancePolygonFillPtr fillAppearance() const override { return nullptr; }
    bool isSame(const m::IMarkTemplatePoly*) const override { return false; }
};

template <class Iface, class TplPtr> class FakeTemplateConfig final : public Iface {
public:
    explicit FakeTemplateConfig(TplPtr def) : def_(std::move(def)) {}
    TplPtr createTemplate(const v::AppearancePtr&, const v::CategoryPtr&) const override { return nullptr; }
    TplPtr defaultTemplate() const override { return def_; }
    void setDefaultTemplate(const TplPtr& value) override { def_ = value; }
    std::shared_ptr<m::IMarkNameGenerator> nameGenerator() const override { return nullptr; }

protected:
    void doReadConfig(const cg::core::IConfigProviderPtr&) override {}
    void doWriteConfig(const cg::core::IConfigWriterPtr&) const override {}

private:
    TplPtr def_;
};

class FakeFactoryConfig final : public m::IMarkFactoryConfig {
public:
    FakeFactoryConfig(m::MarkTemplatePointPtr p, m::MarkTemplateLinePtr l, m::MarkTemplatePolyPtr y)
        : point_(std::make_shared<FakeTemplateConfig<m::IMarkPointTemplateConfig, m::MarkTemplatePointPtr>>(
              std::move(p))),
          line_(std::make_shared<FakeTemplateConfig<m::IMarkLineTemplateConfig, m::MarkTemplateLinePtr>>(
              std::move(l))),
          poly_(std::make_shared<FakeTemplateConfig<m::IMarkPolyTemplateConfig, m::MarkTemplatePolyPtr>>(
              std::move(y))) {}
    std::shared_ptr<m::IMarkPointTemplateConfig> pointTemplateConfig() const override { return point_; }
    std::shared_ptr<m::IMarkLineTemplateConfig> lineTemplateConfig() const override { return line_; }
    std::shared_ptr<m::IMarkPolyTemplateConfig> polyTemplateConfig() const override { return poly_; }

protected:
    void doReadConfig(const cg::core::IConfigProviderPtr&) override {}
    void doWriteConfig(const cg::core::IConfigWriterPtr&) const override {}

private:
    std::shared_ptr<m::IMarkPointTemplateConfig> point_;
    std::shared_ptr<m::IMarkLineTemplateConfig> line_;
    std::shared_ptr<m::IMarkPolyTemplateConfig> poly_;
};

const v::AppearanceOfMarkFactory kAppearances;

QString q(const char* s) {
    return QString::fromLatin1(s);
}

struct Fixture {
    v::CategoryPtr pointCat = std::make_shared<m::MarkCategory>(q("pc"), true, 3, 23);
    v::CategoryPtr lineCat = std::make_shared<m::MarkCategory>(q("lc"), true, 3, 23);
    v::CategoryPtr polyCat = std::make_shared<m::MarkCategory>(q("yc"), true, 3, 23);
    v::AppearancePtr pointApp =
        kAppearances.createPointAppearance(0xFF000001U, 0xFF000002U, 11, q("a.png"), nullptr, 32);
    v::AppearancePtr lineApp = kAppearances.createLineAppearance(0xFF000003U, 2);
    v::AppearancePtr polyApp = kAppearances.createPolygonAppearance(0xFF000004U, 3, 0xFF000005U);
    std::shared_ptr<FakeFactoryConfig> config =
        std::make_shared<FakeFactoryConfig>(std::make_shared<TplPoint>(q("Point"), pointApp, pointCat),
                                            std::make_shared<TplLine>(q("Line"), lineApp, lineCat),
                                            std::make_shared<TplPoly>(q("Poly"), polyApp, polyCat));
    m::MarkFactory factory{config, nullptr, std::make_shared<v::AppearanceOfMarkFactory>(),
                           std::make_shared<t::JoinConverter>()};
};

const v::IVectorDataItemWithCategory* withCategory(const v::VectorDataItemPtr& item) {
    return dynamic_cast<const v::IVectorDataItemWithCategory*>(item->mainInfo().get());
}

TEST(MarkFactory, CreateMarkBuildsItemAndHashRecipe) {
    Fixture f;
    const auto geom = point(0x1122334455667788ULL);
    const auto item = f.factory.createMark(geom, q("name"), q("desc"), f.pointCat, f.pointApp);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->name(), q("name"));
    EXPECT_EQ(item->desc(), q("desc"));
    EXPECT_EQ(item->geometry(), geom);
    EXPECT_EQ(item->appearance(), f.pointApp);
    ASSERT_NE(withCategory(item), nullptr);
    EXPECT_EQ(withCategory(item)->category(), f.pointCat);
    EXPECT_EQ(item->hintText(), q("name|desc"));

    const std::string n = t::utf16LeBytes(q("name"));
    const std::string d = t::utf16LeBytes(q("desc"));
    const HashValue mainHash =
        t::hashChainBytes(cg::core::cityHash64(n.data(), n.size()), d.data(), d.size());
    EXPECT_EQ(item->mainInfo()->hash(), mainHash);
    EXPECT_EQ(item->hash(), t::hashChainU64(t::hashChainU64(geom->hash(), mainHash), f.pointApp->hash()));
}

TEST(MarkFactory, NullInputsGiveNull) { // legacy: Assert in debug, access violation in release
    Fixture f;
    const auto src = f.factory.createMark(point(1), q("n"), q("d"), f.pointCat, f.pointApp);
    EXPECT_EQ(f.factory.createMark(nullptr, q("n"), q("d"), f.pointCat, f.pointApp), nullptr);
    EXPECT_EQ(f.factory.createMark(point(1), q("n"), q("d"), f.pointCat, nullptr), nullptr);
    EXPECT_EQ(f.factory.createNewMark(nullptr, q("n"), q("d")), nullptr);
    EXPECT_EQ(f.factory.modifyGeometry(nullptr, point(2)), nullptr);
    EXPECT_EQ(f.factory.modifyName(nullptr, q("n"), q("d")), nullptr);
    EXPECT_EQ(f.factory.prepareMark(nullptr, q("n"),
                                    std::make_shared<v::ImportLineParams>(f.lineApp, true, true), nullptr),
              nullptr);
    // params built over a null appearance have null facets
    EXPECT_EQ(f.factory.prepareMark(
                  src, q("n"),
                  std::make_shared<v::ImportPointParams>(nullptr, false, false, false, false, false),
                  nullptr),
              nullptr);
    EXPECT_EQ(f.factory.prepareMark(src, q("n"), std::make_shared<v::ImportLineParams>(nullptr, false, false),
                                    nullptr),
              nullptr);
    EXPECT_EQ(f.factory.prepareMark(
                  src, q("n"), std::make_shared<v::ImportPolyParams>(nullptr, false, false, false), nullptr),
              nullptr);
    // null params: nil, as legacy (a defined result, not a guard)
    EXPECT_EQ(f.factory.prepareMark(src, q("n"), nullptr, f.pointCat), nullptr);
    EXPECT_EQ(f.factory.config(), f.config);
    EXPECT_EQ(f.factory.markPictureList(), nullptr);
}

TEST(MarkFactory, ModifyGeometryOfAnItemWithoutCategoryInfoGivesNullCategory) {
    Fixture f;
    const auto plain = std::make_shared<v::VectorDataItem>(
        1, f.pointApp,
        std::make_shared<v::VectorDataItemMainInfo>(1, std::make_shared<t::JoinConverter>(), q("n"), q("d")),
        point(1));
    const auto out = f.factory.modifyGeometry(plain, point(2));
    ASSERT_NE(out, nullptr);
    ASSERT_NE(withCategory(out), nullptr);
    EXPECT_EQ(withCategory(out)->category(), nullptr);
}

TEST(MarkFactory, CreateNewMarkTakesTheDefaultTemplateOfTheGeometryKind) {
    Fixture f;
    const auto p = f.factory.createNewMark(point(1), QString(), q("d"));
    EXPECT_EQ(p->name(), q("Point1"));
    EXPECT_EQ(p->appearance(), f.pointApp);
    EXPECT_EQ(withCategory(p)->category(), f.pointCat);
    const auto l = f.factory.createNewMark(std::make_shared<FakeLine>(2), QString(), QString());
    EXPECT_EQ(l->name(), q("Line1"));
    EXPECT_EQ(withCategory(l)->category(), f.lineCat);
    const auto y = f.factory.createNewMark(std::make_shared<FakePoly>(3), QString(), QString());
    EXPECT_EQ(y->name(), q("Poly1"));
    EXPECT_EQ(y->appearance(), f.polyApp);
    // A given name does not touch the generator; a given template wins over the geometry kind.
    EXPECT_EQ(f.factory.createNewMark(point(1), q("mine"), QString())->name(), q("mine"));
    EXPECT_EQ(f.factory.createNewMark(point(1), QString(), QString())->name(), q("Point2"));
    const auto byTemplate = f.factory.createNewMark(point(1), QString(), QString(),
                                                    f.config->lineTemplateConfig()->defaultTemplate());
    EXPECT_EQ(byTemplate->name(), q("Line2"));
    EXPECT_EQ(byTemplate->appearance(), f.lineApp);
}

TEST(MarkFactory, CreateNewMarkOfAMultiPointWithoutTemplateGivesNull) { // legacy: access violation
    Fixture f;
    const auto multi = std::make_shared<g::GeometryLonLatMultiPoint>(
        1, g::LonLatRect{},
        std::make_shared<const std::vector<g::LonLat>>(std::vector<g::LonLat>{{1.0, 2.0}}));
    EXPECT_EQ(f.factory.createNewMark(multi, QString(), QString()), nullptr);
}

TEST(MarkFactory, ModifyAndReplaceKeepTheRest) {
    Fixture f;
    const auto src = f.factory.createMark(point(1), q("n"), q("d"), f.pointCat, f.pointApp);
    const auto geom = point(2);
    const auto g2 = f.factory.modifyGeometry(src, geom);
    EXPECT_EQ(g2->geometry(), geom);
    EXPECT_EQ(g2->name(), q("n"));
    EXPECT_EQ(withCategory(g2)->category(), f.pointCat);
    EXPECT_EQ(g2->appearance(), f.pointApp);

    const auto n2 = f.factory.modifyName(src, QString(), QString()); // empty keeps the old ones
    EXPECT_EQ(n2->name(), q("n"));
    EXPECT_EQ(n2->desc(), q("d"));
    EXPECT_EQ(n2->geometry(), src->geometry());
    const auto n3 = f.factory.modifyName(src, q("x"), q("y"));
    EXPECT_EQ(n3->name(), q("x"));
    EXPECT_EQ(n3->desc(), q("y"));

    const auto r = f.factory.replaceCategory(src, f.lineCat);
    EXPECT_EQ(withCategory(r)->category(), f.lineCat);
    EXPECT_EQ(r->name(), q("n"));
    EXPECT_EQ(f.factory.replaceCategory(nullptr, f.lineCat), nullptr);
}

TEST(MarkFactory, PrepareMarkPointForceFlagsChooseParamsOrSource) {
    Fixture f;
    const auto src = f.factory.createMark(point(1), q("n"), q("d"), f.pointCat, f.pointApp);
    // params carry a different appearance: text 0xFF0000A1/A2, font 20, pic "b.png", marker 48
    const auto paramsApp =
        kAppearances.createPointAppearance(0xFF0000A1U, 0xFF0000A2U, 20, q("b.png"), nullptr, 48);
    struct Row {
        bool color, bg, font, marker, pic;
    };
    for (const Row r : {Row{false, false, false, false, false}, Row{true, true, true, true, true},
                        Row{true, false, true, false, true}}) {
        const auto params =
            std::make_shared<v::ImportPointParams>(paramsApp, r.color, r.bg, r.font, r.marker, r.pic);
        const auto out = f.factory.prepareMark(src, q("new"), params, f.lineCat);
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(out->name(), q("new"));
        EXPECT_EQ(out->desc(), q("d"));
        EXPECT_EQ(out->geometry(), src->geometry());
        EXPECT_EQ(withCategory(out)->category(), f.lineCat);
        const auto* cap = dynamic_cast<const v::IAppearancePointCaption*>(out->appearance().get());
        const auto* icon = dynamic_cast<const v::IAppearancePointIcon*>(out->appearance().get());
        ASSERT_TRUE(cap != nullptr && icon != nullptr);
        EXPECT_EQ(cap->textColor(), r.color ? 0xFF0000A1U : 0xFF000001U);
        EXPECT_EQ(cap->textBgColor(), r.bg ? 0xFF0000A2U : 0xFF000002U);
        EXPECT_EQ(cap->fontSize(), r.font ? 20 : 11);
        EXPECT_EQ(icon->markerSize(), r.marker ? 48 : 32);
        EXPECT_EQ(icon->picName(), r.pic ? q("b.png") : q("a.png"));
    }
}

TEST(MarkFactory, PrepareMarkLineAndPolygon) {
    Fixture f;
    const auto lineSrc =
        f.factory.createMark(std::make_shared<FakeLine>(1), q("n"), q("d"), nullptr, f.lineApp);
    const auto lineParamsApp = kAppearances.createLineAppearance(0xFF0000B1U, 9);
    // ImportLineParams stores isForceLineWidth into isForceLineColor too (L-130): (true,false) forces
    // nothing, (false,true) forces both; the first argument is ignored.
    const auto keep = f.factory.prepareMark(
        lineSrc, q("n"), std::make_shared<v::ImportLineParams>(lineParamsApp, true, false), nullptr);
    const auto* kept = dynamic_cast<const v::IAppearanceLine*>(keep->appearance().get());
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->lineColor(), 0xFF000003U);
    EXPECT_EQ(kept->lineWidth(), 2);
    const auto lineOut = f.factory.prepareMark(
        lineSrc, q("n"), std::make_shared<v::ImportLineParams>(lineParamsApp, false, true), nullptr);
    const auto* line = dynamic_cast<const v::IAppearanceLine*>(lineOut->appearance().get());
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->lineColor(), 0xFF0000B1U);
    EXPECT_EQ(line->lineWidth(), 9);
    EXPECT_EQ(withCategory(lineOut)->category(), nullptr);

    const auto polySrc =
        f.factory.createMark(std::make_shared<FakePoly>(1), q("n"), q("d"), nullptr, f.polyApp);
    const auto polyParamsApp = kAppearances.createPolygonAppearance(0xFF0000C1U, 7, 0xFF0000C2U);
    const auto yp = std::make_shared<v::ImportPolyParams>(polyParamsApp, true, true, false);
    const auto polyOut = f.factory.prepareMark(polySrc, q("n"), yp, nullptr);
    const auto* border = dynamic_cast<const v::IAppearancePolygonBorder*>(polyOut->appearance().get());
    const auto* fill = dynamic_cast<const v::IAppearancePolygonFill*>(polyOut->appearance().get());
    ASSERT_TRUE(border != nullptr && fill != nullptr);
    EXPECT_EQ(border->lineColor(), 0xFF0000C1U);
    EXPECT_EQ(border->lineWidth(), 7);
    EXPECT_EQ(fill->fillColor(), 0xFF000005U);
}

} // namespace
