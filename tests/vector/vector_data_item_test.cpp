// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-037. Ports u_VectorDataItemBase.pas, u_VectorDataFactorySimple.pas;
// docs/specs/vector.md §2.4, §2.5, §7.2.
#include "cg/vector/appearance_of_mark_line.h"
#include "cg/vector/vector_data_factory_simple.h"
#include "cg/vector/vector_data_item_base.h"

#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>

using namespace cg::vector;
using cg::core::HashValue;

namespace {

// Records every convert() call; answers with a canned reply per call index.
class RecordingConverter final : public IHtmlToHintTextConverter {
public:
    explicit RecordingConverter(QString firstReply, QString secondReply = QStringLiteral("second"))
        : first_(std::move(firstReply)), second_(std::move(secondReply)) {}
    QString convert(const QString& name, const QString& description) const override {
        calls.emplace_back(name, description);
        return calls.size() == 1 ? first_ : second_;
    }
    mutable std::vector<std::pair<QString, QString>> calls;

private:
    QString first_;
    QString second_;
};

// An item implemented elsewhere (marks) whose geometry is null: legacy IsSameGeometry(nil) is False.
class NullGeometryItem final : public IVectorDataItem {
public:
    explicit NullGeometryItem(VectorDataItemPtr inner) : inner_(std::move(inner)) {}
    HashValue hash() const noexcept override { return inner_->hash(); }
    VectorDataItemMainInfoPtr mainInfo() const override { return inner_->mainInfo(); }
    QString name() const override { return inner_->name(); }
    QString desc() const override { return inner_->desc(); }
    cg::geo::GeometryLonLatPtr geometry() const override { return nullptr; }
    AppearancePtr appearance() const override { return inner_->appearance(); }
    bool isEqual(const IVectorDataItem* item) const override { return inner_->isEqual(item); }
    QString hintText() const override { return inner_->hintText(); }
    QString infoUrl() const override { return inner_->infoUrl(); }
    QString infoHtml() const override { return inner_->infoHtml(); }
    QString infoCaption() const override { return inner_->infoCaption(); }

private:
    VectorDataItemPtr inner_;
};

cg::geo::GeometryLonLatPtr pointAt(HashValue hash, double lon, double lat) {
    return std::make_shared<cg::geo::GeometryLonLatPoint>(hash, cg::geo::LonLat{lon, lat});
}

VectorDataItemMainInfoPtr info(HashValue hash, const QString& name, const QString& desc) {
    return std::make_shared<VectorDataItemMainInfo>(hash, nullptr, name, desc);
}

AppearancePtr appearance(HashValue hash) {
    return std::make_shared<AppearanceOfMarkLine>(hash, 1, 1);
}

HashValue chain(HashValue seed, const void* bytes, std::size_t size) {
    return cg::core::cityHash64WithSeed(bytes, size, seed);
}

HashValue chainHash(HashValue seed, HashValue value) { // the 8 little-endian bytes of value
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) {
        b[i] = static_cast<unsigned char>(value >> (8 * i));
    }
    return chain(seed, b, 8);
}

HashValue utf16Hash(const QString& s) {
    return cg::core::cityHash64(s.utf16(), static_cast<std::size_t>(s.size()) * 2);
}

} // namespace

TEST(VectorDataItemMainInfo, HintTextAsksForTheNameAloneThenForNameAndDesc) {
    auto conv = std::make_shared<RecordingConverter>(QStringLiteral("first"));
    const VectorDataItemMainInfo mi(1, conv, QStringLiteral("n"), QStringLiteral("d"));
    EXPECT_EQ(mi.hintText(), QStringLiteral("first"));
    ASSERT_EQ(conv->calls.size(), 1U);
    EXPECT_EQ(conv->calls[0], std::make_pair(QStringLiteral("n"), QString()));

    auto conv2 = std::make_shared<RecordingConverter>(QString());
    const VectorDataItemMainInfo mi2(1, conv2, QStringLiteral("n"), QStringLiteral("d"));
    EXPECT_EQ(mi2.hintText(), QStringLiteral("second"));
    ASSERT_EQ(conv2->calls.size(), 2U);
    EXPECT_EQ(conv2->calls[1], std::make_pair(QStringLiteral("n"), QStringLiteral("d")));

    // Empty name and empty desc: the converter is still asked twice (no shortcut).
    auto conv3 = std::make_shared<RecordingConverter>(QString(), QString());
    const VectorDataItemMainInfo mi3(1, conv3, QString(), QString());
    EXPECT_TRUE(mi3.hintText().isEmpty());
    EXPECT_EQ(conv3->calls.size(), 2U);
}

TEST(VectorDataItemMainInfo, InfoFields) {
    const auto mi = info(1, QStringLiteral("Name"), QStringLiteral("<b>x</b>"));
    EXPECT_EQ(mi->infoCaption(), QStringLiteral("Name"));
    EXPECT_EQ(mi->infoHtml(), QStringLiteral("<HTML><BODY><b>x</b></BODY></HTML>"));
    EXPECT_TRUE(mi->infoUrl().isEmpty());
    EXPECT_TRUE(info(1, QStringLiteral("Name"), QString())->infoHtml().isEmpty());
    EXPECT_EQ(mi->name(), QStringLiteral("Name"));
    EXPECT_EQ(mi->desc(), QStringLiteral("<b>x</b>"));
}

TEST(VectorDataItemMainInfo, IsEqualLadder) {
    const auto a = info(5, QStringLiteral("n"), QStringLiteral("d"));
    struct Row {
        const char* what;
        VectorDataItemMainInfoPtr other;
        bool expected;
    };
    const Row rows[] = {
        {"same name/desc/hash", info(5, QStringLiteral("n"), QStringLiteral("d")), true},
        {"both hashes set and different, same text", info(6, QStringLiteral("n"), QStringLiteral("d")),
         false},
        {"other hash 0", info(0, QStringLiteral("n"), QStringLiteral("d")), true},
        {"name differs", info(5, QStringLiteral("N"), QStringLiteral("d")), false},
        {"desc differs", info(5, QStringLiteral("n"), QStringLiteral("D")), false},
        {"null", nullptr, false},
    };
    for (const auto& r : rows) {
        EXPECT_EQ(a->isEqual(r.other.get()), r.expected) << r.what;
    }
    EXPECT_TRUE(a->isEqual(a.get()));
    EXPECT_TRUE(info(0, QStringLiteral("n"), QStringLiteral("d"))->isEqual(a.get())); // this hash 0
}

TEST(VectorDataItem, ForwardsToMainInfoAndKeepsWhatItIsGiven) {
    auto conv = std::make_shared<RecordingConverter>(QStringLiteral("hint"));
    const auto mi =
        std::make_shared<VectorDataItemMainInfo>(2, conv, QStringLiteral("n"), QStringLiteral("d"));
    const auto geom = pointAt(3, 1, 2);
    const auto app = appearance(4);
    const VectorDataItem item(9, app, mi, geom);
    EXPECT_EQ(item.hash(), 9U);
    EXPECT_EQ(item.mainInfo(), mi);
    EXPECT_EQ(item.geometry(), geom);
    EXPECT_EQ(item.appearance(), app);
    EXPECT_EQ(item.name(), QStringLiteral("n"));
    EXPECT_EQ(item.desc(), QStringLiteral("d"));
    EXPECT_EQ(item.hintText(), QStringLiteral("hint"));
    EXPECT_EQ(item.infoCaption(), QStringLiteral("n"));
    EXPECT_EQ(item.infoHtml(), mi->infoHtml());
    EXPECT_TRUE(item.infoUrl().isEmpty());
}

TEST(VectorDataItem, IsEqualLadder) {
    const auto mi = info(0, QStringLiteral("n"), QStringLiteral("d"));
    const auto mi2 = info(0, QStringLiteral("n2"), QStringLiteral("d"));
    const auto g = pointAt(0, 1, 2);
    const VectorDataItem base(10, appearance(7), mi, g);
    const VectorDataItem noApp(10, nullptr, mi, g);
    struct Row {
        const char* what;
        const VectorDataItem* self;
        VectorDataItem other;
        bool expected;
    };
    const Row rows[] = {
        {"equal", &base, VectorDataItem(10, appearance(7), mi, pointAt(0, 1, 2)), true},
        {"hashes set and different (rest identical)", &base, VectorDataItem(11, appearance(7), mi, g), false},
        {"other hash 0 skips the hash rung", &base, VectorDataItem(0, appearance(7), mi, g), true},
        {"appearance hash differs", &base, VectorDataItem(10, appearance(8), mi, g), false},
        {"this has appearance, other none", &base, VectorDataItem(10, nullptr, mi, g), false},
        {"this none, other has appearance", &noApp, VectorDataItem(10, appearance(7), mi, g), false},
        {"both none", &noApp, VectorDataItem(10, nullptr, mi, g), true},
        {"main info differs", &base, VectorDataItem(10, appearance(7), mi2, g), false},
        {"geometry differs", &base, VectorDataItem(10, appearance(7), mi, pointAt(0, 1, 3)), false},
    };
    for (const auto& r : rows) {
        EXPECT_EQ(r.self->isEqual(&r.other), r.expected) << r.what;
    }
    EXPECT_FALSE(base.isEqual(nullptr));
    EXPECT_TRUE(base.isEqual(&base));
    const NullGeometryItem nullGeometry(std::make_shared<VectorDataItem>(10, appearance(7), mi, g));
    EXPECT_FALSE(base.isEqual(&nullGeometry));
}

TEST(VectorDataItemMainInfoFactory, HashIsNameThenDescChainedEmptyNameIsZero) {
    const VectorDataItemMainInfoFactory factory(std::make_shared<RecordingConverter>(QString()));
    const QString name = QStringLiteral("NameЖ");
    const QString desc = QStringLiteral("Desc");
    const HashValue h0 = utf16Hash(name);
    EXPECT_EQ(factory.buildMainInfo(nullptr, name, QString())->hash(), h0); // empty desc: unchanged
    EXPECT_EQ(factory.buildMainInfo(nullptr, name, desc)->hash(), chain(h0, desc.utf16(), 8));
    // Empty name: an empty Delphi string is nil, CalcHash(nil, 0) = 0 (u_HashFunctionCityHash.pas:59-63).
    EXPECT_EQ(factory.buildMainInfo(nullptr, QString(), QString())->hash(), 0U);
    EXPECT_EQ(factory.buildMainInfo(nullptr, QString(), desc)->hash(), chain(0, desc.utf16(), 8));
    const auto mi = factory.buildMainInfo(nullptr, name, desc);
    EXPECT_EQ(mi->name(), name);
    EXPECT_EQ(mi->desc(), desc);
}

TEST(VectorDataFactorySimple, ItemHashChainsMainInfoThenAppearanceHash) {
    const VectorDataFactorySimple factory;
    const auto geom = pointAt(0x1111222233334444ULL, 1, 2);
    const auto mi = info(0xAABBCCDDEEFF0011ULL, QStringLiteral("n"), QString());
    const auto app = appearance(0x0102030405060708ULL);

    const HashValue withoutApp = chainHash(geom->hash(), mi->hash());
    EXPECT_EQ(factory.buildItem(mi, nullptr, geom)->hash(), withoutApp);
    EXPECT_EQ(factory.buildItem(mi, app, geom)->hash(), chainHash(withoutApp, app->hash()));
    EXPECT_EQ(factory.buildItem(mi, app, geom)->appearance(), app);
}

// D-52 (T-0051): legacy dereferences nil here (access violation, u_VectorDataFactorySimple.pas:95-96);
// the port throws in every build.
TEST(VectorDataFactorySimple, NullMainInfoOrGeometryThrows) {
    const VectorDataFactorySimple factory;
    EXPECT_THROW((void)factory.buildItem(nullptr, nullptr, pointAt(1, 0, 0)), std::invalid_argument);
    EXPECT_THROW((void)factory.buildItem(info(1, {}, {}), nullptr, nullptr), std::invalid_argument);
}
