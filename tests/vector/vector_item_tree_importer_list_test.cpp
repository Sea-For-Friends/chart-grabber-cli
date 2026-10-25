// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (the T-0141 row assigns none). Ports u_VectorItemTreeImporterList.pas
// (GetImporterByExt :123-147, GetCount :114-120, GetItem :149-154) and the item class of
// i_VectorItemTreeImporterList.pas; docs/specs/vector.md §2.11.
#include "cg/vector/vector_item_tree_importer_list.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

using namespace cg::vector;

namespace {

class FakeImporter final : public IVectorItemTreeImporter {
public:
    [[nodiscard]] cg::core::Result<VectorItemTreePtr> processImport(std::stop_token, const QString&,
                                                                    const ImportConfigPtr&) const override {
        return VectorItemTreePtr{};
    }
};

VectorItemTreeImporterListItemPtr makeItem(const VectorItemTreeImporterPtr& imp, QStringList exts,
                                           const QString& name) {
    return std::make_shared<VectorItemTreeImporterListItem>(imp, std::move(exts), name);
}

} // namespace

TEST(ImporterListByExt, DataDriven) {
    auto kml = std::make_shared<FakeImporter>();
    auto gpxA = std::make_shared<FakeImporter>();
    auto gpxB = std::make_shared<FakeImporter>(); // lists "gpx" too: the last item wins
    auto upper = std::make_shared<FakeImporter>();
    auto cyrUpper = std::make_shared<FakeImporter>();
    auto cyrLower = std::make_shared<FakeImporter>();
    VectorItemTreeImporterListStatic list({
        makeItem(kml, {QStringLiteral("kml"), QStringLiteral("kmz")}, QStringLiteral("k")),
        makeItem(gpxA, {QStringLiteral("gpx")}, QStringLiteral("a")),
        makeItem(upper, {QStringLiteral("PLT")}, QStringLiteral("u")),
        makeItem(cyrUpper, {QString::fromUtf8("ГПХ")}, QStringLiteral("cu")),
        makeItem(cyrLower, {QString::fromUtf8("гпх")}, QStringLiteral("cl")),
        makeItem(gpxB, {QStringLiteral("xyz"), QStringLiteral("gpx")}, QStringLiteral("b")),
    });
    struct Row {
        QString ext;
        VectorItemTreeImporterPtr expected;
    };
    const std::vector<Row> rows = {
        {QStringLiteral("kml"), kml},         {QStringLiteral(".kml"), kml},
        {QStringLiteral("KML"), kml},         {QStringLiteral(".KmL"), kml},
        {QStringLiteral("kmz"), kml}, // second extension of an item
        {QStringLiteral("gpx"), gpxB},        {QStringLiteral(".GPX"), gpxB},
        {QStringLiteral("plt"), nullptr},   // item registered as "PLT": never found
        {QStringLiteral("PLT"), nullptr},   // the argument is lower-cased, the stored value is not
        {QStringLiteral(".ГПХ"), cyrUpper}, // no fold of non-ASCII (SysUtils.LowerCase)
        {QStringLiteral(".гпх"), cyrLower},   {QStringLiteral("Гпх"), nullptr},
        {QStringLiteral("unknown"), nullptr}, {QStringLiteral("..kml"), nullptr}, // only one dot is dropped
        {QStringLiteral(""), nullptr},                                            // D row: empty ext
        {QStringLiteral("."), nullptr},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(list.importerByExt(row.ext), row.expected) << row.ext.toStdString();
    }
}

TEST(ImporterListStatic, EmptyListIsTheNilListRegression) {
    VectorItemTreeImporterListStatic list({}); // D row: legacy nil list
    EXPECT_EQ(list.count(), 0);
    EXPECT_EQ(list.importerByExt(QStringLiteral("kml")), nullptr);
    EXPECT_EQ(list.item(0), nullptr);
}

TEST(ImporterListStatic, ItemsInInsertionOrderAndOutOfRange) {
    auto i1 =
        makeItem(std::make_shared<FakeImporter>(), {QStringLiteral("b")}, QStringLiteral("second-by-name"));
    auto i0 =
        makeItem(std::make_shared<FakeImporter>(), {QStringLiteral("a")}, QStringLiteral("first-by-name"));
    VectorItemTreeImporterListStatic list({i1, i0}); // given unsorted by name
    ASSERT_EQ(list.count(), 2);
    EXPECT_EQ(list.item(0), i1);
    EXPECT_EQ(list.item(1), i0);
    EXPECT_EQ(list.item(2), nullptr);
    EXPECT_EQ(list.item(-1), nullptr);
}

TEST(ImporterListItem, ConstructorValuesAndCopy) {
    auto imp = std::make_shared<FakeImporter>();
    VectorItemTreeImporterListItem item(imp, {QStringLiteral("zz"), QStringLiteral("aa")},
                                        QStringLiteral("Имя"));
    EXPECT_EQ(item.importer(), imp);
    EXPECT_EQ(item.name(), QStringLiteral("Имя"));
    QStringList got = item.supportedExt();
    EXPECT_EQ(got, (QStringList{QStringLiteral("zz"), QStringLiteral("aa")})); // order kept
    got[0] = QStringLiteral("changed");
    got.append(QStringLiteral("extra"));
    EXPECT_EQ(item.supportedExt(), (QStringList{QStringLiteral("zz"), QStringLiteral("aa")}));
}
