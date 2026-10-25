// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-033, FEAT-GEO-034 (held open). Compile check of every interface header of ticket M1
// (docs/specs/marks.md sections 2.1-2.3, 2.6) and the constants of c_MarkSystem.pas.
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mark_category_db.h"
#include "cg/marks/mark_category_db_impl.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_factory_config.h"
#include "cg/marks/mark_db.h"
#include "cg/marks/mark_db_impl.h"
#include "cg/marks/mark_factory.h"
#include "cg/marks/mark_factory_config.h"
#include "cg/marks/mark_id.h"
#include "cg/marks/mark_name_generator.h"
#include "cg/marks/mark_system.h"
#include "cg/marks/mark_system_config.h"
#include "cg/marks/mark_system_impl.h"
#include "cg/marks/mark_system_impl_changeable.h"
#include "cg/marks/mark_system_impl_config.h"
#include "cg/marks/mark_system_impl_factory.h"
#include "cg/marks/mark_template.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace m = cg::marks;

namespace {

static_assert(std::is_abstract_v<m::IMarkId> && std::is_abstract_v<m::IMarkTemplate> &&
              std::is_abstract_v<m::IMarkTemplatePoint> && std::is_abstract_v<m::IMarkTemplateLine> &&
              std::is_abstract_v<m::IMarkTemplatePoly>);
static_assert(std::is_abstract_v<m::IMarkDb> && std::is_abstract_v<m::IMarkDbImpl> &&
              std::is_abstract_v<m::IMarkCategoryDB> && std::is_abstract_v<m::IMarkCategoryDBImpl>);
static_assert(std::is_abstract_v<m::IMarkSystem> && std::is_abstract_v<m::IMarkSystemImpl> &&
              std::is_abstract_v<m::IMarkSystemImplChangeable> &&
              std::is_abstract_v<m::IMarkSystemImplFactory> &&
              std::is_abstract_v<m::IMarkSystemImplFactoryListElement> &&
              std::is_abstract_v<m::IMarkSystemImplFactoryListStatic> &&
              std::is_abstract_v<m::IMarkSystemImplConfigStatic> &&
              std::is_abstract_v<m::IMarkSystemConfigStatic> &&
              std::is_abstract_v<m::IMarkSystemConfigListChangeable>);
static_assert(std::is_abstract_v<m::IMarkCategoryFactory> &&
              std::is_abstract_v<m::IMarkCategoryFactoryConfig> &&
              std::is_abstract_v<m::IMarkNameGenerator> && std::is_abstract_v<m::IMarkFactory> &&
              std::is_abstract_v<m::IMarkFactoryConfig> && std::is_abstract_v<m::IMarkPointTemplateConfig> &&
              std::is_abstract_v<m::IMarkLineTemplateConfig> &&
              std::is_abstract_v<m::IMarkPolyTemplateConfig>);
static_assert(static_cast<int>(m::MarkIdType::Point) == 0 && static_cast<int>(m::MarkIdType::Line) == 1 &&
              static_cast<int>(m::MarkIdType::Poly) == 2);

TEST(MarkSystemConstants, Guids) {
    EXPECT_EQ(m::cSMLMarksDbGUID.toString(), QStringLiteral("{f5a9fb07-ff9b-4b4a-98d3-ab8c5f24f36b}"));
    EXPECT_EQ(m::cORMSQLiteMarksDbGUID.toString(), QStringLiteral("{776bc34b-7a4b-4237-be9a-d905228fda1a}"));
    EXPECT_EQ(m::cORMMongoDbMarksDbGUID.toString(), QStringLiteral("{aa5df0da-c785-4c1f-adf5-2fc90146be5b}"));
    EXPECT_EQ(m::cORMODBCMarksDbGUID.toString(), QStringLiteral("{03578f25-094a-4c2f-8609-5d7e76699ba3}"));
    EXPECT_EQ(m::cORMZDBCMarksDbGUID.toString(), QStringLiteral("{4d7a6f63-412e-46e3-afd0-4c0e8b9af2c5}"));
}

TEST(MarkSystemConstants, FileNamesAndInternalUrl) {
    EXPECT_EQ(m::cSMLMarksDbDefFileName.toString() + m::cSMLMarksDbFileExt.toString(),
              QStringLiteral("marks.sml"));
    EXPECT_EQ(m::cORMSQLiteMarksDbDefFileName.toString() + m::cORMSQLiteMarksDbFileExt.toString(),
              QStringLiteral("Marks.db3"));
    EXPECT_EQ(m::cMarksSystemInternalURL, u"sas://Placemarks/");
    EXPECT_EQ(m::rsORMSQLiteMarksDbName(), QStringLiteral("SQLite3"));
}

} // namespace
