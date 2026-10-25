// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/log_ring_buffer.h"

#include <algorithm>
#include <iterator>

namespace cg::core {

LogRingBuffer::LogRingBuffer(int capacity, int minLogLevel)
    : capacity_(std::max(1, capacity)), minLogLevel_(minLogLevel) {}

void LogRingBuffer::append(QString line) {
    const WriteLock lock(mutex_);
    lines_.emplace_back(nextId_++, std::move(line));
    while (std::ssize(lines_) > capacity_) {
        lines_.pop_front();
    }
}

void LogRingBuffer::append(QString line, int level) {
    if (level < minLogLevel_) {
        return;
    }
    append(std::move(line));
}

QStringList LogRingBuffer::since(std::uint64_t& lastId, int maxRows) const {
    QStringList result;
    const ReadLock lock(mutex_);
    // legacy FNextId counts the lines written so far; here nextId_ is one past the newest id
    const std::uint64_t written = nextId_ - 1;
    if (lastId < written) {
        // u_LogForTaskThread.pas:105-116: the newest min(new, AMaxRowsCount, capacity) lines.
        // maxRows is Cardinal in legacy; a negative value cannot arrive (no production caller yet).
        std::uint64_t count = written - lastId;
        count = std::min<std::uint64_t>(count, static_cast<std::uint64_t>(std::max(0, maxRows)));
        count = std::min<std::uint64_t>(count, static_cast<std::uint64_t>(capacity_));
        // a clear() may have left fewer lines than the ring holds (legacy has no clear)
        count = std::min<std::uint64_t>(count, lines_.size());
        for (auto it = lines_.end() - static_cast<std::ptrdiff_t>(count); it != lines_.end(); ++it) {
            result.append(it->second);
        }
    }
    // :125 and :131: every branch leaves lastId at FNextId, so unread older lines are skipped
    lastId = written;
    return result;
}

void LogRingBuffer::clear() {
    const WriteLock lock(mutex_);
    lines_.clear();
}

int LogRingBuffer::capacity() const noexcept {
    return capacity_;
}

} // namespace cg::core
