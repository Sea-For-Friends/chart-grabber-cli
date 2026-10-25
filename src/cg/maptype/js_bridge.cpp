// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/js_bridge.h"

#include "cg/core/delphi_format.h"
#include "cg/net/file_url.h"
#include "cg/script/regex_compat.h"

#include <QVariantMap>

#include <cmath>
#include <exception>
#include <limits>

namespace cg::maptype {

namespace {

QVariantList ok(const QVariant& value = {}) {
    return {true, value};
}

QVariantList fail(const QString& message) {
    return {false, message};
}

// ECMAScript ToInt32: the 32-bit integer a JavaScript number is truncated to (spec 5.4: div, mod, random).
qint32 toInt32(double d) {
    if (!std::isfinite(d)) {
        return 0;
    }
    double m = std::fmod(std::trunc(d), 4294967296.0);
    if (m < 0) {
        m += 4294967296.0;
    }
    return static_cast<qint32>(static_cast<quint32>(m));
}

QString onlyInGetUrl(const char* name) {
    return QString::fromLatin1(name) + QStringLiteral(" is only available inside getUrl");
}

} // namespace

JsBridge::JsBridge(IJsBridgeHost& host, std::shared_ptr<JsSharedStore> shared,
                   std::shared_ptr<JsScriptWatchdog::Entry> clock)
    : host_(host), shared_(std::move(shared)), clock_(std::move(clock)) {}

JsBridge::~JsBridge() {
    releaseLock();
}

void JsBridge::releaseLock() {
    if (lockHeld_) {
        lockHeld_ = false;
        shared_->unlock();
    }
}

QVariantList JsBridge::tmpl(const QString& text) {
    if (!inGetUrl_) {
        return fail(onlyInGetUrl("template"));
    }
    try {
        return ok(host_.templateToUrl(text));
    } catch (const std::exception& e) {
        return fail(QString::fromUtf8(e.what()));
    }
}

QVariantList JsBridge::http(const QString& url, const QString& headers, const QString& body) {
    if (!inGetUrl_) {
        return fail(onlyInGetUrl("http"));
    }
    try {
        clock_->pause(); // the time of the request is not script time
        const JsHttpReply reply = host_.httpRequest(url.toUtf8(), headers.toUtf8(), body.toUtf8());
        clock_->resume();
        QVariantMap map;
        map.insert(QStringLiteral("status"), static_cast<double>(reply.status));
        map.insert(QStringLiteral("headers"), QString::fromLatin1(reply.header));
        map.insert(QStringLiteral("body"), QString::fromUtf8(reply.body));
        return ok(map);
    } catch (const std::exception& e) {
        clock_->resume();
        return fail(QString::fromUtf8(e.what()));
    }
}

void JsBridge::log(const QString& text) {
    host_.log(text);
}

QVariantList JsBridge::sharedGet(const QString& key) {
    const std::optional<QString> value = shared_->get(key);
    return ok(value.has_value() ? QVariant(*value) : QVariant()); // absent: undefined
}

void JsBridge::sharedSet(const QString& key, const QString& value) {
    shared_->set(key, value);
}

QVariantList JsBridge::lockBegin() {
    if (lockHeld_) { // the lock is non-recursive, as Pascal's (ADR-0014)
        return fail(QStringLiteral("shared.locked is not re-entrant"));
    }
    clock_->pause(); // waiting for another worker's token fetch is not script time
    shared_->lock();
    clock_->resume();
    lockHeld_ = true;
    return ok();
}

void JsBridge::lockEnd() {
    releaseLock();
}

// The helpers below are thin bindings of the ones the Pascal path uses (spec 5.4).
QVariantList JsBridge::round(double x) {
    if (!std::isfinite(x) || std::fabs(x) >= 9.2e18) { // the cast to qint64 would be undefined
        return fail(QStringLiteral("pascal.round: argument out of range"));
    }
    return ok(static_cast<double>(core::delphiRound(x)));
}

QVariantList JsBridge::roundEx(double x, double digits) {
    return ok(QString::fromLatin1(core::roundEx(x, toInt32(digits))));
}

QVariantList JsBridge::div(double a, double b) {
    const qint32 divisor = toInt32(b);
    if (divisor == 0) {
        return fail(QStringLiteral("Division by zero"));
    }
    return ok(static_cast<double>(static_cast<qint32>(static_cast<qint64>(toInt32(a)) / divisor)));
}

QVariantList JsBridge::mod(double a, double b) {
    const qint32 divisor = toInt32(b);
    if (divisor == 0) {
        return fail(QStringLiteral("Division by zero"));
    }
    return ok(static_cast<double>(static_cast<qint32>(static_cast<qint64>(toInt32(a)) % divisor)));
}

QVariantList JsBridge::regExprGetMatchSubStr(const QString& s, const QString& re, double index) {
    const QByteArray pattern = re.toUtf8();
    const script::CompiledRegex compiled = script::compileTRegExpr(pattern);
    if (!compiled.ok) { // R16
        return fail(compiled.error);
    }
    return ok(QString::fromUtf8(script::tRegExprMatch(s.toUtf8(), pattern, toInt32(index))));
}

QVariantList JsBridge::regExprReplaceMatchSubStr(const QString& s, const QString& re, const QString& repl) {
    const QByteArray pattern = re.toUtf8();
    const script::CompiledRegex compiled = script::compileTRegExpr(pattern);
    if (!compiled.ok) { // R16
        return fail(compiled.error);
    }
    return ok(QString::fromUtf8(script::tRegExprReplace(s.toUtf8(), pattern, repl.toUtf8())));
}

double JsBridge::random(double n) {
    return static_cast<double>(host_.random(toInt32(n)));
}

// Same arithmetic as BuiltinId::RandomRange of the interpreter: [min, max), equal bounds give the bound.
double JsBridge::randomRange(double a, double b) {
    const qint64 from = toInt32(a);
    const qint64 to = toInt32(b);
    const qint64 low = from < to ? from : to;
    const qint64 span = from < to ? to - from : from - to;
    const qint32 r = span <= 0 ? 0 : host_.random(static_cast<qint32>(span));
    return static_cast<double>(r + low);
}

} // namespace cg::maptype
