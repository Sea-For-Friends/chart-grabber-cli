// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_LogSimpleProvider.pas, u_LogForTaskThread.pas (class TLogSimpleProvider)
//
// A bounded in-memory tail of log lines. The region-download progress dialog polls it for
// whatever appeared since it last looked; lines that were evicted or beyond the poll's row limit
// are simply gone, exactly as in legacy.

#include "cg/core/sync.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <deque>
#include <utility>

namespace cg::core {

class LogRingBuffer {
public:
    // legacy default at call sites: 5000 lines, minimum level 0 (u_ProviderTilesDownload.pas:210)
    explicit LogRingBuffer(int capacity, int minLogLevel = 0);

    // Unconditional; its only caller is core::Log's sink (log.cpp), which has no level.
    void append(QString line);

    // Ports WriteText(AMessage, ALogLevel) (u_LogForTaskThread.pas:135-150): a line whose level is
    // below the minimum is dropped before the lock is taken and consumes no id.
    void append(QString line, int level);

    // Ports GetLastMessages(AMaxRowsCount, var ALastId, out AcntLines) (u_LogForTaskThread.pas:94-133):
    // returns the NEWEST min(lines after `lastId`, `maxRows`, capacity) lines, oldest first, and sets
    // `lastId` to the id of the newest line written (also when nothing is returned or `lastId` was
    // ahead). Older unread lines are skipped for good, as in legacy.
    [[nodiscard]] QStringList since(std::uint64_t& lastId, int maxRows) const;

    void clear();
    [[nodiscard]] int capacity() const noexcept;

private:
    mutable Mutex mutex_;
    std::deque<std::pair<std::uint64_t, QString>> lines_;
    std::uint64_t nextId_ = 1;
    int capacity_;
    const int minLogLevel_;
};

} // namespace cg::core
