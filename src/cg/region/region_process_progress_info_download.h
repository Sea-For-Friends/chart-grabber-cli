// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/RegionProcess/i_RegionProcessProgressInfoDownload.pas (:34-88 reader, :90-116 internal),
//        Src/RegionProcess/u_RegionProcessProgressInfoDownload.pas (TRegionProcessProgressInfoDownload,
//        :38-118)

#include "cg/core/config_writer.h"
#include "cg/core/log_ring_buffer.h"
#include "cg/core/sync.h"
#include "cg/geo/geo_types.h"
#include "cg/region/download_session.h"
#include "cg/region/region_process_progress_info.h"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace cg::region {

// No legacy counterpart: how one tile was answered against the class "the frame is not served yet".
enum class FrameAnswer : std::uint8_t {
    Other,       // stored, skipped as present, or any answer outside the class (also no HTTP answer)
    Unavailable, // HTTP 5xx or the ban tile: the worker asks for the same tile again
    Missing      // 404 / the empty tile (DataNotExists), a TNE row of this frame met while skipping
};

struct FrameAnswers {
    qint64 unavailableRun = 0; // Unavailable answers in a row (any other answer restarts the count)
    qint64 inClass = 0;        // Unavailable + Missing answers
    qint64 other = 0;          // Other answers
};

struct RefusalCounts {      // No legacy counterpart; not saved in the session
    qint64 streak = 0;      // refusals in a row; any other download answer resets it
    qint64 refused = 0;     // refusals since the run started (every attempt counts, also a retried tile)
    qint64 notRefused = 0;  // download answers that were not refusals, and tiles skipped as already stored
    quint32 lastStatus = 0; // HTTP status of the newest refusal
};

// i_RegionProcessProgressInfoDownload.pas:34-88: what the progress window reads and commands.
class IRegionProcessProgressInfoDownload : public IProgressInfoBase {
public:
    [[nodiscard]] virtual qint64 totalToProcess() const = 0;
    [[nodiscard]] virtual qint64 downloaded() const = 0;
    [[nodiscard]] virtual qint64 processed() const = 0;
    [[nodiscard]] virtual quint64 downloadSize() const = 0;
    [[nodiscard]] virtual double elapsedTime() const = 0; // days (TDateTime)
    [[nodiscard]] virtual quint8 zoom() const = 0;
    [[nodiscard]] virtual std::vector<quint8> zoomArray() const = 0;
    virtual void getLastTileInfo(quint8& zoom, geo::PointI& point, int& size) const = 0;
    [[nodiscard]] virtual std::shared_ptr<core::LogRingBuffer> logProvider() const = 0;
    virtual void saveState(core::IConfigWriter& slsSection) = 0;
    [[nodiscard]] virtual bool isPaused() const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    [[nodiscard]] virtual bool autoCloseAtFinish() const = 0;
    virtual void setAutoCloseAtFinish(bool value) = 0;
    [[nodiscard]] virtual int sessionAutosaveInterval() const = 0;
    [[nodiscard]] virtual QString sessionAutosavePrefix() const = 0;
    virtual void getWorkerInfo(int& workerIndex, int& workersCount) const = 0;
    // No legacy counterpart: tiles in a row whose download failed with a connection error.
    [[nodiscard]] virtual qint64 connectionErrorStreak() const = 0;
    // No legacy counterpart: the answers of this run against the class "the frame is not served yet". Default
    // all 0 for readers that never count (test doubles).
    [[nodiscard]] virtual FrameAnswers frameAnswers() const { return {}; }
    // No legacy counterpart: the refusals (HTTP 401, 403, 451) of this run. Default all 0.
    [[nodiscard]] virtual RefusalCounts refusalCounts() const { return {}; }
};

// :90-116: what the worker thread writes. Does not derive from the reader interface, as in legacy.
class IRegionProcessProgressInfoDownloadInternal {
public:
    virtual ~IRegionProcessProgressInfoDownloadInternal() = default;
    [[nodiscard]] virtual bool needPause() const = 0;
    virtual void setNeedPause(bool value) = 0;
    virtual void finish() = 0;
    virtual void setPaused() = 0;
    virtual void setStarted() = 0;
    virtual void addManyProcessedTile(geo::PointI lastTile, quint32 count) = 0; // no legacy caller
    virtual void addProcessedTile(geo::PointI tile) = 0;
    virtual void addDownloadedTile(geo::PointI tile, quint32 size) = 0;
    virtual void addNotNecessaryTile(geo::PointI tile) = 0;
    virtual void setTotalToProcess(qint64 value) = 0;
    [[nodiscard]] virtual std::shared_ptr<core::LogRingBuffer> log() const = 0;
    virtual void setZoom(quint8 value) = 0;
    // No legacy counterpart: the server answered (false: streak = 0) or the connection failed (true).
    virtual void noteDownloadAnswer(bool connectionError) = 0;
    // No legacy counterpart: a tile was answered (or skipped as present). Default: no counting.
    virtual void noteFrameAnswer(FrameAnswer /*answer*/) {}
    // No legacy counterpart: a download answer was a refusal (true, with its HTTP status) or not.
    virtual void noteRefusal(bool /*refused*/, quint32 /*statusCode*/) {}
    // A tile was skipped as already stored (or a TNE row): counted as not refused; the streak stays.
    virtual void noteSkippedPresent() {}
};

// The progress object of a download: progress, pause and resume.
//
// Shared between the download worker (writes through the Internal interface) and the polling
// caller (reads through the reader interface). ONE read/write lock guards the counters and the
// session. The session (IDownloadSession) is not thread-safe: once the worker exists it is reached
// ONLY through this object, which is what makes the unlocked session safe.
class RegionProcessProgressInfoDownload final : public IRegionProcessProgressInfoDownload,
                                                public IRegionProcessProgressInfoDownloadInternal {
public:
    // :128-145. Legacy takes the log twice (ILogSimple and ILogSimpleProvider are one object,
    // u_ProviderTilesDownload.pas:210-212); the port takes it once. `now` is the test seam for the
    // clock in Delphi day counts; empty = the wall clock (legacy Now).
    RegionProcessProgressInfoDownload(std::shared_ptr<core::LogRingBuffer> log,
                                      std::shared_ptr<IDownloadSession> session, bool paused,
                                      std::function<double()> now = {});

    // IProgressInfoBase / reader
    [[nodiscard]] double processedRatio() const override;
    [[nodiscard]] bool finished() const override;
    [[nodiscard]] qint64 totalToProcess() const override;
    [[nodiscard]] qint64 downloaded() const override;
    [[nodiscard]] qint64 processed() const override;
    [[nodiscard]] quint64 downloadSize() const override;
    [[nodiscard]] double elapsedTime() const override;
    [[nodiscard]] quint8 zoom() const override;
    [[nodiscard]] std::vector<quint8> zoomArray() const override;
    void getLastTileInfo(quint8& zoom, geo::PointI& point, int& size) const override;
    [[nodiscard]] std::shared_ptr<core::LogRingBuffer> logProvider() const override;
    void saveState(core::IConfigWriter& slsSection) override;
    [[nodiscard]] bool isPaused() const override;
    void pause() override;
    void resume() override;
    [[nodiscard]] bool autoCloseAtFinish() const override;
    void setAutoCloseAtFinish(bool value) override;
    [[nodiscard]] int sessionAutosaveInterval() const override;
    [[nodiscard]] QString sessionAutosavePrefix() const override;
    void getWorkerInfo(int& workerIndex, int& workersCount) const override;
    [[nodiscard]] qint64 connectionErrorStreak() const override;
    [[nodiscard]] FrameAnswers frameAnswers() const override;
    [[nodiscard]] RefusalCounts refusalCounts() const override;

    // Internal
    [[nodiscard]] bool needPause() const override;
    void setNeedPause(bool value) override;
    void finish() override;
    void setPaused() override;
    void setStarted() override;
    void addManyProcessedTile(geo::PointI lastTile, quint32 count) override;
    void addProcessedTile(geo::PointI tile) override;
    void addDownloadedTile(geo::PointI tile, quint32 size) override;
    void addNotNecessaryTile(geo::PointI tile) override;
    void setTotalToProcess(qint64 value) override;
    [[nodiscard]] std::shared_ptr<core::LogRingBuffer> log() const override;
    void setZoom(quint8 value) override;
    void noteDownloadAnswer(bool connectionError) override;
    void noteFrameAnswer(FrameAnswer answer) override;
    void noteRefusal(bool refused, quint32 statusCode) override;
    void noteSkippedPresent() override;

private:
    [[nodiscard]] double now() const;

    std::function<double()> now_;
    std::shared_ptr<core::LogRingBuffer> log_;
    std::shared_ptr<IDownloadSession> session_;
    mutable core::Mutex mutex_;
    bool finished_ = false;
    bool needPause_;
    bool paused_;
    double startTime_;
    qint64 totalInRegion_ = 0;
    int lastSuccessfulSize_ = -1;
    std::atomic<qint64> connectionErrorStreak_{0}; // Not saved
    std::atomic<qint64> unavailableRun_{0};        // Not saved
    std::atomic<qint64> inClass_{0};               // Not saved
    std::atomic<qint64> other_{0};                 // Not saved
    std::atomic<qint64> refusalStreak_{0};         // Not saved
    std::atomic<qint64> refused_{0};               // Not saved
    std::atomic<qint64> notRefused_{0};            // Not saved
    std::atomic<quint32> lastRefusalStatus_{0};    // Not saved
};

} // namespace cg::region
