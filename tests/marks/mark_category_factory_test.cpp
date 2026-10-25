// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports u_MarkCategoryFactory.pas (docs/specs/marks.md section 2.3). Ticket T-0091 is a slice of
// the marks module. One small table per factory method; the expected values are literals taken from
// the Pascal (:72-146), not from the class under test.
#include "cg/core/language_manager.h"
#include "cg/marks/mark_category.h"
#include "cg/marks/mark_category_factory.h"
#include "cg/marks/mark_category_factory_config.h"

#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

namespace m = cg::marks;
namespace c = cg::core;
using namespace Qt::StringLiterals;

namespace {

struct Expected {
    QString name;
    bool visible;
    int after;
    int before;
};

void expectCategory(const m::MarkCategoryPtr& result, const Expected& exp) {
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->name(), exp.name);
    EXPECT_EQ(result->visible(), exp.visible);
    EXPECT_EQ(result->afterScale(), exp.after);
    EXPECT_EQ(result->beforeScale(), exp.before);
}

// A factory over a real config: the default name and scales are changed only when given.
m::MarkCategoryFactory makeFactory(QTemporaryDir& dir, const QString& defaultName = {}, int after = -1,
                                   int before = -1) {
    auto config =
        std::make_shared<m::MarkCategoryFactoryConfig>(std::make_shared<c::LanguageManager>(dir.path()));
    if (!defaultName.isEmpty()) {
        config->defaultName()->setValue(defaultName);
    }
    if (after >= 0) {
        config->setAfterScale(after);
    }
    if (before >= 0) {
        config->setBeforeScale(before);
    }
    return m::MarkCategoryFactory(config);
}

} // namespace

// createNew (:72-99): "" -> default name; "X\" -> "X\" + default; other names kept; always visible.
TEST(MarkCategoryFactory, CreateNew) {
    struct Row {
        const char* label;
        QString defaultName; // empty: "New Category"
        int after;           // -1: 3
        int before;          // -1: 23
        QString name;
        Expected exp;
    };
    const std::vector<Row> rows = {
        {"empty name", {}, -1, -1, {}, {u"New Category"_s, true, 3, 23}},
        {"trailing backslash", {}, -1, -1, u"Trips\\"_s, {u"Trips\\New Category"_s, true, 3, 23}},
        {"non-ASCII prefix", {}, -1, -1, u"Город\\"_s, {u"Город\\New Category"_s, true, 3, 23}},
        {"plain name", {}, -1, -1, u"Trips"_s, {u"Trips"_s, true, 3, 23}},
        {"changed default and scales", u"Новая"_s, 7, 11, {}, {u"Новая"_s, true, 7, 11}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        QTemporaryDir dir;
        expectCategory(makeFactory(dir, row.defaultName, row.after, row.before).createNew(row.name), row.exp);
    }
}

// modify (:106-132): "" -> default name; no backslash rule; the source is not read.
TEST(MarkCategoryFactory, Modify) {
    struct Row {
        const char* label;
        QString defaultName;
        QString name;
        bool visible;
        int after;
        int before;
        Expected exp;
    };
    const std::vector<Row> rows = {
        {"empty name takes the default", {}, {}, false, 1, 2, {u"New Category"_s, false, 1, 2}},
        {"empty name takes the changed default", u"Новая"_s, {}, true, 4, 6, {u"Новая"_s, true, 4, 6}},
        {"name and arguments", {}, u"Город"_s, true, 4, 6, {u"Город"_s, true, 4, 6}},
        {"trailing backslash kept", {}, u"Trips\\"_s, false, 5, 9, {u"Trips\\"_s, false, 5, 9}},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.label);
        QTemporaryDir dir;
        const auto source = std::make_shared<const m::MarkCategory>(u"Source"_s, !row.visible, 8, 12);
        expectCategory(
            makeFactory(dir, row.defaultName).modify(source, row.name, row.visible, row.after, row.before),
            row.exp);
    }
}

// modifyVisible (:134-146): name and both scales from the source, visibility from the argument.
TEST(MarkCategoryFactory, ModifyVisible) {
    QTemporaryDir dir;
    const auto factory = makeFactory(dir);
    expectCategory(
        factory.modifyVisible(std::make_shared<const m::MarkCategory>(u"Город\\Trips"_s, false, 5, 9), true),
        {u"Город\\Trips"_s, true, 5, 9});
    expectCategory(
        factory.modifyVisible(std::make_shared<const m::MarkCategory>(u"Trips"_s, true, 8, 12), false),
        {u"Trips"_s, false, 8, 12});
}

// D-95 (T-0091): legacy fails with an access violation outside any handler: a nil config at the first use
// (u_MarkCategoryFactory.pas:78,116), a nil source at ASource.Name (:141). The port throws.
TEST(MarkCategoryFactory, NilArgumentsAreRefused) {
    EXPECT_THROW(m::MarkCategoryFactory(nullptr), std::invalid_argument);
    QTemporaryDir dir;
    EXPECT_THROW(static_cast<void>(makeFactory(dir).modifyVisible(nullptr, true)), std::invalid_argument);
}
