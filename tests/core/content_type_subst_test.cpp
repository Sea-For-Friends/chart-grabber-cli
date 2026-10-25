// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: (ContentTypeSubst, consumed by net FEAT-NET-011); docs/specs/core.md 2.22, test plan 7.16
#include "cg/core/config_provider_ini.h"
#include "cg/core/content_type_subst.h"
#include "cg/core/ini_file.h"

#include <gtest/gtest.h>

#include <memory>

using cg::core::ConfigProviderIni;
using cg::core::ContentTypeSubst;
using cg::core::IniFile;

TEST(ContentTypeSubst, EmptyIsIdentity) {
    const ContentTypeSubst subst;
    EXPECT_TRUE(subst.isEmpty());
    EXPECT_EQ(subst.substitute("image/png"), QByteArray("image/png"));
}

TEST(ContentTypeSubst, UnknownSourcePassesThrough) {
    const ContentTypeSubst subst = ContentTypeSubst::fromString(QStringLiteral("image/png=image/jpeg"));
    EXPECT_EQ(subst.substitute("text/plain"), QByteArray("text/plain"));
}

TEST(ContentTypeSubst, PlainPair) {
    const ContentTypeSubst subst =
        ContentTypeSubst::fromString(QStringLiteral(R"("application/octet-stream=image/png")"));
    EXPECT_EQ(subst.substitute("application/octet-stream"), QByteArray("image/png"));
}

TEST(ContentTypeSubst, UnquotedPairWithTwoEquals) {
    const ContentTypeSubst subst = ContentTypeSubst::fromString(QStringLiteral("image/png=image/jpeg"));
    EXPECT_EQ(subst.substitute("image/png"), QByteArray("image/jpeg"));
}

TEST(ContentTypeSubst, QuotedSourceWithSemicolon) {
    const ContentTypeSubst subst = ContentTypeSubst::fromString(QStringLiteral(
        R"("""application/vnd.google-earth.kml+xml; charset=utf-8""=application/vnd.google-earth.kml+xml")"));
    EXPECT_EQ(subst.substitute("application/vnd.google-earth.kml+xml; charset=utf-8"),
              QByteArray("application/vnd.google-earth.kml+xml"));
}

TEST(ContentTypeSubst, QuotedSourceWithSemicolonNoSpace) {
    const ContentTypeSubst subst =
        ContentTypeSubst::fromString(QStringLiteral(R"("""image/jpeg;charset=ISO-8859-1""=image/jpeg")"));
    EXPECT_EQ(subst.substitute("image/jpeg;charset=ISO-8859-1"), QByteArray("image/jpeg"));
}

TEST(ContentTypeSubst, KmlCharsetPair) {
    const ContentTypeSubst subst = ContentTypeSubst::fromString(
        QStringLiteral(R"("""text/xml; charset=UTF-8""=application/vnd.google-earth.kml+xml")"));
    EXPECT_EQ(subst.substitute("text/xml; charset=UTF-8"),
              QByteArray("application/vnd.google-earth.kml+xml"));
}

TEST(ContentTypeSubst, LookupIsCaseInsensitive) {
    const ContentTypeSubst subst = ContentTypeSubst::fromString(QStringLiteral("image/png=image/jpeg"));
    EXPECT_EQ(subst.substitute("IMAGE/PNG"), QByteArray("image/jpeg"));
}

TEST(ContentTypeSubst, MalformedEntryIsSkipped) {
    const ContentTypeSubst subst =
        ContentTypeSubst::fromString(QStringLiteral("no-equals-sign;image/png=image/jpeg"));
    EXPECT_EQ(subst.substitute("image/png"), QByteArray("image/jpeg"));
    EXPECT_EQ(subst.substitute("no-equals-sign"), QByteArray("no-equals-sign")); // skipped, passes through
}

TEST(ContentTypeSubst, EmptySourceOrTargetPairIsDropped) {
    // u_ContentTypeSubstByList.pas:125 ParseSubstListItem keeps a pair only when (VSource <> '') and (VTarget
    // <> '')
    const ContentTypeSubst subst =
        ContentTypeSubst::fromString(QStringLiteral("image/png=;=image/gif;image/bmp=image/jpeg"));
    EXPECT_EQ(subst.substitute("image/png"), QByteArray("image/png"));
    EXPECT_EQ(subst.substitute("image/bmp"), QByteArray("image/jpeg"));
    EXPECT_TRUE(ContentTypeSubst::fromString(QStringLiteral("a/b=")).isEmpty());
}

TEST(ContentTypeSubst, FromConfigReadsMimeTypeSubstKey) {
    auto file = std::make_shared<IniFile>();
    const auto root = ConfigProviderIni::wrap(file);
    const auto section = root->orCreateSubItem(u"PARAMS");
    section->writeString(u"MimeTypeSubst", QStringLiteral("image/png=image/jpeg"));
    const ContentTypeSubst subst = ContentTypeSubst::fromConfig(root->subItem(u"PARAMS"));
    EXPECT_EQ(subst.substitute("image/png"), QByteArray("image/jpeg"));
    EXPECT_TRUE(ContentTypeSubst::fromConfig(nullptr).isEmpty());
}

TEST(ContentTypeSubst, AllTenCorpusValuesParse) {
    const struct {
        const char* value;
        const char* source;
        const char* target;
    } corpus[] = {
        {R"("application/octet-stream=image/png")", "application/octet-stream", "image/png"},
        {R"("""application/vnd.google-earth.kml+xml; charset=utf-8""=application/vnd.google-earth.kml+xml")",
         "application/vnd.google-earth.kml+xml; charset=utf-8", "application/vnd.google-earth.kml+xml"},
        {R"("""image/jpeg;charset=ISO-8859-1""=image/jpeg")", "image/jpeg;charset=ISO-8859-1", "image/jpeg"},
        {R"("image/png8bit=image/png")", "image/png8bit", "image/png"},
        {R"("image/png8tr=image/png")", "image/png8tr", "image/png"},
        {R"("image/pnga=image/png")", "image/pnga", "image/png"},
        {R"("image/png=image/jpeg")", "image/png", "image/jpeg"},
        {"image/png=image/jpeg", "image/png", "image/jpeg"},
        {R"("text/plain=image/png")", "text/plain", "image/png"},
        {R"("""text/xml; charset=UTF-8""=application/vnd.google-earth.kml+xml")", "text/xml; charset=UTF-8",
         "application/vnd.google-earth.kml+xml"},
    };
    for (const auto& entry : corpus) {
        const ContentTypeSubst subst = ContentTypeSubst::fromString(QString::fromUtf8(entry.value));
        EXPECT_EQ(subst.substitute(entry.source), QByteArray(entry.target)) << entry.value;
    }
}
