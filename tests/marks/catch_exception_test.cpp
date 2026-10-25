// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Covers: u_MarkSystemErrorHandler.pas (CatchException)
#include "cg/core/notifier.h"
#include "cg/marks/mark_system_error_handler.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

namespace m = cg::marks;

namespace {

static_assert(noexcept(m::catchException(std::declval<cg::core::Notifier&>())));

QStringList g_messages;

void collect(QtMsgType, const QMessageLogContext&, const QString& msg) {
    g_messages.push_back(msg);
}

class CaptureLog {
public:
    CaptureLog() : previous_(qInstallMessageHandler(collect)) { g_messages.clear(); }
    ~CaptureLog() { qInstallMessageHandler(previous_); }
    CaptureLog(const CaptureLog&) = delete;
    CaptureLog& operator=(const CaptureLog&) = delete;

private:
    QtMessageHandler previous_;
};

TEST(CatchException, NonStandardExceptionIsLoggedAndNotified) {
    CaptureLog log;
    cg::core::Notifier errors;
    int fired = 0;
    const auto sub = errors.subscribe([&] { ++fired; });
    try {
        throw 42;
    } catch (...) {
        m::catchException(errors);
    }
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(g_messages.size(), 1);
}

} // namespace
