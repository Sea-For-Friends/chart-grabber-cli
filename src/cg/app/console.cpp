// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/app/console.h"

#include "cg/core/platform/terminal.h"

#include <chrono>
#include <utility>

namespace cg::app {

namespace {

constexpr qint64 kProgressIntervalMs = 30000;

qint64 steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void put(std::FILE* stream, const QString& text) {
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stream);
}

} // namespace

Console::Console(int verbosity, bool progress, std::FILE* out, std::FILE* err, std::function<qint64()> nowMs,
                 bool forceTerminal)
    : verbosity_(verbosity), progress_(progress), out_(out), err_(err),
      nowMs_(nowMs ? std::move(nowMs) : std::function<qint64()>(steadyNowMs)),
      terminal_(progress && (forceTerminal || core::platform::isTerminal(err))) {
    core::platform::prepareOutputStream(out_);
    core::platform::prepareOutputStream(err_);
}

void Console::endProgressLocked() {
    if (showing_) {
        put(err_, QStringLiteral("\n"));
        std::fflush(err_);
        showing_ = false;
        lastLength_ = 0;
    }
}

void Console::writeLine(std::FILE* stream, const QString& text) {
    endProgressLocked();
    put(stream, text);
    put(stream, QStringLiteral("\n"));
    std::fflush(stream);
}

void Console::result(const QString& line) {
    const std::scoped_lock lock(mutex_);
    writeLine(out_, line);
}

void Console::warning(const QString& text) {
    if (verbosity_ < 0) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    writeLine(err_, QStringLiteral("warning: ") + text);
}

void Console::error(const QString& text) {
    const std::scoped_lock lock(mutex_);
    writeLine(err_, QStringLiteral("error: ") + text);
}

void Console::notice(const QString& line) {
    const std::scoped_lock lock(mutex_);
    writeLine(err_, line);
}

void Console::log(const QString& line) {
    if (verbosity_ <= 0) {
        return;
    }
    const std::scoped_lock lock(mutex_);
    writeLine(err_, line);
}

void Console::progress(const QString& line) {
    if (!progress_ || records_ || verbosity_ < 0) { // -q: errors and results only (app-cli.md 1.2)
        return;
    }
    const std::scoped_lock lock(mutex_);
    if (terminal_) {
        QString text = line;
        if (text.size() < lastLength_) {
            text += QString(lastLength_ - text.size(), QLatin1Char(' '));
        }
        put(err_, QStringLiteral("\r") + text);
        std::fflush(err_);
        lastLength_ = text.size();
        showing_ = true;
        return;
    }
    const qint64 now = nowMs_();
    if (printedAny_ && now - lastPrintedMs_ < kProgressIntervalMs) {
        pending_ = (line == lastLine_) ? QString()
                                       : line; // T-0267: never repeat the printed line; kept for endProgress:
                                               // the run may end inside the 30 s window (T-0266)
        return;
    }
    put(err_, line + QStringLiteral("\n"));
    std::fflush(err_);
    printedAny_ = true;
    lastPrintedMs_ = now;
    lastLine_ = line;
    pending_.clear();
}

void Console::setProgressRecords(bool on) {
    records_ = on;
}

void Console::progressRecord(const QString& line) {
    const std::scoped_lock lock(mutex_);
    endProgressLocked();
    const QByteArray bytes = (line + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), err_);
    std::fflush(err_);
}

void Console::endProgress() {
    const std::scoped_lock lock(mutex_);
    endProgressLocked();
    if (!pending_.isEmpty()) { // non-terminal only: a terminal never sets pending_ (T-0266)
        put(err_, pending_ + QStringLiteral("\n"));
        std::fflush(err_);
        lastPrintedMs_ = nowMs_();
        lastLine_ = pending_;
        pending_.clear();
    }
}

} // namespace cg::app
