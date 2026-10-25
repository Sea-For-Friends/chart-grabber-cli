// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkFullBase.pas (TMarkMainInfo): info strings and the IsEqual rules (:130-170).
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_full_base.h"
#include "cg/vector/vector_data_item_base.h"

#include <gtest/gtest.h>

#include <memory>

#include "mark_test_util.h"

namespace m = cg::marks;
namespace v = cg::vector;
namespace t = cg::marks::testutil;

namespace {

std::shared_ptr<const m::MarkMainInfo> info(cg::core::HashValue hash, const QString& name,
                                            const QString& desc, v::CategoryPtr category = nullptr) {
    return std::make_shared<const m::MarkMainInfo>(hash, t::converter(), name, std::move(category), desc);
}

TEST(MarkMainInfo, InfoStrings) {
    const auto i = info(5, QStringLiteral("n"), QStringLiteral("d"));
    EXPECT_EQ(i->hintText(), QStringLiteral("n|d"));
    EXPECT_EQ(i->infoCaption(), QStringLiteral("n"));
    EXPECT_EQ(i->infoHtml(), QStringLiteral("<HTML><BODY>d</BODY></HTML>"));
    EXPECT_TRUE(i->infoUrl().isEmpty());
    EXPECT_TRUE(info(5, QStringLiteral("n"), QString())->infoHtml().isEmpty());
}

TEST(MarkMainInfo, IsEqualRules) {
    const auto cat = std::make_shared<const m::MarkCategory>(QStringLiteral("c"), true, 3, 23);
    const auto other = std::make_shared<const m::MarkCategory>(QStringLiteral("c"), true, 3, 23);
    const auto a = info(7, QStringLiteral("n"), QStringLiteral("d"), cat);

    EXPECT_FALSE(a->isEqual(nullptr));
    EXPECT_TRUE(a->isEqual(a.get()));
    EXPECT_TRUE(a->isEqual(info(7, QStringLiteral("n"), QStringLiteral("d"), cat).get()));
    // A zero hash on either side skips the hash comparison; two non-zero different hashes differ.
    EXPECT_TRUE(a->isEqual(info(0, QStringLiteral("n"), QStringLiteral("d"), cat).get()));
    EXPECT_FALSE(a->isEqual(info(8, QStringLiteral("n"), QStringLiteral("d"), cat).get()));
    EXPECT_FALSE(a->isEqual(info(7, QStringLiteral("x"), QStringLiteral("d"), cat).get()));
    EXPECT_FALSE(a->isEqual(info(7, QStringLiteral("n"), QStringLiteral("x"), cat).get()));
    // The category is compared by identity (IsSame), not by value.
    EXPECT_FALSE(a->isEqual(info(7, QStringLiteral("n"), QStringLiteral("d"), other).get()));
    EXPECT_FALSE(a->isEqual(info(7, QStringLiteral("n"), QStringLiteral("d")).get()));
    EXPECT_FALSE(info(7, QStringLiteral("n"), QStringLiteral("d"))->isEqual(a.get()));
    EXPECT_TRUE(info(7, QStringLiteral("n"), QStringLiteral("d"))
                    ->isEqual(info(7, QStringLiteral("n"), QStringLiteral("d")).get()));
    // A main info without IVectorDataItemWithCategory is never equal.
    const v::VectorDataItemMainInfo plain(7, t::converter(), QStringLiteral("n"), QStringLiteral("d"));
    EXPECT_FALSE(a->isEqual(&plain));
}

} // namespace
