// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/platform/termination_signal.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cg::core::platform {

int signalForConsoleEvent(unsigned long event) {
    constexpr unsigned long kCtrlC = 0;
    constexpr unsigned long kCtrlBreak = 1;
    constexpr unsigned long kCtrlClose = 2;
    constexpr unsigned long kCtrlLogoff = 5;
    constexpr unsigned long kCtrlShutdown = 6;
    switch (event) {
    case kCtrlC:
    case kCtrlBreak:
        return SIGINT;
    case kCtrlClose:
    case kCtrlLogoff:
    case kCtrlShutdown:
        return SIGTERM;
    default:
        return 0;
    }
}

namespace {

// TL-3 (app-cli.md 6.1): the one global of the program; a signal handler can reach nothing else
std::atomic<int> receivedSignal{0};
static_assert(std::atomic<int>::is_always_lock_free, "signal handler needs a lock-free atomic");

// T-0262: a second signal within this window of the first is the same delivery repeated (`timeout` signals
// the child and then its process group; `kill -INT -PGID`; a terminal's Ctrl-C reaches every process of the
// group), not a user's second Ctrl-C: it is dropped. Later ones end the process (app-cli.md 6.1).
constexpr std::int64_t kSameDeliveryNs = 100'000'000;
std::atomic<std::int64_t> firstSignalAtNs{0};
static_assert(std::atomic<std::int64_t>::is_always_lock_free, "signal handler needs a lock-free atomic");

std::int64_t monotonicNs() {
#ifdef _WIN32
    // T-0335: the Windows handler runs on its own thread (not in signal context), so the C++ clock is fine.
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
#else
    timespec now{}; // clock_gettime is async-signal-safe
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1'000'000'000 + now.tv_nsec;
#endif
}

// T-0332: the stdin reader's side. stdinGeneration is bumped by the destructor, so a reader that outlives its
// object (it cannot be woken from a blocked read) sets nothing.
std::atomic<unsigned> stdinGeneration{0};
std::atomic<bool> stoppedFromStdin{false};

std::atomic<bool> objectAlive{
    false}; // guards "one object at a time"; set by the constructor / destructor, read by onConsoleEvent
#ifdef _WIN32
using SignalHandler = void (*)(int);
SignalHandler oldIntHandler = SIG_DFL;
SignalHandler oldTermHandler = SIG_DFL;
#else
struct sigaction oldIntAction{};
struct sigaction oldTermAction{};
#endif

extern "C" void onSignal(int signum) {
#ifdef _WIN32
    std::signal(
        signum,
        &onSignal); // the CRT resets a handler to SIG_DFL when it runs: a second signal would end the process
#endif
    if (receivedSignal.load(std::memory_order_acquire) == 0) {
        firstSignalAtNs.store(monotonicNs(), std::memory_order_relaxed);
        receivedSignal.store(signum, std::memory_order_release);
    } else if (monotonicNs() - firstSignalAtNs.load(std::memory_order_relaxed) >= kSameDeliveryNs) {
        std::_Exit(128 + signum);
    }
}

// The stdin stop is the first SIGTERM: same flag, same time stamp for the "same delivery" window.
void stopFromStdin(unsigned generation) {
    if (stdinGeneration.load(std::memory_order_acquire) != generation) {
        return;
    }
    if (receivedSignal.load(std::memory_order_acquire) != 0) {
        return; // a signal came first
    }
    stoppedFromStdin.store(true, std::memory_order_release);
    firstSignalAtNs.store(monotonicNs(), std::memory_order_relaxed);
    int expected = 0;
    if (!receivedSignal.compare_exchange_strong(expected, SIGTERM, std::memory_order_acq_rel)) {
        stoppedFromStdin.store(false, std::memory_order_release); // a signal won the race
    }
}

constexpr std::size_t kMaxLine = 64; // longer lines cannot be "stop": they are junk until the next newline

#ifdef _WIN32
using StdinSource = HANDLE;
#else
using StdinSource = int;
#endif

// Blocking read of up to `size` bytes: > 0 data, 0 EOF or a read error (a broken pipe is the Windows EOF,
// T-0335), < 0 retry (EINTR).
std::ptrdiff_t readSome(StdinSource source, char* buffer, std::size_t size) {
#ifdef _WIN32
    DWORD got = 0;
    if (!::ReadFile(source, buffer, static_cast<DWORD>(size), &got, nullptr)) {
        return 0; // ERROR_BROKEN_PIPE (the writer closed) or any other error: stop, as on POSIX
    }
    return static_cast<std::ptrdiff_t>(got); // 0 bytes: EOF
#else
    const ssize_t n = ::read(source, buffer, size);
    if (n < 0 && errno == EINTR) {
        return -1;
    }
    return n < 0 ? 0 : n;
#endif
}

void readStdin(StdinSource source, unsigned generation) {
    std::string line;
    char buffer[256];
    for (;;) {
        const std::ptrdiff_t n = readSome(source, buffer, sizeof buffer);
        if (n < 0) {
            continue;
        }
        if (n == 0) { // EOF, or a broken pipe / closed descriptor
            stopFromStdin(generation);
            return;
        }
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            if (buffer[i] != '\n') {
                if (line.size() < kMaxLine) { // a longer line is cut: it can never equal "stop"
                    line.push_back(buffer[i]);
                }
                continue;
            }
            if (line == "stop" || line == "stop\r") {
                stopFromStdin(generation);
                return;
            }
            line.clear();
        }
    }
}

#ifdef _WIN32
// T-0335: the console events of a Windows process. The handler runs on a thread the system creates.
// CTRL_C / CTRL_BREAK are SIGINT (exit 130); CTRL_CLOSE / LOGOFF / SHUTDOWN are SIGTERM (exit 143).
constexpr DWORD kCloseGraceMs =
    4000; // the system ends the process when the handler returns (closing: after 5 s)

// T-0341: the handler is installed once and stays (see ~TerminationSignal); without a TerminationSignal
// object it is inert.
std::atomic<bool> consoleHandlerInstalled{false};

BOOL WINAPI onConsoleEvent(DWORD event) {
    if (!objectAlive.load(std::memory_order_acquire)) {
        return FALSE; // the default handling (the process ends)
    }
    const int signum = signalForConsoleEvent(event);
    if (signum == 0) {
        return FALSE;
    }
    onSignal(signum);
    if (signum == SIGTERM) {
        ::Sleep(kCloseGraceMs); // keep the process alive so the stop can save the session
    }
    return TRUE;
}

void noopInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {}
#endif

} // namespace

TerminationSignal::TerminationSignal() {
    if (objectAlive.exchange(true)) {
        throw std::logic_error("TerminationSignal: only one object may exist at a time");
    }
    receivedSignal = 0;
    stoppedFromStdin = false;
#ifdef _WIN32
    // std::raise (and abort-like paths of the CRT) reach the CRT handlers; the console events reach the ctrl
    // handler.
    oldIntHandler = std::signal(SIGINT, &onSignal);
    oldTermHandler = std::signal(SIGTERM, &onSignal);
    if (!consoleHandlerInstalled.exchange(true)) {
        ::SetConsoleCtrlHandler(&onConsoleEvent, TRUE);
    }
#else
    struct sigaction action{};
    action.sa_handler = &onSignal;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGINT, &action, &oldIntAction);
    ::sigaction(SIGTERM, &action, &oldTermAction);
#endif

    watcher_ = std::jthread([this](const std::stop_token& own) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        while (!own.stop_requested()) {
            if (receivedSignal.load(std::memory_order_acquire) != 0) {
                stopSource_.request_stop();
                return;
            }
            wake.wait_for(lock, own, std::chrono::milliseconds(100), [] { return false; });
        }
    });
}

TerminationSignal::~TerminationSignal() {
    stdinGeneration.fetch_add(1, std::memory_order_acq_rel);
    watcher_.request_stop();
    watcher_.join();
#ifdef _WIN32
    // T-0341: no SetConsoleCtrlHandler(..., FALSE) here: on CTRL_CLOSE the handler is inside its 4 s grace
    // sleep while the main thread ends the program, and the removal waits for the handler to return; the
    // system then ends the process with STATUS_CONTROL_C_EXIT (0xC000013A) instead of the exit code 143. The
    // handler is inert once objectAlive is false.
    std::signal(SIGINT, oldIntHandler);
    std::signal(SIGTERM, oldTermHandler);
#else
    ::sigaction(SIGINT, &oldIntAction, nullptr);
    ::sigaction(SIGTERM, &oldTermAction, nullptr);
#endif
    receivedSignal = 0;
    stoppedFromStdin = false;
    objectAlive = false;
}

std::stop_token TerminationSignal::token() const {
    return stopSource_.get_token();
}

int TerminationSignal::exitCode() const {
    const int signum = receivedSignal.load(std::memory_order_acquire);
    return signum == 0 ? 0 : 128 + signum;
}

bool TerminationSignal::stoppedByStdin() const {
    return stoppedFromStdin.load(std::memory_order_acquire);
}

StdinWatch TerminationSignal::watchStdin(int fd) {
#ifdef _WIN32
    // T-0335. _get_osfhandle on a bad descriptor calls the invalid-parameter handler, which ends the process
    // by default: silence it for this one call.
    const auto previous = _set_invalid_parameter_handler(&noopInvalidParameter);
    const intptr_t raw = _get_osfhandle(fd);
    _set_invalid_parameter_handler(previous);
    const HANDLE handle = reinterpret_cast<HANDLE>(raw); // NOLINT(performance-no-int-to-ptr)
    if (raw == -1 || raw == -2) {
        return StdinWatch::Invalid; // no handle (-2: a process without stdin)
    }
    ::SetLastError(NO_ERROR);
    const DWORD type = ::GetFileType(handle);
    if (type == FILE_TYPE_UNKNOWN && ::GetLastError() != NO_ERROR) {
        return StdinWatch::Invalid; // ERROR_INVALID_HANDLE: a handle the system rejects
    }
    if (type != FILE_TYPE_PIPE) {
        return StdinWatch::NotPipe;
    }
    _setmode(fd, _O_BINARY);
    const unsigned generation = stdinGeneration.load(std::memory_order_acquire);
    std::thread([handle, generation] { readStdin(handle, generation); }).detach();
    return StdinWatch::Started;
#else
    struct stat info{};
    if (::fstat(fd, &info) != 0) {
        return StdinWatch::Invalid;
    }
    if (!S_ISFIFO(info.st_mode)) {
        return StdinWatch::NotPipe;
    }
    const unsigned generation = stdinGeneration.load(std::memory_order_acquire);
    std::thread([fd, generation] { readStdin(fd, generation); }).detach();
    return StdinWatch::Started;
#endif
}

} // namespace cg::core::platform
