// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/Download/frm_ProgressDownload.pas:166-167, 251-261, 316-321, 395-437, 451-577,
// 586-595
//        (headless half; the form goes to ui)

#include "cg/core/operation_notifier.h"
#include "cg/geo/value_to_string.h"
#include "cg/region/region_process_progress_info_download.h"

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

namespace cg::region {

// :316-321. Round is Delphi's (ties to even).
[[nodiscard]] QString percentText(qint64 total, qint64 processed);
// GetLenEnd :395-408.
[[nodiscard]] QString remainingSize(qint64 total, qint64 processed, qint64 downloaded, double downloadedKb,
                                    const geo::ValueToStringConverter& converter);
// GetTimeEnd :410-429.
[[nodiscard]] QString remainingTime(qint64 total, qint64 processed, double elapsedDays);

// The session state of TfrmProgressDownload, without the form. The owner calls checkSessionAutosave() once
// a second, before it updates its view (UpdateTimerTimer :431-437). Legacy also shows an error box when the
// first autosave fails (:570-572); the box has no headless meaning, the port only writes the memo line.
class ProgressDownload {
public:
    // `now` is legacy Now, `sleep` is legacy Sleep(ms); empty = the wall clock / a real sleep.
    ProgressDownload(std::shared_ptr<IRegionProcessProgressInfoDownload> progressInfo,
                     std::shared_ptr<core::OperationNotifier> cancelNotifier, QString autoSaveDir,
                     std::function<void(const QString&)> memo, std::function<QDateTime()> now = {},
                     std::function<void(int ms)> sleep = {});
    ~ProgressDownload(); // :251-261

    // FEAT-REG-016
    void checkSessionAutosave(); // :472-577
    // FEAT-REG-017
    void saveSession(const QString& targetFileName); // DoSaveSession :451-470
    void close();                                    // FormClose :586-595: cancel + delete the .bk
    [[nodiscard]] QString sessionFileName() const;   // the newest autosave file, empty if none

private:
    [[nodiscard]] bool createFileUnique(QString& fileName); // _CreateFileUnique :474-527

    std::shared_ptr<IRegionProcessProgressInfoDownload> progressInfo_;
    std::shared_ptr<core::OperationNotifier> cancelNotifier_;
    QString autoSaveDir_;
    std::function<void(const QString&)> memo_;
    std::function<QDateTime()> now_;
    std::function<void(int)> sleep_;
    bool isSessionInitialized_ = false;
    QString sessionPrefix_;
    QString sessionFileName_;
    QString sessionFileNameBack_;
    double lastElapsedTime_ = 0;
};

} // namespace cg::region
