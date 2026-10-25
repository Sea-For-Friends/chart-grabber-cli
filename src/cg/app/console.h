// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
// New code (no legacy unit). Inherits: frm_ProgressDownload.pas - the window polls once a second
// (UpdateTimerTimer :431-437) and shows the log (UpdateMemoProgressForm :381-393); the console is its
// text-mode counterpart: results to stdout, messages and the progress line to stderr (app-cli.md 1.10).

#include <QString>
#include <QtGlobal>

#include <cstdio>
#include <functional>
#include <mutex>

namespace cg::app {

class Console final {
public:
    // nowMs: millisecond clock for the 30 s throttle (default steady clock). forceTerminal: treat err as a
    // terminal (a test seam; the production value false means core::platform::isTerminal(err) decides, once).
    Console(int verbosity, bool progress, std::FILE* out = stdout, std::FILE* err = stderr,
            std::function<qint64()> nowMs = {}, bool forceTerminal = false);

    void result(const QString& line);   // stdout
    void warning(const QString& text);  // stderr "warning: ", dropped with -q (verbosity < 0)
    void error(const QString& text);    // stderr "error: ", always
    void notice(const QString& line);   // stderr, the line as given, always (app-cli.md 1.6.1, 1.6.2)
    void log(const QString& line);      // stderr, only with verbosity > 0 (the log has no level)
    void progress(const QString& line); // rewritten in place on a terminal, else a full line every 30 s
    // endProgress ends a showing terminal line; on a non-terminal it also prints the last progress text the
    // 30 s throttle dropped, so a run shorter than 30 s still shows its final state (T-0266).
    void endProgress();
    // --progress=lines (T-0203): on, progress() prints nothing; progressRecord() writes whole records.
    // Set once before the first thread starts.
    void setProgressRecords(bool on);
    void progressRecord(const QString& line); // stderr: line + LF in one fwrite, then fflush

private:
    void endProgressLocked();
    void writeLine(std::FILE* stream, const QString& text);

    int verbosity_;
    bool progress_;
    std::FILE* out_;
    std::FILE* err_;
    std::function<qint64()> nowMs_;
    bool terminal_;
    bool records_ = false;
    std::mutex mutex_;
    qsizetype lastLength_ = 0; // length of the progress line showing on a terminal
    bool showing_ = false;     // a progress line is showing on a terminal
    bool printedAny_ = false;  // non-terminal: a progress line was printed
    qint64 lastPrintedMs_ = 0;
    QString lastLine_; // non-terminal: the last progress text printed (T-0267)
    QString pending_;  // non-terminal: the last progress text the throttle dropped (T-0266)
};

} // namespace cg::app
