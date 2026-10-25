// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: no FEAT id (the T-0129 row assigns none). Ports u_VectorItemTreeImporterByVectorLoader.pas;
// docs/specs/vector.md §2.10.3, §8.1 LV-10, §4.
#include "cg/vector/html_to_hint_text_converter_stuped.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_data_factory_simple.h"
#include "cg/vector/vector_item_subset.h"
#include "cg/vector/vector_item_tree_importer_by_vector_loader.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <utility>

using namespace cg::vector;
using cg::core::ErrorCode;

namespace {

// Records what the importer handed to the loader; answers with a canned result.
class RecordingLoader final : public IVectorDataLoader {
public:
    explicit RecordingLoader(cg::core::Result<VectorItemSubsetPtr> reply) : reply_(std::move(reply)) {}

    cg::core::Result<VectorItemSubsetPtr> load(const VectorLoadContext& context,
                                               const QByteArray& data) const override {
        ++calls;
        seen = context;
        seenData = data;
        return reply_;
    }

    mutable int calls = 0;
    mutable VectorLoadContext seen;
    mutable QByteArray seenData;

private:
    cg::core::Result<VectorItemSubsetPtr> reply_;
};

VectorDataItemMainInfoFactoryPtr mainInfoFactory() {
    return std::make_shared<VectorDataItemMainInfoFactory>(std::make_shared<HtmlToHintTextConverterStuped>());
}

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& bytes) {
    const QString path = dir.filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    EXPECT_EQ(file.write(bytes), bytes.size());
    return path;
}

} // namespace

TEST(VectorItemTreeImporterByVectorLoader, HandsWholeFileAndMainInfoFactoryToLoaderAndWrapsSubset) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QByteArray bytes("binary\0data", 11); // embedded NUL: the file is read as bytes
    const QString path = writeFile(dir, QStringLiteral("a.bin"), bytes);
    const auto subset = std::make_shared<VectorItemSubset>(7, std::vector<VectorDataItemPtr>{});
    const auto loader = std::make_shared<RecordingLoader>(subset);
    const auto factory = mainInfoFactory();
    const VectorItemTreeImporterByVectorLoader importer(factory, loader);

    const auto result = importer.processImport({}, path, nullptr);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(loader->calls, 1);
    EXPECT_EQ(loader->seenData, bytes);
    EXPECT_EQ(loader->seen.mainInfoFactory, factory);
    EXPECT_EQ(loader->seen.idData, nullptr);      // the file path never sets IdData
    EXPECT_EQ(loader->seen.pointParams, nullptr); // no config: no params
    EXPECT_EQ(loader->seen.lineParams, nullptr);
    EXPECT_EQ(loader->seen.polygonParams, nullptr);
    const VectorItemTreePtr& tree = *result;
    EXPECT_TRUE(tree->name().isEmpty());
    EXPECT_EQ(tree->items(), subset);
    EXPECT_EQ(tree->subTreeItemCount(), 0);
}

// LV-10 (u_VectorItemTreeImporterByVectorLoader.pas:103): the loader found nothing, the tree is
// still returned, with null items.
TEST(VectorItemTreeImporterByVectorLoader, LoaderWithNoItemsGivesTreeWithNullItems) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeFile(dir, QStringLiteral("empty.bin"), QByteArray());
    const auto loader = std::make_shared<RecordingLoader>(VectorItemSubsetPtr{});
    const VectorItemTreeImporterByVectorLoader importer(mainInfoFactory(), loader);

    const auto result = importer.processImport({}, path, nullptr);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(loader->seenData.size(), 0);
    EXPECT_EQ((*result)->items(), nullptr);
    EXPECT_EQ((*result)->subTreeItemCount(), 0);
}

TEST(VectorItemTreeImporterByVectorLoader, ConfigParamsReachTheLoaderOneEachAndNullConfigGivesNone) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeFile(dir, QStringLiteral("a.bin"), QByteArray("x"));
    const auto loader = std::make_shared<RecordingLoader>(VectorItemSubsetPtr{});
    const VectorItemTreeImporterByVectorLoader importer(mainInfoFactory(), loader);

    const auto point = std::make_shared<ImportPointParams>(nullptr, true, false, false, false, false);
    const auto line = std::make_shared<ImportLineParams>(nullptr, false, true);
    const auto poly = std::make_shared<ImportPolyParams>(nullptr, true, true, false);
    const auto config = std::make_shared<ImportConfig>(nullptr, nullptr, point, line, poly);
    ASSERT_TRUE(importer.processImport({}, path, config).has_value());
    EXPECT_EQ(loader->seen.pointParams, point);
    EXPECT_EQ(loader->seen.lineParams, line);
    EXPECT_EQ(loader->seen.polygonParams, poly);

    ASSERT_TRUE(importer.processImport({}, path, nullptr).has_value());
    EXPECT_EQ(loader->seen.pointParams, nullptr);
    EXPECT_EQ(loader->seen.lineParams, nullptr);
    EXPECT_EQ(loader->seen.polygonParams, nullptr);
}

TEST(VectorItemTreeImporterByVectorLoader, LoaderFailureIsReturnedAndNoTreeIsBuilt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = writeFile(dir, QStringLiteral("a.bin"), QByteArray("x"));
    const auto loader = std::make_shared<RecordingLoader>(
        cg::core::fail(ErrorCode::ParseError, QStringLiteral("bad line 3")));
    const VectorItemTreeImporterByVectorLoader importer(mainInfoFactory(), loader);

    const auto result = importer.processImport({}, path, nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::ParseError);
    EXPECT_EQ(result.error().what(), QStringLiteral("bad line 3"));
}

// A file that cannot be read is IoError and the loader is never called (spec §4; legacy raises
// EFOpenError from LoadFromFile, u_VectorItemTreeImporterByVectorLoader.pas:92).
TEST(VectorItemTreeImporterByVectorLoader, UnreadableFileIsIoErrorAndLoaderIsNotCalled) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto loader = std::make_shared<RecordingLoader>(VectorItemSubsetPtr{});
    const VectorItemTreeImporterByVectorLoader importer(mainInfoFactory(), loader);

    for (const QString& path : {dir.filePath(QStringLiteral("missing.kml")), dir.path()}) {
        const auto result = importer.processImport({}, path, nullptr);
        ASSERT_FALSE(result.has_value()) << path.toStdString();
        EXPECT_EQ(result.error().code(), ErrorCode::IoError) << path.toStdString();
        EXPECT_TRUE(result.error().what().contains(path)) << path.toStdString();
    }
    EXPECT_EQ(loader->calls, 0);
}
