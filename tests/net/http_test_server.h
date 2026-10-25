// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing -- a test fixture only. docs/specs/net.md §7.0.
//
// A scripted HTTP/1.1 server, in-process, bound to 127.0.0.1:0 (the OS picks a free port; read it
// back via port()). Chosen over a subprocess server because it needs no external process, can
// emit deliberately malformed responses (a wrong Content-Length, an unknown Content-Encoding, a
// body with no Content-Type) a real server would not produce, starts in microseconds, and leaves
// no PID to leak on a failing test.
//
// Runs entirely on its own std::jthread using blocking Qt socket calls
// (QTcpServer::waitForNewConnection, QTcpSocket::waitForReadyRead/waitForBytesWritten) -- no Qt
// event loop is required anywhere, including in the calling test.

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QPair>

#include <future>
#include <thread>

class QTcpSocket; // global namespace, like every Qt class

namespace cg::net::test {

class HttpTestServer {
public:
    struct Reply {
        int status = 200;
        QByteArray reason = "OK";
        QList<QPair<QByteArray, QByteArray>> headers; // written verbatim, in order
        QByteArray body;
        bool omitContentLength = false;
        bool closeWithoutResponse = false; // for connection-reset tests
        int delayMs = 0;                   // for timeout/cancel tests -- applied before the response
    };

    HttpTestServer() = default;
    // NOT "= default": std::jthread's own destructor requests-stop-and-joins, but that only runs
    // AFTER this class's other members (replies_/cursor_/requestLog_/mutex_) are already
    // destroyed, since worker_ is declared before them (reverse declaration order) -- run() can
    // still be mid-handleConnection(), touching those members, when that happens. ThreadSanitizer
    // catches it as a real data race. Stop and join explicitly first instead.
    ~HttpTestServer();
    HttpTestServer(const HttpTestServer&) = delete;
    HttpTestServer& operator=(const HttpTestServer&) = delete;

    [[nodiscard]] bool start(); // listens on 127.0.0.1:0; blocks until listening (or failed)
    [[nodiscard]] quint16 port() const noexcept { return port_; }
    [[nodiscard]] QByteArray url(QByteArrayView path) const;

    // Route by exact path (scheme+host already stripped in proxy mode). A path may be given
    // several replies; served in order, then the last one repeats -- how the redirect-loop and
    // retry-style tests work.
    void addReply(QByteArrayView path, Reply reply);
    // T-0271: forgets every reply and cursor (the log stays), so a test can serve the same paths differently.
    void clearReplies();

    // Accepts absolute-form request targets ("GET http://host/path HTTP/1.1") and, on the FIRST
    // request after being turned on, replies 407 with Proxy-Authenticate: Basic realm="test"
    // instead of consulting addReply().
    void setProxyMode(bool on);

    [[nodiscard]] QList<QByteArray> requestLog() const; // full raw request blobs, in order
    // The number of requests so far; no copy of the log, so a poll loop shares no array with the server.
    [[nodiscard]] qsizetype requestCount() const;

private:
    void run(std::stop_token stopToken, std::promise<bool> ready);
    void handleConnection(QTcpSocket& socket);
    static void writeResponse(QTcpSocket& socket, const Reply& reply, bool suppressBody);

    std::jthread worker_;
    quint16 port_ = 0; // written once on worker_ before start()'s promise resolves; safe to read after

    mutable QMutex mutex_; // guards everything below
    QHash<QByteArray, QList<Reply>> replies_;
    QHash<QByteArray, int> cursor_;
    QList<QByteArray> requestLog_;
    bool proxyMode_ = false;
    bool challengeSent_ = false;
};

} // namespace cg::net::test
