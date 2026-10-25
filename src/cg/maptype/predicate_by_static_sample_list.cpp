// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/maptype/predicate_by_static_sample_list.h"

#include <algorithm>
#include <memory>
#include <zlib.h>

namespace cg::maptype {

namespace {

// CalcHash (:132-138): `Integer(crc32(0, Buffer, Size))`.
qint32 calcHash(const QByteArray& data) {
    return static_cast<qint32>(
        crc32(0, reinterpret_cast<const Bytef*>(data.constData()), static_cast<uInt>(data.size())));
}

} // namespace

PredicateByStaticSampleList::PredicateByStaticSampleList(const std::vector<QByteArray>& samples) {
    for (const QByteArray& sample : samples) { // :64-73
        if (sample.size() > 0) {
            std::vector<QByteArray>& same = bySize_[sample.size()][calcHash(sample)]; // AppendSample :76-120
            if (std::ranges::find(same, sample) == same.end()) { // CompareData: CompareMem
                same.push_back(sample);
            }
        }
    }
}

bool PredicateByStaticSampleList::check(const QByteArray& data) const {
    const auto bySize = bySize_.find(data.size());
    if (bySize == bySize_.end()) {
        return false;
    }
    const auto byHash = bySize->second.find(calcHash(data));
    if (byHash == bySize->second.end()) {
        return false;
    }
    return std::ranges::find(byHash->second, data) != byHash->second.end();
}

PredicateByBinaryData makePredicateByStaticSampleList(const std::vector<QByteArray>& samples) {
    auto predicate = std::make_shared<const PredicateByStaticSampleList>(samples);
    return [predicate](const QByteArray& data) { return predicate->check(data); };
}

} // namespace cg::maptype
