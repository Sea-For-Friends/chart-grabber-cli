// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: nothing -- a test fixture only. docs/specs/net.md §7.0.
#include "http_test_server.h"

#include <QHostAddress>
#include <QMutexLocker>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <algorithm>
#include <memory>
#include <utility>

namespace cg::net::test {

namespace {
constexpr int kIoTimeoutMs = 5000;
}

HttpTestServer::~HttpTestServer() {
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join(); // run() has fully returned; no other member is touched by it any more
    }
}

bool HttpTestServer::start() {
    std::promise<bool> ready;
    std::future<bool> readyFuture = ready.get_future();
    worker_ = std::jthread([this, ready = std::move(ready)](std::stop_token stopToken) mutable {
        run(stopToken, std::move(ready));
    });
    return readyFuture.get();
}

QByteArray HttpTestServer::url(QByteArrayView path) const {
    return "http://127.0.0.1:" + QByteArray::number(port_) + path.toByteArray();
}

void HttpTestServer::addReply(QByteArrayView path, Reply reply) {
    const QMutexLocker locker(&mutex_);
    replies_[path.toByteArray()].append(std::move(reply));
}

void HttpTestServer::clearReplies() {
    const QMutexLocker locker(&mutex_);
    replies_.clear();
    cursor_.clear();
}

void HttpTestServer::setProxyMode(bool on) {
    const QMutexLocker locker(&mutex_);
    proxyMode_ = on;
    challengeSent_ = false;
}

QList<QByteArray> HttpTestServer::requestLog() const {
    const QMutexLocker locker(&mutex_);
    return requestLog_;
}

qsizetype HttpTestServer::requestCount() const {
    const QMutexLocker locker(&mutex_);
    return requestLog_.size();
}

void HttpTestServer::run(std::stop_token stopToken, std::promise<bool> ready) {
    QTcpServer server;
    const bool listening = server.listen(QHostAddress::LocalHost, 0);
    if (listening) {
        port_ = server.serverPort();
    }
    ready.set_value(listening);
    if (!listening) {
        return;
    }

    while (!stopToken.stop_requested()) {
        if (!server.waitForNewConnection(100)) {
            continue;
        }
        while (server.hasPendingConnections()) {
            std::unique_ptr<QTcpSocket> socket(server.nextPendingConnection());
            handleConnection(*socket);
        }
    }
}

void HttpTestServer::handleConnection(QTcpSocket& socket) {
    QByteArray buf;
    while (!buf.contains("\r\n\r\n")) {
        if (!socket.waitForReadyRead(kIoTimeoutMs)) {
            return; // nothing usable arrived; drop the connection
        }
        buf += socket.readAll();
    }
    const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
    const QByteArray headerBlob = buf.left(headerEnd + 4); // request line + headers + trailing blank line
    QByteArray body = buf.mid(headerEnd + 4);              // any body bytes read alongside the headers

    const qsizetype firstCrlf = headerBlob.indexOf("\r\n");
    const QByteArray requestLine = firstCrlf >= 0 ? headerBlob.left(firstCrlf) : headerBlob;
    const QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 2) {
        return; // not a request we understand
    }
    const QByteArray method = requestParts.at(0);
    QByteArray target = requestParts.at(1);

    bool proxyMode = false;
    {
        const QMutexLocker locker(&mutex_);
        proxyMode = proxyMode_;
    }
    if (proxyMode) {
        // "GET http://host/path HTTP/1.1" -- strip scheme+authority, keep the path.
        const qsizetype schemeEnd = target.indexOf("://");
        if (schemeEnd >= 0) {
            const qsizetype pathStart = target.indexOf('/', schemeEnd + 3);
            target = pathStart >= 0 ? target.mid(pathStart) : QByteArray("/");
        }
    }

    const QByteArray upperHeaders = headerBlob.toUpper();
    const qsizetype clIdx = upperHeaders.indexOf("CONTENT-LENGTH:");
    qint64 contentLength = 0;
    if (clIdx >= 0) {
        qsizetype valueStart = clIdx + 15;
        while (valueStart < headerBlob.size() && headerBlob.at(valueStart) == ' ') {
            ++valueStart;
        }
        qsizetype valueEnd = headerBlob.indexOf("\r\n", valueStart);
        if (valueEnd < 0) {
            valueEnd = headerBlob.size();
        }
        contentLength = headerBlob.mid(valueStart, valueEnd - valueStart).toLongLong();
    }
    while (body.size() < contentLength) {
        if (!socket.waitForReadyRead(kIoTimeoutMs)) {
            break;
        }
        body += socket.readAll();
    }

    bool sendChallenge = false;
    {
        const QMutexLocker locker(&mutex_);
        requestLog_.append(headerBlob);
        if (proxyMode_ && !challengeSent_) {
            challengeSent_ = true;
            sendChallenge = true;
        }
    }

    if (sendChallenge) {
        Reply challenge;
        challenge.status = 407;
        challenge.reason = "Proxy Authentication Required";
        challenge.headers.append(QPair<QByteArray, QByteArray>{QByteArrayLiteral("Proxy-Authenticate"),
                                                               QByteArrayLiteral("Basic realm=\"test\"")});
        writeResponse(socket, challenge, method == "HEAD");
        socket.disconnectFromHost();
        socket.waitForDisconnected(kIoTimeoutMs);
        return;
    }

    Reply reply;
    {
        const QMutexLocker locker(&mutex_);
        const auto it = replies_.constFind(target);
        if (it == replies_.constEnd() || it->isEmpty()) {
            reply.status = 404;
            reply.reason = "Not Found";
        } else {
            int& cursor = cursor_[target];
            const int index = std::min(cursor, static_cast<int>(it->size()) - 1);
            reply = it->at(index);
            if (cursor < it->size() - 1) {
                ++cursor;
            }
        }
    }

    if (reply.delayMs > 0) {
        QThread::msleep(static_cast<unsigned long>(reply.delayMs));
    }
    if (reply.closeWithoutResponse) {
        socket.disconnectFromHost();
        socket.waitForDisconnected(kIoTimeoutMs);
        return;
    }

    writeResponse(socket, reply, method == "HEAD");
    socket.disconnectFromHost();
    socket.waitForDisconnected(kIoTimeoutMs);
}

void HttpTestServer::writeResponse(QTcpSocket& socket, const Reply& reply, bool suppressBody) {
    QByteArray out = "HTTP/1.1 " + QByteArray::number(reply.status) + ' ' + reply.reason + "\r\n";
    bool hasContentLength = false;
    for (const auto& header : reply.headers) {
        out += header.first + ": " + header.second + "\r\n";
        if (header.first.compare("Content-Length", Qt::CaseInsensitive) == 0) {
            hasContentLength = true;
        }
    }
    if (!reply.omitContentLength && !hasContentLength) {
        out += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n";
    }
    out += "\r\n";
    if (!suppressBody) {
        out += reply.body;
    }
    socket.write(out);
    socket.waitForBytesWritten(kIoTimeoutMs);
}

} // namespace cg::net::test
