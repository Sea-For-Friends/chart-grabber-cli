// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (the T-0141 row assigns none). Ports u_VectorItemTreeExporterList.pas
// (GetCount :127-131, GetItem :134-139) and the item class of i_VectorItemTreeExporterList.pas;
// docs/specs/vector.md §2.11.
#include "cg/vector/vector_item_tree_exporter_list.h"

#include <QString>

#include <gtest/gtest.h>

#include <memory>
#include <utility>

using namespace cg::vector;

namespace {

class FakeExporter final : public IVectorItemTreeExporter {
public:
    [[nodiscard]] cg::core::Result<void> processExport(std::stop_token, const QString&,
                                                       const VectorItemTreePtr&) const override {
        return {};
    }
};

class FakeConfig final : public IExportConfig {
protected:
    void doReadConfig(const cg::core::IConfigProviderPtr&) override {}
    void doWriteConfig(const cg::core::IConfigWriterPtr&) const override {}
};

VectorItemTreeExporterListItemPtr makeItem(const QString& name, ExporterListItemOptions opt = {}) {
    return std::make_shared<VectorItemTreeExporterListItem>(std::make_shared<FakeExporter>(), nullptr, opt,
                                                            QStringLiteral("e"), name);
}

} // namespace

TEST(ExporterListItem, FiveGettersAreTheConstructorValues) {
    auto exporter = std::make_shared<FakeExporter>();
    auto config = std::make_shared<FakeConfig>();
    VectorItemTreeExporterListItem item(exporter, config, ExporterListItemOption::SaveToDir,
                                        QStringLiteral(".экс"), QStringLiteral("Имя"));
    EXPECT_EQ(item.exporter(), exporter);
    EXPECT_EQ(item.config(), config);
    EXPECT_EQ(item.options(), ExporterListItemOptions(ExporterListItemOption::SaveToDir));
    EXPECT_EQ(item.defaultExt(), QStringLiteral(".экс"));
    EXPECT_EQ(item.name(), QStringLiteral("Имя"));
}

TEST(ExporterListItem, NullConfigIsNull) {
    auto item = makeItem(QStringLiteral("n"));
    EXPECT_EQ(item->config(), nullptr);
}

TEST(ExporterListItem, OptionFlags) {
    struct Row {
        ExporterListItemOptions in;
        bool file, dir, sep;
    };
    const Row rows[] = {
        {ExporterListItemOption::SaveToFile, true, false, false},
        {ExporterListItemOption::SaveToDir, false, true, false},
        {ExporterListItemOption::AllowSeparateFiles, false, false, true},
        {ExporterListItemOption::SaveToFile | ExporterListItemOption::AllowSeparateFiles, true, false, true},
        {ExporterListItemOption::SaveToDir | ExporterListItemOption::SaveToFile, true, true, false},
        {ExporterListItemOptions{}, false, false, false},
    };
    for (const auto& r : rows) {
        auto item = makeItem(QStringLiteral("o"), r.in);
        EXPECT_EQ(item->options(), r.in);
        EXPECT_EQ(item->options().testFlag(ExporterListItemOption::SaveToFile), r.file);
        EXPECT_EQ(item->options().testFlag(ExporterListItemOption::SaveToDir), r.dir);
        EXPECT_EQ(item->options().testFlag(ExporterListItemOption::AllowSeparateFiles), r.sep);
    }
}

TEST(ExporterListStatic, InsertionOrderAndOutOfRange) {
    auto b = makeItem(QStringLiteral("Б"));
    auto a = makeItem(QStringLiteral("А"));
    VectorItemTreeExporterListStatic list({b, a}); // unsorted by name
    ASSERT_EQ(list.count(), 2);
    EXPECT_EQ(list.item(0), b);
    EXPECT_EQ(list.item(1), a);
    EXPECT_EQ(list.item(2), nullptr);
    EXPECT_EQ(list.item(-1), nullptr);
}

TEST(ExporterListStatic, EmptyListIsTheNilListRegression) {
    VectorItemTreeExporterListStatic list({}); // D row: legacy nil list
    EXPECT_EQ(list.count(), 0);
    EXPECT_EQ(list.item(0), nullptr);
}
