// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_PredicateByStaticSampleList.pas (docs/specs/maptype.md 2.13.4). Expected values are literals
// derived from the Pascal; the 8-byte pair below was found by a script (python zlib.crc32): both strings have
// CRC-32 0x3892fd9c, so they land in one size+hash bucket and only the byte comparison tells them apart.
#include "cg/maptype/predicate_by_static_sample_list.h"

#include <gtest/gtest.h>

using cg::maptype::makePredicateByStaticSampleList;
using cg::maptype::PredicateByStaticSampleList;

namespace {

const QByteArray kA = "gnyijstj"; // crc32 0x3892fd9c
const QByteArray kB = "etislvlf"; // crc32 0x3892fd9c

TEST(PredicateByStaticSampleList, MatchesOnlyByteEqualSamples) {
    // unsorted, with a duplicate and an empty sample (skipped, :70)
    const PredicateByStaticSampleList p({"abd", QByteArray(), "abc", "abd", "wxyz"});
    EXPECT_TRUE(p.check("abc"));
    EXPECT_TRUE(p.check("abd"));
    EXPECT_TRUE(p.check("wxyz"));
    EXPECT_FALSE(p.check("abe")); // same size, other bytes
    EXPECT_FALSE(p.check("ab"));  // prefix
    EXPECT_FALSE(p.check("abcd"));
    EXPECT_FALSE(
        p.check("\xd0\x93\xd0\xbe\xd1\x80")); // non-ASCII bytes of "Гор": same size as nothing stored
    EXPECT_FALSE(p.check(QByteArray()));      // size 0 matches nothing, also not the skipped empty sample
}

TEST(PredicateByStaticSampleList, NonAsciiSampleBytes) {
    const PredicateByStaticSampleList p({"\xd0\x93\xd0\xbe\xd1\x80\xd0\xbe\xd0\xb4"}); // "Город"
    EXPECT_TRUE(p.check("\xd0\x93\xd0\xbe\xd1\x80\xd0\xbe\xd0\xb4"));
    EXPECT_FALSE(p.check("\xd0\xb3\xd0\xbe\xd1\x80\xd0\xbe\xd0\xb4")); // "город": one byte differs
}

TEST(PredicateByStaticSampleList, SameSizeAndHashButOtherBytesDoesNotMatch) {
    const PredicateByStaticSampleList one({kA});
    EXPECT_TRUE(one.check(kA));
    EXPECT_FALSE(one.check(kB)); // :158-160, CompareMem decides

    const PredicateByStaticSampleList both({kB, kA}); // two samples in one bucket: the list branch (:98-117)
    EXPECT_TRUE(both.check(kA));
    EXPECT_TRUE(both.check(kB));
    EXPECT_FALSE(both.check("abcdefgh"));
}

TEST(PredicateByStaticSampleList, NoSamplesMatchNothing) {
    const PredicateByStaticSampleList none({});
    EXPECT_FALSE(none.check("abc"));
    EXPECT_FALSE(none.check(QByteArray()));
    const PredicateByStaticSampleList onlyEmpty({QByteArray(), QByteArray("")});
    EXPECT_FALSE(onlyEmpty.check("abc"));
    EXPECT_FALSE(onlyEmpty.check(QByteArray()));
}

TEST(PredicateByStaticSampleList, CallableOwnsItsTable) {
    cg::maptype::PredicateByBinaryData f;
    {
        std::vector<QByteArray> samples = {"one", "three"};
        f = makePredicateByStaticSampleList(samples);
        samples.clear();
    }
    EXPECT_TRUE(f("three"));
    EXPECT_FALSE(f("two"));
}

} // namespace
