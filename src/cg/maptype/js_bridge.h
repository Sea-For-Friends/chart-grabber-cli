// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing (NEW code, ADR-0014 point 4). The one QObject a JavaScript builder hands to its fixed
// prelude as a function argument (never a global): the prelude builds the frozen host objects of spec §5.2 /
// §5.4 over its Q_INVOKABLE methods. Every method returns a list [ok, value]; the prelude throws the text of
// a failed one, so no C++ exception and no engine type crosses this class. Used on the thread of its engine
// only.

#include "cg/maptype/js_script_watchdog.h"
#include "cg/maptype/js_shared_store.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace cg::maptype {

struct JsHttpReply {
    quint32 status = 0;
    QByteArray header;
    QByteArray body;
};

// What the bridge needs from the request builder that owns it.
class IJsBridgeHost {
public:
    virtual ~IJsBridgeHost() = default;
    [[nodiscard]] virtual QString templateToUrl(const QString& tmpl) = 0;
    [[nodiscard]] virtual JsHttpReply httpRequest(const QByteArray& url, const QByteArray& requestHeader,
                                                  const QByteArray& postData) = 0;
    virtual void log(const QString& text) = 0;
    [[nodiscard]] virtual qint32 random(qint32 upperExclusive) = 0;
};

class JsBridge final : public QObject {
    Q_OBJECT
public:
    JsBridge(IJsBridgeHost& host, std::shared_ptr<JsSharedStore> shared,
             std::shared_ptr<JsScriptWatchdog::Entry> clock);
    ~JsBridge() override;

    // template() and http exist inside getUrl only (spec §5.2).
    void setInGetUrl(bool inGetUrl) { inGetUrl_ = inGetUrl; }
    // Defensive: whether an interrupted script runs the finally of shared.locked depends on the Qt version;
    // the builder calls this after every run.
    void releaseLock();

    Q_INVOKABLE QVariantList tmpl(const QString& text);
    Q_INVOKABLE QVariantList http(const QString& url, const QString& headers, const QString& body);
    Q_INVOKABLE void log(const QString& text);
    Q_INVOKABLE QVariantList sharedGet(const QString& key);
    Q_INVOKABLE void sharedSet(const QString& key, const QString& value);
    Q_INVOKABLE QVariantList lockBegin();
    Q_INVOKABLE void lockEnd();
    Q_INVOKABLE QVariantList round(double x);
    Q_INVOKABLE QVariantList roundEx(double x, double digits);
    Q_INVOKABLE QVariantList div(double a, double b);
    Q_INVOKABLE QVariantList mod(double a, double b);
    Q_INVOKABLE QVariantList regExprGetMatchSubStr(const QString& s, const QString& re, double index);
    Q_INVOKABLE QVariantList regExprReplaceMatchSubStr(const QString& s, const QString& re,
                                                       const QString& repl);
    Q_INVOKABLE double random(double n);
    Q_INVOKABLE double randomRange(double a, double b);

private:
    IJsBridgeHost& host_;
    std::shared_ptr<JsSharedStore> shared_;
    std::shared_ptr<JsScriptWatchdog::Entry> clock_;
    bool inGetUrl_ = false;
    bool lockHeld_ = false;
};

} // namespace cg::maptype
