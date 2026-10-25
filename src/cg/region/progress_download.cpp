// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// Ports: Src/RegionProcess/Download/frm_ProgressDownload.pas:166-167, 251-261, 316-321, 395-437, 451-577,
// 586-595 (headless half; the form goes to ui)
#include "cg/region/progress_download.h"

#include "cg/core/config_provider_ini.h"

#include <QDir>
#include <QFile>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cxxabi.h>
#include <stdexcept>
#include <thread>
#include <typeinfo>
#include <utility>

namespace cg::region {

namespace {

// E.ClassName + ': ' + E.Message; same form as the worker's exceptionText (thread_download_tiles.cpp).
QString exceptionText(const std::exception& e) {
    int status = 0;
    char* demangled = // NOLINT(cppcoreguidelines-owning-memory): C allocation, freed below
        abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
    const QString name =
        (status == 0 && demangled != nullptr) ? QString::fromLatin1(demangled) : QStringLiteral("Exception");
    std::free(demangled); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return name + QStringLiteral(": ") + QString::fromUtf8(e.what());
}

// ChangeFileExt(name, '.bk')
QString changeExtToBk(const QString& name) {
    const qsizetype slash = name.lastIndexOf(QLatin1Char('/'));
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const QString stem = (dot > slash) ? name.left(dot) : name;
    return stem + QStringLiteral(".bk");
}

} // namespace

QString percentText(qint64 total, qint64 processed) { // :316-321
    if (total > 0) {
        const double ratio = static_cast<double>(processed) / static_cast<double>(total) * 100;
        return QString::number(static_cast<qint64>(std::nearbyint(ratio))) + QLatin1Char('%');
    }
    return QStringLiteral("~%");
}

QString remainingSize(qint64 total, qint64 processed, qint64 downloaded, double downloadedKb,
                      const geo::ValueToStringConverter& converter) { // :395-408
    if (downloaded == 0) {
        return QStringLiteral("~ Kb");
    }
    return converter.dataSizeConvert(downloadedKb / static_cast<double>(downloaded) *
                                     static_cast<double>(total - processed));
}

QString remainingTime(qint64 total, qint64 processed, double elapsedDays) { // :410-429
    if (processed == 0) {
        return QStringLiteral("~");
    }
    const double expected = elapsedDays * (static_cast<double>(total) / static_cast<double>(processed));
    const double diff = expected - elapsedDays;
    QString result;
    const auto dd = static_cast<qint64>(std::trunc(std::fabs(diff))); // DaysBetween
    if (dd > 0) {
        result = QString::number(dd) + QStringLiteral(" days, ");
    }
    double whole = 0;
    const double frac = std::modf(diff, &whole);
    const auto ms = static_cast<qint64>(std::llround(std::fabs(frac) * 86400000.0));
    const qint64 hh = ms / 3600000;
    const qint64 nn = ms / 60000 % 60;
    const qint64 ss = ms / 1000 % 60;
    result += QStringLiteral("%1:%2:%3")
                  .arg(hh, 2, 10, QLatin1Char('0'))
                  .arg(nn, 2, 10, QLatin1Char('0'))
                  .arg(ss, 2, 10, QLatin1Char('0'));
    return result;
}

ProgressDownload::ProgressDownload(std::shared_ptr<IRegionProcessProgressInfoDownload> progressInfo,
                                   std::shared_ptr<core::OperationNotifier> cancelNotifier,
                                   QString autoSaveDir, std::function<void(const QString&)> memo,
                                   std::function<QDateTime()> now, std::function<void(int ms)> sleep)
    : progressInfo_(std::move(progressInfo)), cancelNotifier_(std::move(cancelNotifier)),
      autoSaveDir_(std::move(autoSaveDir)), memo_(std::move(memo)), now_(std::move(now)),
      sleep_(std::move(sleep)) {
    if (!now_) {
        now_ = [] { return QDateTime::currentDateTime(); };
    }
    if (!sleep_) {
        sleep_ = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    }
}

ProgressDownload::~ProgressDownload() { // :251-261 (the timer belongs to the form)
    if (cancelNotifier_) {
        cancelNotifier_->cancel();
    }
}

bool ProgressDownload::createFileUnique(QString& fileName) { // :474-527
    constexpr int kTryCount = 10;
    if (!isSessionInitialized_) {
        sessionPrefix_ =
            progressInfo_->sessionAutosavePrefix(); // legacy turns '/' into '\'; the port keeps '/'
        sessionPrefix_ = autoSaveDir_ + QLatin1Char('/') + sessionPrefix_;
        const QString dir =
            sessionPrefix_.left(sessionPrefix_.lastIndexOf(QLatin1Char('/')) + 1); // ExtractFilePath
        if (!QDir().mkpath(dir)) {
            throw std::runtime_error("cannot create directory " + dir.toStdString()); // RaiseLastOSError
        }
        int workerIndex = 0;
        int workersCount = 0;
        progressInfo_->getWorkerInfo(workerIndex, workersCount);
        if (workersCount > 1) {
            sessionPrefix_ += QString::asprintf("%.2d_", workerIndex + 1);
        }
    }

    const QString dateTimeFmt = progressInfo_->sessionAutosavePrefix().isEmpty()
                                    ? QStringLiteral("yyMMdd_HHmmss_zzz")
                                    : QStringLiteral("HHmmss_zzz");

    int i = 0;
    while (i < kTryCount) {
        fileName = sessionPrefix_ + now_().toString(dateTimeFmt) + QStringLiteral(".sls");
        if (!QFile::exists(fileName)) {
            QFile f(fileName);
            if (f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { // CREATE_NEW
                f.close();
                break;
            }
        }
        sleep_(100);
        ++i;
    }
    return i < kTryCount;
}

// FEAT-REG-017
void ProgressDownload::saveSession(const QString& targetFileName) { // DoSaveSession :451-470
    if (targetFileName.isEmpty()) {
        return;
    }
    auto ini = core::ConfigProviderIni::open(targetFileName);
    if (!ini) {
        throw std::runtime_error(ini.error().what().toStdString());
    }
    const core::IConfigWriterPtr section = (*ini)->orCreateSubItem(u"Session");
    progressInfo_->saveState(*section);
    if (auto saved = (*ini)->save(); !saved) {
        throw std::runtime_error(saved.error().what().toStdString());
    }
    memo_(QStringLiteral("Session saved to file: ") + targetFileName);
}

// FEAT-REG-016
void ProgressDownload::checkSessionAutosave() { // :472-577
    const int interval = progressInfo_->sessionAutosaveInterval();
    if (interval <= 0) {
        return; // disabled
    }

    const auto minutes =
        static_cast<qint64>(std::nearbyint((progressInfo_->elapsedTime() - lastElapsedTime_) * 24 * 60));
    if (isSessionInitialized_ && minutes < interval) {
        return; // timeout is not expired yet
    }

    QString fileName;
    try {
        if (!createFileUnique(fileName)) {
            return; // legacy Exit inside the try: isSessionInitialized_ stays as it was
        }
        saveSession(fileName);

        if (!sessionFileName_.isEmpty()) {
            if (!sessionFileNameBack_.isEmpty()) {
                QFile::remove(sessionFileNameBack_);
            }
            sessionFileNameBack_ = changeExtToBk(sessionFileName_);
            // frm_ProgressDownload.pas:556-559: DeleteFile of the old backup, then RenameFile (which fails
            // when the target exists, as QFile::rename does on every OS): the target was removed above, so no
            // replace semantics are needed here (T-0335).
            if (!QFile::rename(sessionFileName_, sessionFileNameBack_)) {
                sessionFileNameBack_ = sessionFileName_;
            }
        }
        sessionFileName_ = fileName;

        lastElapsedTime_ = progressInfo_->elapsedTime();
    } catch (const std::exception& e) {
        memo_(QStringLiteral("Failed to save session to file: ") + fileName + QStringLiteral("\r\n") +
              exceptionText(e));
    }

    isSessionInitialized_ = true;
}

void ProgressDownload::close() { // FormClose :586-595
    cancelNotifier_->cancel();
    if (!sessionFileNameBack_.isEmpty()) {
        QFile::remove(sessionFileNameBack_);
    }
}

QString ProgressDownload::sessionFileName() const {
    return sessionFileName_;
}

} // namespace cg::region
