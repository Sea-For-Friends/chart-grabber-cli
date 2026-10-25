// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports no legacy test (there is none): Src/Vector/u_VectorItemTreeMarksDb.pas (_GetConfigForExporter
// :116-139, _GetImpl :141-175, ProcessExport :177-197, ProcessImport :199-221), ticket T-0152.
// FEAT-GEO-041 contributes (the id closes with V-M2 / V-M3).
//
// Expectations are literals taken from the Pascal. The factory list, its element and its factory are fakes
// written here (recording build()); the databases are the shared fakes of tests/marks (FakeDb,
// FakeCategoryDbImpl, FakeMarkFactory, FakeMarkSystemImpl). The end-to-end test goes through the REAL factory
// list on a file in a QTemporaryDir. Not provable here: IsAddAllInRootCategory (the first argument of
// ImportCategoryParams, :122) is read by no legacy code (rtk proxy grep -a over Src) and by no port code.
#include "cg/core/read_write_state.h"
#include "cg/core/read_write_state_internal.h"
#include "cg/core/result.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_system_impl_factory.h"
#include "cg/marks/mark_system_impl_factory_changeable.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/sml/mark_system_impl_config_sml.h"
#include "cg/marks/vector_item_tree_marks_db.h"
#include "cg/vector/appearance_of_mark_factory.h"
#include "cg/vector/appearance_of_vector_item.h"
#include "cg/vector/import_config.h"
#include "cg/vector/vector_item_subset.h"
#include "cg/vector/vector_item_tree.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "mark_system_fakes.h"
#include "mark_system_rig.h"
#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace t = cg::marks::testutil;
namespace core = cg::core;
namespace g = cg::geo;

namespace {

const QString kTown = QString::fromUtf8("Город");
const QString kStreet = QString::fromUtf8("Улица");
const QString kStreetPath = QString::fromUtf8("Город\\Улица"); // the category name of the nested node
const QString kPoint = QString::fromUtf8("Точка");
const QString kLine = QString::fromUtf8("Линия");
const QString kPoly = QString::fromUtf8("Полигон");
const QString kHidden = QString::fromUtf8("Скрытая");

// A factory that records what build() was given and answers `impl`, or throws `failure` when it is set.
class RecFactory final : public m::IMarkSystemImplFactory {
public:
    std::shared_ptr<m::IMarkSystemImpl> impl;
    QString failure;
    int calls = 0;
    QString basePath;
    m::MarkSystemImplConfigStaticPtr config;
    bool stopPossible = false;
    bool stopRequested = false;

    [[nodiscard]] bool isInitializationRequired() const override { return false; }
    std::shared_ptr<m::IMarkSystemImpl> build(std::stop_token stop, const QString& base,
                                              const m::MarkSystemImplConfigStaticPtr& implConfig) override {
        ++calls;
        basePath = base;
        config = implConfig;
        stopPossible = stop.stop_possible();
        stopRequested = stop.stop_requested();
        if (!failure.isEmpty()) {
            throw std::runtime_error(failure.toStdString());
        }
        return impl;
    }
};

class RecElement final : public m::IMarkSystemImplFactoryListElement {
public:
    RecElement(QUuid guid, std::shared_ptr<m::IMarkSystemImplFactory> factory)
        : guid_(guid), factory_(std::move(factory)) {}
    [[nodiscard]] QUuid guid() const override { return guid_; }
    [[nodiscard]] QString caption() const override { return {}; }
    [[nodiscard]] std::shared_ptr<m::IMarkSystemImplFactory> factory() const override { return factory_; }

private:
    QUuid guid_;
    std::shared_ptr<m::IMarkSystemImplFactory> factory_;
};

// One element per GUID given to add(); counts the lookups.
class RecList final : public m::IMarkSystemImplFactoryListStatic {
public:
    std::vector<m::MarkSystemImplFactoryListElementPtr> elements;
    mutable int gets = 0;

    [[nodiscard]] std::vector<QUuid> guidEnum() const override { return {}; }
    [[nodiscard]] m::MarkSystemImplFactoryListElementPtr get(const QUuid& guid) const override {
        ++gets;
        for (const auto& e : elements) {
            if (e->guid() == guid) {
                return e;
            }
        }
        return nullptr;
    }
};

// A state whose snapshot is fixed (read and write access independent of each other), or null.
class FixedState final : public core::IReadWriteStateChangeble {
public:
    explicit FixedState(core::ReadWriteStateStaticPtr snapshot) : snapshot_(std::move(snapshot)) {}
    [[nodiscard]] core::ReadWriteStateStaticPtr getStatic() const override { return snapshot_; }

private:
    core::ReadWriteStateStaticPtr snapshot_;
};

core::ReadWriteStateChangeablePtr state(bool read, bool write) {
    return std::make_shared<FixedState>(std::make_shared<const core::ReadWriteStateStatic>(
        core::ReadWriteStateStatic{.readAccess = read, .writeAccess = write}));
}

// The mark factory that keeps the params and category of every prepareMark call (the config literals).
class ParamsRecorder final : public m::IMarkFactory {
public:
    struct Call {
        QString name;
        std::shared_ptr<const v::IImportMarkParams> params;
        QString category;
    };
    mutable std::vector<Call> calls;

    [[nodiscard]] v::VectorDataItemPtr prepareMark(const v::VectorDataItemPtr& item, const QString& name,
                                                   const std::shared_ptr<const v::IImportMarkParams>& params,
                                                   const v::CategoryPtr& category) const override {
        calls.push_back({name, params, category ? category->name() : QStringLiteral("<nil>")});
        return t::dataItem(name, item->geometry());
    }
    [[nodiscard]] v::VectorDataItemPtr createNewMark(const g::GeometryLonLatPtr&, const QString&,
                                                     const QString&,
                                                     const m::MarkTemplatePtr&) const override {
        throw std::logic_error("not used");
    }
    [[nodiscard]] v::VectorDataItemPtr replaceCategory(const v::VectorDataItemPtr&,
                                                       const v::CategoryPtr&) const override {
        throw std::logic_error("not used");
    }
    [[nodiscard]] v::VectorDataItemPtr createMark(const g::GeometryLonLatPtr&, const QString&, const QString&,
                                                  const v::CategoryPtr&,
                                                  const v::AppearancePtr&) const override {
        throw std::logic_error("not used");
    }
    [[nodiscard]] v::VectorDataItemPtr modifyGeometry(const v::VectorDataItemPtr&,
                                                      const g::GeometryLonLatPtr&) const override {
        throw std::logic_error("not used");
    }
    [[nodiscard]] v::VectorDataItemPtr modifyName(const v::VectorDataItemPtr&, const QString&,
                                                  const QString&) const override {
        throw std::logic_error("not used");
    }
    [[nodiscard]] v::MarkPictureListPtr markPictureList() const override {
        throw std::logic_error("not used");
    }
    [[nodiscard]] std::shared_ptr<m::IMarkFactoryConfig> config() const override {
        throw std::logic_error("not used");
    }
};

// A category database whose list is always null.
class NoListCategoryDb final : public m::IMarkCategoryDBImpl {
public:
    [[nodiscard]] m::MarkCategoryPtr firstCategoryByName(const QString&) const override { return nullptr; }
    [[nodiscard]] int categoryByNameCount(const QString&) const override { return 0; }
    m::MarkCategoryPtr updateCategory(const m::MarkCategoryPtr&, const m::MarkCategoryPtr& n) override {
        return n;
    }
    m::MarkCategoryListPtr updateCategoryList(const m::MarkCategoryListPtr&,
                                              const m::MarkCategoryListPtr& n) override {
        return n;
    }
    [[nodiscard]] bool isCategoryFromThisDb(const v::CategoryPtr&) const override { return false; }
    [[nodiscard]] m::MarkCategoryListPtr categoriesList() const override { return nullptr; }
    void setAllCategoriesVisible(bool) override {}
    [[nodiscard]] core::Notifier& changeNotifier() override { return changed; }
    core::Notifier changed;
};

v::VectorItemTreePtr node(const QString& name, std::vector<v::VectorDataItemPtr> items = {},
                          std::vector<v::VectorItemTreePtr> children = {}) {
    v::VectorItemSubsetPtr subset;
    if (!items.empty()) {
        subset = std::make_shared<const v::VectorItemSubset>(0, std::move(items));
    }
    return std::make_shared<const v::VectorItemTree>(name, subset, std::move(children));
}
v::VectorDataItemPtr pt(const QString& name) {
    return t::dataItem(name, t::point());
}
v::VectorDataItemPtr ln(const QString& name) {
    return t::dataItem(name, t::lineGeometry());
}
v::VectorDataItemPtr pg(const QString& name) {
    return t::dataItem(name, t::polygonGeometry());
}

// "name[item,item](child)(child)" in tree order; a null subtree is "(null)".
std::string describe(const v::VectorItemTreePtr& tree) {
    if (!tree) {
        return "(null)";
    }
    std::string out = tree->name().toStdString() + "[";
    if (const auto items = tree->items()) {
        for (int i = 0; i < items->count(); ++i) {
            if (i > 0) {
                out += ',';
            }
            out += items->item(i)->name().toStdString();
        }
    }
    out += "]";
    for (int i = 0; i < tree->subTreeItemCount(); ++i) {
        out += '(';
        out += describe(tree->subTreeItem(i));
        out += ')';
    }
    return out;
}

std::size_t pointCount(const v::VectorDataItemPtr& mark) {
    const g::GeometryLonLatPtr geometry = mark->geometry();
    if (const auto* line = dynamic_cast<const g::IGeometryLonLatSingleLine*>(geometry.get())) {
        return line->points().size();
    }
    if (const auto* poly = dynamic_cast<const g::IGeometryLonLatSinglePolygon*>(geometry.get())) {
        return poly->outerBorder()->points().size();
    }
    return dynamic_cast<const g::IGeometryLonLatPoint*>(geometry.get()) != nullptr ? 1 : 0;
}

// One object under test with the fake list, factory, impl and databases behind it.
struct Rig {
    t::CallLog log;
    std::shared_ptr<RecFactory> factory = std::make_shared<RecFactory>();
    std::shared_ptr<RecList> list = std::make_shared<RecList>();
    std::shared_ptr<t::FakeMarkSystemImpl> impl = std::make_shared<t::FakeMarkSystemImpl>();
    std::shared_ptr<t::FakeDb> markDb = std::make_shared<t::FakeDb>();
    std::shared_ptr<t::FakeCategoryDbImpl> categoryDb = std::make_shared<t::FakeCategoryDbImpl>();
    std::shared_ptr<t::FakeMarkFactory> markFactory = std::make_shared<t::FakeMarkFactory>();
    std::shared_ptr<t::FakeCategoryFactory> categoryFactory = std::make_shared<t::FakeCategoryFactory>();
    v::AppearanceOfMarkFactoryPtr appearances = std::make_shared<v::AppearanceOfMarkFactory>();

    // The impl has both accesses and both databases; the factory answers it; the list holds `guid`.
    explicit Rig(const QUuid& guid = m::cORMSQLiteMarksDbGUID, bool registered = true) {
        categoryDb->log = &log;
        categoryDb->storesLists = true;
        markDb->log = &log;
        markDb->byName = true;
        markFactory->log = &log;
        impl->markDatabase = markDb;
        impl->db = categoryDb;
        impl->stateObject = state(true, true);
        factory->impl = impl;
        if (registered) {
            list->elements.push_back(std::make_shared<RecElement>(guid, factory));
        }
    }

    [[nodiscard]] m::VectorItemTreeMarksDb make(const QUuid& guid) const {
        return m::VectorItemTreeMarksDb(guid, markFactory, categoryFactory, appearances, list);
    }
};

// The factory list entry exists but the GUID asked for is a different one.
const QUuid kUnknownGuid = m::cORMMongoDbMarksDbGUID;

} // namespace

// Proof 1: _GetImpl builds the config by the GUID (:153-160); the file name, read-only flag, base path
// (ExtractFilePath, :172) and the caller's stop token reach build().
TEST(VectorItemTreeMarksDb, ConfigByGuidAndMethod) {
    struct Row {
        QUuid guid;
        bool isExport;
        bool readOnly;
    };
    const QString file = QStringLiteral("/tmp/x/marks.sml");
    for (const Row& row :
         {Row{m::cSMLMarksDbGUID, true, false}, Row{m::cSMLMarksDbGUID, false, true},
          Row{m::cORMSQLiteMarksDbGUID, true, false}, Row{m::cORMSQLiteMarksDbGUID, false, true}}) {
        SCOPED_TRACE(std::string(row.isExport ? "export " : "import ") +
                     (row.guid == m::cSMLMarksDbGUID ? "SML" : "ORM"));
        Rig rig(row.guid);
        const auto db = rig.make(row.guid);
        if (row.isExport) {
            ASSERT_TRUE(db.processExport({}, file, node(QString())));
        } else {
            ASSERT_TRUE(db.processImport({}, file, nullptr));
        }
        ASSERT_EQ(rig.factory->calls, 1);
        ASSERT_TRUE(rig.factory->config);
        EXPECT_EQ(rig.factory->config->fileName(), file);
        EXPECT_EQ(rig.factory->config->isReadOnly(), row.readOnly);
        const bool isSml =
            dynamic_cast<const m::IMarkSystemImplConfigSML*>(rig.factory->config.get()) != nullptr;
        const auto* orm = dynamic_cast<const m::IMarkSystemImplConfigORM*>(rig.factory->config.get());
        EXPECT_EQ(isSml, row.guid == m::cSMLMarksDbGUID);
        EXPECT_EQ(orm != nullptr, row.guid == m::cORMSQLiteMarksDbGUID);
        if (orm != nullptr) {
            EXPECT_EQ(orm->userName(), QString());
            EXPECT_EQ(orm->password(), QString());
            EXPECT_EQ(orm->passwordPlain(), QString());
        }
    }
}

TEST(VectorItemTreeMarksDb, BasePathIsExtractFilePath) {
    // ExtractFilePath: up to and including the last delimiter; none gives "" (:172). The port's delimiters
    // are '/' and '\' (D-123), the later one of the two counts.
    const std::vector<std::pair<QString, QString>> rows{
        {QStringLiteral("/tmp/x/marks.sml"), QStringLiteral("/tmp/x/")},
        {QStringLiteral("C:\\d\\m.db3"), QStringLiteral("C:\\d\\")},
        {QStringLiteral("marks.sml"), QString()},
        {QStringLiteral("a/b\\c.db3"), QStringLiteral("a/b\\")},
        {QStringLiteral("a\\b/c.db3"), QStringLiteral("a\\b/")},
        {QString::fromUtf8("/Город/м.db3"), QString::fromUtf8("/Город/")},
    };
    for (const auto& [fileName, expected] : rows) {
        SCOPED_TRACE(fileName.toStdString());
        Rig rig;
        const auto db = rig.make(m::cORMSQLiteMarksDbGUID);
        ASSERT_TRUE(db.processExport({}, fileName, node(QString())));
        EXPECT_EQ(rig.factory->basePath, expected);
        ASSERT_TRUE(db.processImport({}, fileName, nullptr));
        EXPECT_EQ(rig.factory->basePath, expected);
    }
}

TEST(VectorItemTreeMarksDb, StopTokenIsTheCallers) {
    std::stop_source source;
    {
        Rig rig;
        ASSERT_TRUE(rig.make(m::cORMSQLiteMarksDbGUID)
                        .processExport(source.get_token(), QStringLiteral("a/m"), node(QString())));
        EXPECT_TRUE(rig.factory->stopPossible);
        EXPECT_FALSE(rig.factory->stopRequested);
    }
    source.request_stop();
    Rig exportRig;
    ASSERT_TRUE(exportRig.make(m::cORMSQLiteMarksDbGUID)
                    .processExport(source.get_token(), QStringLiteral("a/m"), node(QString())));
    EXPECT_TRUE(exportRig.factory->stopRequested);
    Rig importRig;
    ASSERT_TRUE(importRig.make(m::cORMSQLiteMarksDbGUID)
                    .processImport(source.get_token(), QStringLiteral("a/m"), nullptr));
    EXPECT_TRUE(importRig.factory->stopRequested);
}

// Proof 2: unknown GUID -> nothing, null tree, the list is NOT asked (config first, :153-160); a known GUID
// without an element -> the list is asked once and build is not called (:162-166).
TEST(VectorItemTreeMarksDb, UnknownGuidDoesNotAskTheList) {
    Rig rig(kUnknownGuid);
    const auto db = rig.make(kUnknownGuid);
    EXPECT_TRUE(db.processExport({}, QStringLiteral("/d/m.db3"), node(kTown, {pt(kPoint)})));
    const auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_FALSE(*imported);
    EXPECT_EQ(rig.list->gets, 0);
    EXPECT_EQ(rig.factory->calls, 0);
    EXPECT_EQ(rig.markDb->calls, 0);
}

TEST(VectorItemTreeMarksDb, KnownGuidWithoutElementDoesNotBuild) {
    Rig rig(m::cORMSQLiteMarksDbGUID, false);
    const auto db = rig.make(m::cORMSQLiteMarksDbGUID);
    EXPECT_TRUE(db.processExport({}, QStringLiteral("/d/m.db3"), node(kTown, {pt(kPoint)})));
    const auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_FALSE(*imported);
    EXPECT_EQ(rig.list->gets, 2);
    EXPECT_EQ(rig.factory->calls, 0);
    EXPECT_EQ(rig.markDb->calls, 0);
}

// Proof 3: build() answers null -> nothing / null tree; build() throws -> a failure with the message.
TEST(VectorItemTreeMarksDb, BuildReturningNullDoesNothing) {
    Rig rig;
    rig.factory->impl = nullptr;
    const auto db = rig.make(m::cORMSQLiteMarksDbGUID);
    EXPECT_TRUE(db.processExport({}, QStringLiteral("/d/m.db3"), node(kTown, {pt(kPoint)})));
    const auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_FALSE(*imported);
    EXPECT_EQ(rig.factory->calls, 2);
    EXPECT_EQ(rig.markDb->calls, 0);
}

TEST(VectorItemTreeMarksDb, BuildThrowingBecomesAFailureWithTheMessage) {
    Rig rig;
    rig.factory->failure = QString::fromUtf8("не открыть: диск");
    const auto db = rig.make(m::cORMSQLiteMarksDbGUID);
    const auto exported = db.processExport({}, QStringLiteral("/d/m.db3"), node(kTown, {pt(kPoint)}));
    ASSERT_FALSE(exported);
    EXPECT_EQ(exported.error().what(), QString::fromUtf8("не открыть: диск"));
    const auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_FALSE(imported);
    EXPECT_EQ(imported.error().what(), QString::fromUtf8("не открыть: диск"));
    EXPECT_EQ(rig.markDb->calls, 0);
}

// Nothing else is caught (finding d): an exception of importItemsTree leaves processExport. A null tree is
// refused by it with std::invalid_argument (D-95 widening).
TEST(VectorItemTreeMarksDb, ExceptionOfTheImportHelperPropagates) {
    Rig rig;
    EXPECT_THROW(std::ignore =
                     rig.make(m::cORMSQLiteMarksDbGUID).processExport({}, QStringLiteral("/d/m"), nullptr),
                 std::invalid_argument);
}

// Proof 4: export needs WRITE access (:187); read access alone writes nothing, write alone writes.
TEST(VectorItemTreeMarksDb, ExportNeedsWriteAccess) {
    {
        Rig rig;
        rig.impl->stateObject = state(true, false);
        EXPECT_TRUE(rig.make(m::cORMSQLiteMarksDbGUID)
                        .processExport({}, QStringLiteral("/d/m"), node(kTown, {pt(kPoint)})));
        EXPECT_EQ(rig.markDb->calls, 0);
        EXPECT_EQ(rig.categoryDb->calls, 0);
        EXPECT_TRUE(rig.log.empty());
    }
    Rig rig;
    rig.impl->stateObject = state(false, true);
    EXPECT_TRUE(
        rig.make(m::cORMSQLiteMarksDbGUID)
            .processExport({}, QStringLiteral("/d/m"), node(QString(), {}, {node(kTown, {pt(kPoint)})})));
    EXPECT_EQ(rig.markDb->writeCalls, 1);
}

// Proof 4: the tree goes in with the category path rule of the helper: "Город", "Город\Улица", marks of the
// nested node in the nested category; categories are listed first, marks written last.
TEST(VectorItemTreeMarksDb, ExportWritesTheTreeWithPathCategories) {
    Rig rig;
    const auto tree =
        node(QString(), {}, {node(kTown, {pt(kPoint)}, {node(kStreet, {ln(kLine), pg(kPoly)})})});
    ASSERT_TRUE(rig.make(m::cORMSQLiteMarksDbGUID).processExport({}, QStringLiteral("/d/m.db3"), tree));
    const t::CallLog expected{
        "cat.first Город",
        "cat.first Город\\Улица",
        "cat.list Город",
        "cat.list Город\\Улица",
        "cat.first Город",
        "factory.prepare Точка point @Город",
        "cat.first Город\\Улица",
        "factory.prepare Линия line @Город\\Улица",
        "factory.prepare Полигон poly @Город\\Улица",
        "mark.write Точка",
        "mark.write Линия",
        "mark.write Полигон",
    };
    EXPECT_EQ(rig.log, expected);
}

// The exporter config says IsIgnoreMarkIfExistsWithSameNameInCategory = false (:122): a mark that already
// exists under that name is written again.
TEST(VectorItemTreeMarksDb, ExportDoesNotSkipAMarkThatExists) {
    Rig rig;
    rig.markDb->marks.push_back(pt(kPoint));
    ASSERT_TRUE(
        rig.make(m::cORMSQLiteMarksDbGUID)
            .processExport({}, QStringLiteral("/d/m.db3"), node(QString(), {}, {node(kTown, {pt(kPoint)})})));
    EXPECT_EQ(rig.markDb->written, std::vector<QString>{kPoint});
    EXPECT_EQ(rig.markDb->writeCalls, 1);
}

// _GetConfigForExporter literals (:116-139): the root category is "", the three params exist with zero
// appearances and no "force" flag.
TEST(VectorItemTreeMarksDb, ExporterConfigLiterals) {
    Rig rig;
    const auto recorder = std::make_shared<ParamsRecorder>();
    const m::VectorItemTreeMarksDb db(m::cORMSQLiteMarksDbGUID, recorder, rig.categoryFactory,
                                      rig.appearances, rig.list);
    // The nameless root node's items take the root category "" (:140).
    const auto tree = node(QString(), {pt(kPoint), ln(kLine), pg(kPoly)});
    ASSERT_TRUE(db.processExport({}, QStringLiteral("/d/m.db3"), tree));
    ASSERT_EQ(recorder->calls.size(), 3U);
    for (const auto& call : recorder->calls) {
        EXPECT_EQ(call.category, QString());
        ASSERT_TRUE(call.params);
    }

    const auto point = std::dynamic_pointer_cast<const v::IImportPointParams>(recorder->calls[0].params);
    ASSERT_TRUE(point);
    EXPECT_FALSE(point->isForceTextColor());
    EXPECT_FALSE(point->isForceTextBgColor());
    EXPECT_FALSE(point->isForceFontSize());
    EXPECT_FALSE(point->isForceMarkerSize());
    EXPECT_FALSE(point->isForcePicName());
    ASSERT_TRUE(point->captionAppearance());
    EXPECT_EQ(point->captionAppearance()->textColor(), 0U);
    EXPECT_EQ(point->captionAppearance()->textBgColor(), 0U);
    EXPECT_EQ(point->captionAppearance()->fontSize(), 0);
    ASSERT_TRUE(point->iconAppearance());
    EXPECT_EQ(point->iconAppearance()->picName(), QString());
    EXPECT_EQ(point->iconAppearance()->markerSize(), 0);

    const auto line = std::dynamic_pointer_cast<const v::IImportLineParams>(recorder->calls[1].params);
    ASSERT_TRUE(line);
    EXPECT_FALSE(line->isForceLineColor());
    EXPECT_FALSE(line->isForceLineWidth());
    ASSERT_TRUE(line->lineAppearance());
    EXPECT_EQ(line->lineAppearance()->lineColor(), 0U);
    EXPECT_EQ(line->lineAppearance()->lineWidth(), 0);

    const auto poly = std::dynamic_pointer_cast<const v::IImportPolyParams>(recorder->calls[2].params);
    ASSERT_TRUE(poly);
    EXPECT_FALSE(poly->isForceLineColor());
    EXPECT_FALSE(poly->isForceLineWidth());
    EXPECT_FALSE(poly->isForceFillColor());
    ASSERT_TRUE(poly->borderAppearance());
    EXPECT_EQ(poly->borderAppearance()->lineColor(), 0U);
    EXPECT_EQ(poly->borderAppearance()->lineWidth(), 0);
    ASSERT_TRUE(poly->fillAppearance());
    EXPECT_EQ(poly->fillAppearance()->fillColor(), 0U);
}

// Proof 5: import needs READ access (:211); write access alone gives a null tree.
TEST(VectorItemTreeMarksDb, ImportNeedsReadAccess) {
    Rig rig;
    rig.categoryDb->categories = {std::make_shared<const m::MarkCategory>(kTown, true, 3, 23)};
    rig.markDb->subsets.emplace_back(
        kTown, std::make_shared<const v::VectorItemSubset>(0, std::vector<v::VectorDataItemPtr>{pt(kPoint)}));
    const auto db = rig.make(m::cORMSQLiteMarksDbGUID);

    rig.impl->stateObject = state(false, true);
    auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_FALSE(*imported);
    EXPECT_EQ(rig.markDb->calls, 0);
    EXPECT_EQ(rig.categoryDb->calls, 0);

    rig.impl->stateObject = state(true, false);
    imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_EQ(describe(*imported), "[](" + std::string("Город[Точка]") + ")");
}

TEST(VectorItemTreeMarksDb, ImportWithoutCategoriesGivesANullTree) {
    Rig rig;
    const auto db = rig.make(m::cORMSQLiteMarksDbGUID);
    auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_FALSE(*imported); // the fake list is empty -> categoriesList() is null (:213)
    EXPECT_EQ(rig.categoryDb->calls, 1);
    EXPECT_EQ(rig.markDb->calls, 0);

    rig.impl->db = std::make_shared<NoListCategoryDb>();
    imported = db.processImport({}, QStringLiteral("/d/m.db3"), nullptr);
    ASSERT_TRUE(imported);
    EXPECT_FALSE(*imported);
    EXPECT_EQ(rig.markDb->calls, 0);
}

// Proof 5: the category tree becomes the mark tree (pre-order, hidden marks included, :214-215); the config
// argument is never read: null, empty and a normal one give the same tree.
TEST(VectorItemTreeMarksDb, ImportBuildsTheTreeAndIgnoresTheConfig) {
    Rig rig;
    // unsorted on purpose
    rig.categoryDb->categories = {std::make_shared<const m::MarkCategory>(kStreetPath, true, 3, 23),
                                  std::make_shared<const m::MarkCategory>(kTown, true, 3, 23)};
    rig.markDb->subsets.emplace_back(
        kTown, std::make_shared<const v::VectorItemSubset>(0, std::vector<v::VectorDataItemPtr>{pt(kPoint)}));
    rig.markDb->subsets.emplace_back(kStreetPath,
                                     std::make_shared<const v::VectorItemSubset>(
                                         0, std::vector<v::VectorDataItemPtr>{ln(kLine), pt(kHidden)}));
    const auto db = rig.make(m::cORMSQLiteMarksDbGUID);

    const v::ImportConfigPtr excluding =
        std::make_shared<const v::ImportConfig>(nullptr, nullptr, nullptr, nullptr, nullptr);
    const v::ImportConfigPtr normal = std::make_shared<const v::ImportConfig>(
        std::make_shared<const v::Category>(QStringLiteral("Q")),
        std::make_shared<const v::ImportCategoryParams>(false, true, false, true), nullptr, nullptr, nullptr);
    for (const v::ImportConfigPtr& config : {v::ImportConfigPtr{}, excluding, normal}) {
        rig.log.clear();
        const auto imported = db.processImport({}, QStringLiteral("/d/m.db3"), config);
        ASSERT_TRUE(imported);
        EXPECT_EQ(describe(*imported), "[](Город[Точка](Улица[Линия,Скрытая]))");
        // include-hidden = true for every category, parent before child
        EXPECT_EQ(rig.log, (t::CallLog{"mark.subset Город hidden=1", "mark.subset Город\\Улица hidden=1"}));
    }
}

// Finding f: a null collaborator is refused at construction (distinct argument each time).
TEST(VectorItemTreeMarksDb, NullCollaboratorsAreRefused) {
    Rig rig;
    const QUuid guid = m::cORMSQLiteMarksDbGUID;
    EXPECT_THROW(m::VectorItemTreeMarksDb(guid, nullptr, rig.categoryFactory, rig.appearances, rig.list),
                 std::invalid_argument);
    EXPECT_THROW(m::VectorItemTreeMarksDb(guid, rig.markFactory, nullptr, rig.appearances, rig.list),
                 std::invalid_argument);
    EXPECT_THROW(m::VectorItemTreeMarksDb(guid, rig.markFactory, rig.categoryFactory, nullptr, rig.list),
                 std::invalid_argument);
    EXPECT_THROW(
        m::VectorItemTreeMarksDb(guid, rig.markFactory, rig.categoryFactory, rig.appearances, nullptr),
        std::invalid_argument);
    EXPECT_NO_THROW(
        m::VectorItemTreeMarksDb(guid, rig.markFactory, rig.categoryFactory, rig.appearances, rig.list));
}

// Proof 6: end to end on a real .db3 through the REAL factory list. Export a two-category tree, hide one mark
// in the file (a second opening), import with a second object, compare names, categories and geometry sizes.
// The .sml case is not run: the landed list registers only the SQLite3 backend (MarkSystemImplFactorySML is
// not landed, see mark_system_impl_factory_changeable.h).
TEST(VectorItemTreeMarksDb, Db3RoundTripThroughTheRealFactoryList) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file = QDir(dir.path()).filePath(QStringLiteral("Marks.db3"));
    const t::Parts parts;
    const auto list = t::makeList(parts);
    const auto categoryFactory = std::make_shared<t::FakeCategoryFactory>();
    const m::VectorItemTreeMarksDb exporter(m::cORMSQLiteMarksDbGUID, parts.markFactory, categoryFactory,
                                            parts.appearances, list);
    const auto tree = node(QString(), {},
                           {node(kTown, {pt(kPoint), ln(kLine)}, {node(kStreet, {pg(kPoly), pt(kHidden)})})});
    const auto exported = exporter.processExport({}, file, tree);
    ASSERT_TRUE(exported) << exported.error().what().toStdString();
    ASSERT_TRUE(QFile::exists(file));

    {
        // hide one mark in the file through the real backend
        const auto impl = list->get(m::cORMSQLiteMarksDbGUID)
                              ->factory()
                              ->build({}, dir.path() + u'/', t::ormConfig(false, file));
        ASSERT_TRUE(impl);
        const auto category = impl->categoryDB()->firstCategoryByName(kStreetPath);
        ASSERT_TRUE(category);
        const auto mark = impl->markDb()->firstMarkByName(kHidden, category);
        ASSERT_TRUE(mark);
        impl->markDb()->setMarkVisible(mark, false);
    }

    const m::VectorItemTreeMarksDb importer(m::cORMSQLiteMarksDbGUID, parts.markFactory, categoryFactory,
                                            parts.appearances, list);
    const auto imported = importer.processImport({}, file, nullptr);
    ASSERT_TRUE(imported) << imported.error().what().toStdString();
    ASSERT_TRUE(*imported);

    // root "" -> Город (Точка, Линия) -> Улица (Полигон, Скрытая); item order inside a category is storage
    // order
    const auto& root = *imported;
    EXPECT_EQ(root->name(), QString());
    ASSERT_EQ(root->subTreeItemCount(), 1);
    const auto town = root->subTreeItem(0);
    EXPECT_EQ(town->name(), kTown);
    ASSERT_TRUE(town->items());
    EXPECT_EQ(town->items()->count(), 2);
    QStringList townNames;
    for (int i = 0; i < town->items()->count(); ++i) {
        const auto item = town->items()->item(i);
        townNames.append(item->name());
        EXPECT_EQ(pointCount(item), item->name() == kPoint ? 1U : 2U);
    }
    townNames.sort();
    EXPECT_EQ(townNames, t::sorted(QStringList{kPoint, kLine}));

    ASSERT_EQ(town->subTreeItemCount(), 1);
    const auto street = town->subTreeItem(0);
    EXPECT_EQ(street->name(), kStreet);
    ASSERT_TRUE(street->items());
    EXPECT_EQ(street->items()->count(), 2); // the hidden mark is in the tree (include-hidden = true)
    QStringList streetNames;
    for (int i = 0; i < street->items()->count(); ++i) {
        const auto item = street->items()->item(i);
        streetNames.append(item->name());
        EXPECT_EQ(pointCount(item), item->name() == kPoly ? 3U : 1U);
    }
    streetNames.sort();
    EXPECT_EQ(streetNames, t::sorted(QStringList{kPoly, kHidden}));
}
