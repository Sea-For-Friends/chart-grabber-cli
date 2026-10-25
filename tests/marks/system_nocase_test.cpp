// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: FEAT-GEO-034 (held open). The SYSTEMNOCASE collation of Marks.db3, docs/specs/marks.md
// §2.9.4, against vectors computed from the mORMot Pascal source by
// tools/gen_system_nocase_vectors.py (tests/fixtures/marks/README.md).
#include "cg/marks/mormot/system_nocase.h"

#include <QByteArray>
#include <QFile>
#include <QList>

#include <gtest/gtest.h>

#include "../corpus_skip.h"
#include "fixture_path.h"

using cg::marks::systemNoCaseCompare;

namespace {

int sign(int value) {
    return (value > 0) - (value < 0);
}

QByteArray fromFixtureHex(const QByteArray& field) {
    return field == "-" ? QByteArray() : QByteArray::fromHex(field);
}

} // namespace

TEST(SystemNoCase, VectorsComputedFromThePascalSource) {
    CG_SKIP_IF_NO_FIXTURE(cgMarksFixture(QStringLiteral("marks/system_nocase_vectors.tsv")));
    QFile file(cgMarksFixture(QStringLiteral("marks/system_nocase_vectors.tsv")));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << file.fileName().toStdString();

    int vectors = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().chopped(1);
        if (line.startsWith('#')) {
            continue;
        }
        const QList<QByteArray> fields = line.split('\t');
        ASSERT_EQ(fields.size(), 4) << line.constData();
        const QByteArray a = fromFixtureHex(fields[0]);
        const QByteArray b = fromFixtureHex(fields[1]);
        EXPECT_EQ(sign(systemNoCaseCompare(a, b)), fields[2].toInt())
            << fields[3].constData() << ": " << fields[0].constData() << " vs " << fields[1].constData();
        ++vectors;
    }
    EXPECT_EQ(vectors, 928); // the whole fixture was read
}

// Utf8ILComp starts with `if u1 <> u2`: one buffer compared with itself is equal before any length
// is looked at. SQLite never passes that with two different lengths; the branch is kept as written.
TEST(SystemNoCase, SameBufferIsEqualWhateverTheLengths) {
    const QByteArray text("abc");
    EXPECT_EQ(systemNoCaseCompare(QByteArrayView(text.constData(), 3), QByteArrayView(text.constData(), 1)),
              0);
    EXPECT_GT(systemNoCaseCompare(QByteArray("abc"), QByteArray("a")), 0);
}

// Deviation D-69 regression: FC 80 80 80 81 81 decodes, in the 64-bit Windows build, to
// 0x3F00000041, passes the "is 8-bit" test (bits 8..31 are clear) and indexes the 256-entry table
// at that offset. The port folds the low 8 bits (0x41), as the 32-bit build does.
TEST(SystemNoCase, SixByteFormOfAnEightBitCodePointFoldsItsLowByte) {
    const QByteArray sixByteA("\xFC\x80\x80\x80\x81\x81", 6);
    EXPECT_EQ(systemNoCaseCompare(sixByteA, QByteArray("a")), 0);
    EXPECT_EQ(systemNoCaseCompare(QByteArray("A"), sixByteA), 0);
    EXPECT_LT(systemNoCaseCompare(sixByteA, QByteArray("b")), 0);
    EXPECT_GT(systemNoCaseCompare(QByteArray("b"), sixByteA), 0);
}
