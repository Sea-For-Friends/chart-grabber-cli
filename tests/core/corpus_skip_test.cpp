// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: T-0370 tests/corpus_skip.h: a missing fixture skips in the public tree (CG_REQUIRE_FIXTURES unset)
// and FAILS in the private checkout (CG_REQUIRE_FIXTURES=1, exported by tools/validate.sh), so it can never
// become a silent skip.
#include <QtGlobal>

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include "../corpus_skip.h"
#include "fixture_path.h"

namespace {

void missingFixture() {
    CG_SKIP_IF_NO_FIXTURE(QStringLiteral("/nonexistent/cg-t0370/fixture.bin"));
    ADD_FAILURE() << "the macro did not return";
}

void missingCorpus() {
    CG_SKIP_IF_NO_CORPUS_IN(QStringLiteral("/nonexistent/cg-t0370"));
    ADD_FAILURE() << "the macro did not return";
}

void presentFixture() {
    CG_SKIP_IF_NO_FIXTURE(cgFixture(QStringLiteral("marks/README.md")));
    SUCCEED();
}

// Runs fn with CG_REQUIRE_FIXTURES set to value (nullptr = unset) and collects what it reported in results,
// without letting the reports reach the running test.
void runWith(const char* value, void (*fn)(), testing::TestPartResultArray& results) {
    const QByteArray saved = qgetenv("CG_REQUIRE_FIXTURES");
    if (value) {
        qputenv("CG_REQUIRE_FIXTURES", value);
    } else {
        qunsetenv("CG_REQUIRE_FIXTURES");
    }
    {
        testing::ScopedFakeTestPartResultReporter reporter(
            testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &results);
        fn();
    }
    if (saved.isNull()) {
        qunsetenv("CG_REQUIRE_FIXTURES");
    } else {
        qputenv("CG_REQUIRE_FIXTURES", saved);
    }
}

} // namespace

TEST(CorpusSkip, MissingFixtureSkipsInThePublicTree) {
    testing::TestPartResultArray r;
    runWith(nullptr, missingFixture, r);
    ASSERT_EQ(r.size(), 1);
    EXPECT_EQ(r.GetTestPartResult(0).type(), testing::TestPartResult::kSkip);
    EXPECT_NE(std::string(r.GetTestPartResult(0).message()).find("not present (public tree): fixture.bin"),
              std::string::npos);
}

TEST(CorpusSkip, MissingFixtureFailsInThePrivateCheckout) {
    testing::TestPartResultArray r;
    runWith("1", missingFixture, r);
    ASSERT_EQ(r.size(), 1);
    EXPECT_EQ(r.GetTestPartResult(0).type(), testing::TestPartResult::kFatalFailure);
    EXPECT_NE(std::string(r.GetTestPartResult(0).message()).find("not present (public tree): fixture.bin"),
              std::string::npos);
}

TEST(CorpusSkip, MissingCorpusSkipsInThePublicTreeAndFailsInThePrivateCheckout) {
    testing::TestPartResultArray pub;
    runWith(nullptr, missingCorpus, pub);
    ASSERT_EQ(pub.size(), 1);
    EXPECT_EQ(pub.GetTestPartResult(0).type(), testing::TestPartResult::kSkip);
    testing::TestPartResultArray priv;
    runWith("1", missingCorpus, priv);
    ASSERT_EQ(priv.size(), 1);
    EXPECT_EQ(priv.GetTestPartResult(0).type(), testing::TestPartResult::kFatalFailure);
    EXPECT_NE(std::string(priv.GetTestPartResult(0).message()).find("not present (public tree)"),
              std::string::npos);
}

TEST(CorpusSkip, OnlyTheValueOneRequiresFixtures) {
    testing::TestPartResultArray r;
    runWith("0", missingFixture, r);
    ASSERT_EQ(r.size(), 1);
    EXPECT_EQ(r.GetTestPartResult(0).type(), testing::TestPartResult::kSkip);
}

TEST(CorpusSkip, PresentFixtureIsNotSkippedAndNotFailed) {
    for (const char* value : {static_cast<const char*>(nullptr), "1"}) {
        testing::TestPartResultArray r;
        runWith(value, presentFixture, r);
        ASSERT_EQ(r.size(), 1);
        EXPECT_EQ(r.GetTestPartResult(0).type(), testing::TestPartResult::kSuccess);
    }
}
