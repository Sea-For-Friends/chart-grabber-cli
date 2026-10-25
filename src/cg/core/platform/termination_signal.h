// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing.
// New code (no legacy unit). Inherits: frm_ProgressDownload.pas FormClose (:586-595), where closing the
// window calls FCancelNotifier.NextOperation; here SIGINT / SIGTERM play the role of closing the window and
// request a stop.

#include <cstdint>
#include <stop_token>
#include <thread>

namespace cg::core::platform {

// T-0335: the signal number a Windows console event stands for (SIGINT for CTRL_C / CTRL_BREAK, SIGTERM for
// CTRL_CLOSE / LOGOFF / SHUTDOWN), 0 for an event the handler leaves alone. `event` is a DWORD; the function
// is compiled everywhere (the event numbers are fixed by winbase.h), only the handler that calls it is
// Windows code.
[[nodiscard]] int signalForConsoleEvent(unsigned long event);

// T-0332: the verdict of TerminationSignal::watchStdin.
enum class StdinWatch : std::uint8_t {
    Started, // the reader thread runs
    NotPipe, // the descriptor is a valid handle but not a pipe (a file, /dev/null, a terminal): refused
    Invalid  // the descriptor is not a valid handle: the option is disabled
};

// One object per process, owned by `main` (RAII: no thread in a static). A second live object throws
// std::logic_error. The second signal ends the process inside the handler with std::_Exit(128 + signal
// number), unless it arrives within 100 ms of the first (T-0262: the same delivery repeated, as by
// `timeout`, which signals the child and then its process group); such a signal is dropped.
class TerminationSignal final {
public:
    TerminationSignal();  // sigaction SIGINT / SIGTERM; starts the 100 ms watcher thread
    ~TerminationSignal(); // stops and joins the watcher, restores the old actions
    TerminationSignal(const TerminationSignal&) = delete;
    TerminationSignal& operator=(const TerminationSignal&) = delete;

    [[nodiscard]] std::stop_token token() const; // stopped on the first signal
    [[nodiscard]] int exitCode() const;          // 130, 143, or 0 when no signal arrived

    // T-0332 (--stop-on-stdin): reads `fd` (0 = stdin) on a detached thread. The exact line "stop" (an
    // optional trailing '\r'), EOF or a read error do what SIGTERM does (token stopped, exitCode() 143);
    // other input is ignored. The thread only sets the file-scope atomics the signal handler sets, so it is
    // harmless after this object is gone. A descriptor that is not a pipe is refused (so `< /dev/null` cannot
    // stop at once). The thread never closes `fd`. Call at most once.
    [[nodiscard]] StdinWatch watchStdin(int fd = 0);
    [[nodiscard]] bool stoppedByStdin() const; // the stop came from stdin, not from a signal

private:
    std::stop_source stopSource_;
    std::jthread watcher_;
};

} // namespace cg::core::platform
