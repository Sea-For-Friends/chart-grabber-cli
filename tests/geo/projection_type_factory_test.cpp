// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-001, FEAT-GEO-003. New coverage only (no legacy DUnit suite for the factory);
// docs/specs/geo.md §11.2 projection_type_factory_test.cpp (18 new), §2.4.
#include "cg/geo/projection_type_factory.h"

#include <QHash>
#include <QString>

#include <gtest/gtest.h>

using cg::core::IConfigProvider;
using cg::core::IConfigProviderPtr;
using cg::geo::DatumFactory;
using cg::geo::HashSeed;
using cg::geo::ProjectionTypeFactory;
using cg::geo::UnexpectedProjectionType;

namespace {

// A minimal in-memory cg::core::IConfigProvider: values are stored as strings, matching how a
// real ini-backed provider works, so readInt()/readFloat() go through the same string parsing
// (cg/core/ini_value_codec.h) as production. Only value() is ever queried by byConfig(); the
// other four pure virtuals are unused here.
class FakeConfig : public IConfigProvider {
public:
    FakeConfig& setInt(const QString& key, qint32 value) {
        values_.insert(key, QString::number(value));
        return *this;
    }
    FakeConfig& setFloat(const QString& key, double value) {
        values_.insert(key, QString::number(value, 'f', 10));
        return *this;
    }

    [[nodiscard]] IConfigProviderPtr subItem(QStringView) const override { return nullptr; }
    [[nodiscard]] QVariant value(QStringView id) const override {
        const auto it = values_.constFind(id.toString());
        return it == values_.constEnd() ? QVariant() : QVariant(*it); // invalid QVariant == absent
    }
    [[nodiscard]] QByteArray binary(QStringView) const override { return {}; }
    [[nodiscard]] QStringList subItemNames() const override { return {}; }
    [[nodiscard]] QStringList valueNames() const override { return values_.keys(); }

private:
    QHash<QString, QString> values_;
};

ProjectionTypeFactory makeFactory() {
    return ProjectionTypeFactory{HashSeed{}, std::make_shared<const DatumFactory>(HashSeed{})};
}

} // namespace

TEST(ProjectionTypeFactory, ByCode3857) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byCode(3857)->projectionEpsg(), 3857);
}
TEST(ProjectionTypeFactory, ByCode3785AliasesGoogle) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byCode(3785), factory.byCode(3857));
}
TEST(ProjectionTypeFactory, ByCode53004) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byCode(53004)->projectionEpsg(), 53004);
}
TEST(ProjectionTypeFactory, ByCode3395) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byCode(3395)->projectionEpsg(), 3395);
}
TEST(ProjectionTypeFactory, ByCode4326) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byCode(4326)->projectionEpsg(), 4326);
}
TEST(ProjectionTypeFactory, ByCodeUnknownThrows) {
    auto factory = makeFactory();
    EXPECT_THROW((void)factory.byCode(9999), UnexpectedProjectionType);
}

TEST(ProjectionTypeFactory, ByConfigNullUsesDefaults) {
    auto factory = makeFactory();
    EXPECT_EQ(factory.byConfig(nullptr)->projectionEpsg(), 3857); // projection=1, radiusA=6378137
}

TEST(ProjectionTypeFactory, ByConfigEpsgWins) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("EPSG"), 4326).setInt(QStringLiteral("projection"), 1);
    EXPECT_EQ(factory.byConfig(&cfg)->projectionEpsg(), 4326);
}

TEST(ProjectionTypeFactory, ByConfigUnknownEpsgFallsThroughWithoutThrowing) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("EPSG"), 12345)
        .setInt(QStringLiteral("projection"), 1)
        .setFloat(QStringLiteral("sradiusa"), 6378137.0);
    EXPECT_NO_THROW({
        const auto type = factory.byConfig(&cfg);
        EXPECT_EQ(type->projectionEpsg(), 3857); // falls through to the radius inference
    });
}

TEST(ProjectionTypeFactory, ByConfigUnknownProjectionThrows) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 7);
    EXPECT_THROW((void)factory.byConfig(&cfg), UnexpectedProjectionType);
}

TEST(ProjectionTypeFactory, ByConfigSradiusbDefaultsToSradiusa) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 2)
        .setFloat(QStringLiteral("sradiusa"), 6378137.0); // no sradiusb
    const auto type = factory.byConfig(&cfg);
    // sradiusb defaults to sradiusa (6378137), giving e = 0 -- a sphere in an ellipsoid formula.
    EXPECT_NEAR(type->lonLat2Relative({10.0, 10.0}).y(),
                factory.byCode(3857)->lonLat2Relative({10.0, 10.0}).y() * 1.0, 0.05);
    EXPECT_EQ(type->projectionEpsg(), 0);
}

TEST(ProjectionTypeFactory, ByConfigInfersGoogleFromRadius) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 1).setFloat(QStringLiteral("sradiusa"), 6378137.5);
    EXPECT_EQ(factory.byConfig(&cfg)->projectionEpsg(), 3857);
}
TEST(ProjectionTypeFactory, ByConfigInfers53004FromRadius) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 1).setFloat(QStringLiteral("sradiusa"), 6371000.5);
    EXPECT_EQ(factory.byConfig(&cfg)->projectionEpsg(), 53004);
}
TEST(ProjectionTypeFactory, ByConfigInfersYandex) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 2)
        .setFloat(QStringLiteral("sradiusa"), 6378137.2)
        .setFloat(QStringLiteral("sradiusb"), 6356752.1);
    EXPECT_EQ(factory.byConfig(&cfg)->projectionEpsg(), 3395);
}
TEST(ProjectionTypeFactory, ByConfigInfersGeographic) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 3)
        .setFloat(QStringLiteral("sradiusa"), 6378137.2)
        .setFloat(QStringLiteral("sradiusb"), 6356752.1);
    EXPECT_EQ(factory.byConfig(&cfg)->projectionEpsg(), 4326);
}

TEST(ProjectionTypeFactory, ByConfigToleranceIsOneMetre) {
    auto factory = makeFactory();
    FakeConfig within;
    within.setInt(QStringLiteral("projection"), 1).setFloat(QStringLiteral("sradiusa"), 6378137.9);
    EXPECT_EQ(factory.byConfig(&within)->projectionEpsg(), 3857);

    FakeConfig outside;
    outside.setInt(QStringLiteral("projection"), 1).setFloat(QStringLiteral("sradiusa"), 6378138.1);
    EXPECT_EQ(factory.byConfig(&outside)->projectionEpsg(), 0); // ad-hoc, tolerance exceeded
}

TEST(ProjectionTypeFactory, ByConfigAdHocSphereHasEpsgZero) {
    auto factory = makeFactory();
    FakeConfig cfg;
    cfg.setInt(QStringLiteral("projection"), 1).setFloat(QStringLiteral("sradiusa"), 3396190.0); // Mars
    const auto type = factory.byConfig(&cfg);
    EXPECT_EQ(type->projectionEpsg(), 0);
    EXPECT_EQ(type->datum()->radiusA(), 3396190.0);
}

TEST(ProjectionTypeFactory, ByConfigProjection1IgnoresSradiusb) {
    auto factory = makeFactory();
    FakeConfig cfg;
    // projection=1 passes radiusA as BOTH radii; a wild sradiusb must have no effect at all.
    cfg.setInt(QStringLiteral("projection"), 1)
        .setFloat(QStringLiteral("sradiusa"), 6378137.0)
        .setFloat(QStringLiteral("sradiusb"), 1.0);
    EXPECT_EQ(factory.byConfig(&cfg)->projectionEpsg(), 3857);
}
