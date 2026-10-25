// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-NET-004. Ports u_TileDownloadRequestBuilderConfig.pas; docs/specs/maptype.md 2.7, 2.7.3.
#include "cg/core/config_provider_ini.h"
#include "cg/core/ini_file.h"
#include "cg/maptype/tile_download_request_builder_config.h"

#include <QString>

#include <gtest/gtest.h>

#include "maptype_test_util.h"

using namespace Qt::StringLiterals;
using cg::maptype::TileDownloadRequestBuilderConfig;
using cg::maptype::TileDownloadRequestBuilderConfigStatic;

namespace {

std::shared_ptr<TileDownloadRequestBuilderConfig> make(const char* url, const char* names, const char* head) {
    return std::make_shared<TileDownloadRequestBuilderConfig>(
        std::make_shared<const TileDownloadRequestBuilderConfigStatic>(
            TileDownloadRequestBuilderConfigStatic{url, names, head, true, "+proj=x"}));
}

TEST(TileDownloadRequestBuilderConfig, ZmpValuesPassThroughStrToUrlAndStrToHeader) {
    const auto c = make("  http://a b/{x} \t", "n1,n2", "Accept: *\\r\\nX: y\\R\\N");
    EXPECT_EQ(c->urlBase(), QByteArray("http://a%20b/{x}"));
    EXPECT_EQ(c->requestHeader(), QByteArray("Accept: *\r\nX: y\r\n"));
    EXPECT_EQ(c->serverNames(), QByteArray("n1,n2"));
    EXPECT_TRUE(c->isUseDownloader());
    EXPECT_EQ(c->defaultProjConverterArgs(), QByteArray("+proj=x"));
}

// One lock, one consistent view of the current (not the ZMP default) values.
TEST(TileDownloadRequestBuilderConfig, SnapshotHoldsTheCurrentValuesTogether) {
    const auto c = make("http://a/{x}", "n1", "H: 1");
    c->setUrlBase("http://b/{y}");
    c->setRequestHeader("K: 2");
    const TileDownloadRequestBuilderConfigStatic s = c->snapshot();
    EXPECT_EQ(s.urlBase, QByteArray("http://b/{y}"));
    EXPECT_EQ(s.serverNames, QByteArray("n1"));
    EXPECT_EQ(s.requestHeader, QByteArray("K: 2"));
    EXPECT_TRUE(s.isUseDownloader);
    EXPECT_EQ(s.defaultProjConverterArgs, QByteArray("+proj=x"));
}

TEST(TileDownloadRequestBuilderConfig, ReadsThreeKeysWritesOnlyDifferingTwoWithEscapedHeader) {
    const auto c = make("http://a/{x}", "n1", "H: 1");
    const auto ini = cg::maptype::test::iniFrom(
        "[S]\nURLBase=http://b c/{y}\nServerNames=q\nRequestHead=A: 1\\r\\nB: 2\n");
    c->readConfig(ini->subItem(u"S"));
    EXPECT_EQ(c->urlBase(), QByteArray("http://b%20c/{y}"));
    EXPECT_EQ(c->serverNames(), QByteArray("q"));
    EXPECT_EQ(c->requestHeader(), QByteArray("A: 1\r\nB: 2"));

    auto out = cg::maptype::test::iniFrom("[S]\nServerNames=old\n");
    c->writeConfig(out->orCreateSubItem(u"S"));
    EXPECT_EQ(out->subItem(u"S")->readString(u"URLBase"), QString("http://b%20c/{y}"));
    EXPECT_EQ(out->subItem(u"S")->readString(u"RequestHead"), QString("A: 1\\r\\nB: 2"));
    EXPECT_EQ(out->subItem(u"S")->readString(u"ServerNames"), QString("old")); // never written (L-140)

    // back to the ZMP values: the keys are deleted, not written
    c->setUrlBase("http://a/{x}");
    c->setRequestHeader("H: 1");
    c->writeConfig(out->orCreateSubItem(u"S"));
    EXPECT_EQ(out->subItem(u"S")->readString(u"URLBase", u"gone"_s), u"gone"_s);
    EXPECT_EQ(out->subItem(u"S")->readString(u"RequestHead", u"gone"_s), u"gone"_s);
}

TEST(TileDownloadRequestBuilderConfig, SettersNotifyOnlyOnRealChange) {
    const auto c = make("http://a", "", "");
    int changes = 0;
    const auto sub = c->changed().subscribe([&] { ++changes; });
    c->setUrlBase(" http://a "); // equal after StrToUrl
    c->setServerNames("");
    c->setRequestHeader("");
    EXPECT_EQ(changes, 0);
    c->readConfig(
        cg::maptype::test::iniFrom("[S]\n")->subItem(u"S")); // unchanged: no notification, unlike siblings
    EXPECT_EQ(changes, 0);
    c->setUrlBase("http://b");
    c->setServerNames("x");
    c->setRequestHeader("a\\r\\nb");
    EXPECT_EQ(changes, 3);
}

} // namespace
