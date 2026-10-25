// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: u_ArchiveReadWrite7Zip.pas (TArchiveReadBy7Zip, atZip) -- on libarchive, ADR-0012
//
// The whole zip file lives in memory (legacy: TMemoryStream, u_ZmpInfoSet.pas:141-155). open() lists
// the item names once; an item's bytes are extracted on demand by re-reading the archive up to
// that item, so a lookup costs one scan of the headers (legacy: a linear SameText scan too).

#include "cg/core/archive_reader.h"
#include "cg/core/result.h"

namespace cg::core {

// Port invention (D-65): one item larger than this is refused instead of being inflated into
// memory without bound (decompression bomb); legacy has no limit.
inline constexpr qint64 kDefaultMaxItemBytes = qint64{512} * 1024 * 1024;

// Fails (Result error) when the bytes are not a readable zip; legacy raised from OpenStream.
// `label` (the file name) is only used in messages; maxItemBytes is a parameter for the test.
[[nodiscard]] Result<IArchiveReaderPtr> openArchiveReaderZip(const QByteArray& zipBytes,
                                                             const QString& label = {},
                                                             qint64 maxItemBytes = kDefaultMaxItemBytes);

} // namespace cg::core
