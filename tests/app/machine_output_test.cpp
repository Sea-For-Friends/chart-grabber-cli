// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Tests for src/cg/app/machine_output (new code, T-0202; no FEAT). Expected lines are literals of the
// record layouts of the ticket (cli-requirements.md section 2).
#include "cg/app/fill_report.h"
#include "cg/app/machine_output.h"
#include "cg/app/tile_estimate.h"
#include "cg/core/platform/qt_compat.h"

#include <QDateTime>
#include <QLocale>

#include <gtest/gtest.h>

using namespace cg::app;
using namespace Qt::StringLiterals;

// Proof 6: TAB, CR and LF inside a field become one space each; the line keeps exactly fields-count TABs.
TEST(MachineOutput, TabCrLfInsideAFieldBecomeSpacesAndNeverSplitIt) {
    const QString line = tsvLine(u"source", {u"a\tb"_s, u"c\r\nd"_s, u"Город\te"_s, QString(), u"z"_s});
    EXPECT_EQ(line, u"source\ta b\tc  d\tГород e\t\tz"_s);
    EXPECT_EQ(line.count(u'\t'), 5);
}

TEST(MachineOutput, EstimateRecordHasNineFieldsShareFourDecimalsAndDotUnderAGermanLocale) {
    const QLocale saved;
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    ZoomEstimate a;
    a.zoom = 6;
    a.tiles = 80;
    a.bboxTiles = 90;
    a.sharePresent = 0.125;
    a.shareMarkedMissing = 1.0 / 3.0;
    a.bytesToDownload = 96000;
    a.assumed = true;
    a.bytesOnDisk = 111000;
    ZoomEstimate b;
    b.zoom = 3;
    b.tiles = 2;
    const QStringList lines = estimateRecords(u"{G}"_s, {a, b});
    QLocale::setDefault(saved);
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], u"estimate\t{G}\t6\t80\t90\t0.1250\t0.3333\t96000\t1\t111000"_s);
    EXPECT_EQ(lines[1], u"estimate\t{G}\t3\t2\t0\t0.0000\t0.0000\t0\t0\t0"_s);
}

TEST(MachineOutput, FillSessionAndWrittenRecordsKeepTheFieldOrder) {
    ZoomFill f;
    f.zoom = 7;
    f.tiles = 100;
    f.present = 60;
    f.markedMissing = 30;
    f.missing = 10;
    f.bytes = 123456;
    EXPECT_EQ(fillRecords(u"{G}"_s, {f}), QStringList{u"fill\t{G}\t7\t100\t60\t30\t10\t123456"_s});
    EXPECT_EQ(sessionRecord(u"{G}"_s, QDateTime(QDate(2026, 10, 15), QTime(9, 8, 7)), u"/x/a.sls"_s),
              u"session\t{G}\t2026-10-15 09:08:07\t/x/a.sls"_s);
    EXPECT_EQ(writtenRecord(u"{G}"_s, u"/x/a.mbtiles"_s, 77, 4096, QString()),
              u"written\t{G}\t/x/a.mbtiles\t77\t4096\t"_s);
}

// T-0243 (weather-sources.md 5.4): the `snapshot` record and field 5 of `written`, as literals; a date-only
// time is the date, an unknown time an empty field, a non-ASCII version stays as it is.
TEST(MachineOutput, SnapshotRecordAndWrittenFieldFiveKeepTheLayout) {
    Snapshot s;
    s.version = u"/v2/radar/7b6bac246650"_s;
    s.time = cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(20, 10, 0));
    EXPECT_EQ(snapshotRecord(u"{G}"_s, s), u"snapshot\t{G}\t2026-10-15T20:10:00Z\t/v2/radar/7b6bac246650"_s);

    Snapshot day;
    day.version = u"2026-10-15"_s;
    day.time = cg::core::platform::utcDateTime(QDate(2026, 10, 15), QTime(0, 0, 0));
    day.dateOnly = true;
    EXPECT_EQ(snapshotRecord(u"{G}"_s, day), u"snapshot\t{G}\t2026-10-15\t2026-10-15"_s);

    Snapshot unknown;
    unknown.version = u"Город"_s;
    EXPECT_EQ(snapshotRecord(u"{G}"_s, unknown), u"snapshot\t{G}\t\tГород"_s);

    EXPECT_EQ(writtenRecord(u"{G}"_s, u"/x/a.mbtiles"_s, 77, 4096, u"2026-10-15T20:10:00Z"_s),
              u"written\t{G}\t/x/a.mbtiles\t77\t4096\t2026-10-15T20:10:00Z"_s);
}

// T-0203: the layouts of cli-requirements.md section 3, as literals.
TEST(MachineOutput, PhaseAndProgressRecordsKeepTheFieldOrder) {
    EXPECT_EQ(phaseRecord(u"estimate", u"{G}"_s), u"phase\testimate\t{G}"_s);
    EXPECT_EQ(downloadProgressRecord(u"{G}"_s, 6, 7, 20, 3, 3072, 12),
              u"progress\tdownload\t{G}\t6\t7\t20\t3\t3072\t12"_s);
    EXPECT_EQ(exportProgressRecord(u"{G}"_s, 999), u"progress\texport\t{G}\t999"_s);
}
