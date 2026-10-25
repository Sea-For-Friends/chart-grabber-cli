// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: i_PredicateByBinaryData.pas, u_PredicateByStaticSampleList.pas
//
// IPredicateByBinaryData is `Check(data): bool`; here a std::function (core.md assigns the alias to maptype).
// TPredicateByStaticSampleList answers "is the data byte-for-byte equal to one of the samples". Samples are
// indexed by size, then by CRC-32, then compared with CompareMem (u_PredicateByStaticSampleList.pas:58-138);
// the index only speeds the lookup up, the answer is plain equality with a non-empty sample.

#include <QByteArray>
#include <QtGlobal>

#include <functional>
#include <map>
#include <vector>

namespace cg::maptype {

using PredicateByBinaryData = std::function<bool(const QByteArray&)>;

class PredicateByStaticSampleList final {
public:
    // Empty samples (size 0) are skipped (:70); an identical sample given twice is stored once (:88-117).
    explicit PredicateByStaticSampleList(const std::vector<QByteArray>& samples);

    // Check (:140-172). Data of size 0 never matches: no sample has that size.
    [[nodiscard]] bool check(const QByteArray& data) const;

private:
    // size -> crc32 as Integer (signed, :137) -> the distinct samples with that size and hash.
    std::map<qsizetype, std::map<qint32, std::vector<QByteArray>>> bySize_;
};

// The predicate as a callable that owns its table (the checker and the delete-tiles dialog hold it as
// IPredicateByBinaryData).
[[nodiscard]] PredicateByBinaryData makePredicateByStaticSampleList(const std::vector<QByteArray>& samples);

} // namespace cg::maptype
