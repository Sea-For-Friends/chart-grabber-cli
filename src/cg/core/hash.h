// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: Src/Basic/t_Hash.pas, i_HashFunction.pas, i_HashFunctionImpl.pas,
//        u_HashFunctionByImpl.pas, u_HashFunctionCityHash.pas, u_HashFunctionCRC64.pas,
//        Includes/CityHash.pas, Includes/libcrc32.pas
//
// Content hashes. The values are persisted by some cache formats and gate every tile redraw, so
// the primitives must stay byte-identical to the legacy CityHash.dll. Legacy threaded an
// IHashFunction interface through ~40 constructors only so that debug builds could wrap it in a
// performance counter; with one implementation this is free functions.

#include <QByteArrayView>
#include <QStringView>
#include <QUuid>
#include <QtGlobal>

#include <cstddef>
#include <cstdint>

namespace cg::core {

using HashValue = std::uint64_t;

// Legacy has no named sentinel (t_Hash.pas is a bare UInt64 alias), but treats 0 as
// "not computed" at every comparison site. The port names it so the convention is greppable.
inline constexpr HashValue kNoHash = 0;

// --- primitives -------------------------------------------------------------
// Google CityHash 1.1.1, vendored in third_party/cityhash (see its README for why 1.1.1 and not
// the 1.0.3 the spec names). Legacy called the identical functions through CityHash.dll.
[[nodiscard]] HashValue cityHash64(const void* data, std::size_t size) noexcept;
[[nodiscard]] HashValue cityHash64WithSeed(const void* data, std::size_t size, HashValue seed) noexcept;

// CRC-64, Jones polynomial 0xC96C5795D7870F42 (reflected), init 0xFFFF'FFFF'FFFF'FFFF,
// NO final xor-out (u_HashFunctionCRC64.pas:68,82,99). Used by the ported legacy tests and the
// benchmark harness as a deterministic stand-in; never by the shipping app.
[[nodiscard]] HashValue crc64(const void* data, std::size_t size) noexcept;
[[nodiscard]] HashValue crc64WithSeed(const void* data, std::size_t size, HashValue seed) noexcept;

// CRC-32/ISO-HDLC (zlib): poly 0xEDB88320 reflected, init 0xFFFFFFFF, final xor 0xFFFFFFFF.
// Legacy called zlib's crc32(prev, buf, len) through libcrc32.dll. Implemented in-tree so core
// keeps zero link dependencies.
[[nodiscard]] std::uint32_t crc32(std::uint32_t prev, const void* data, std::size_t size) noexcept;

// --- the IHashFunction facade ----------------------------------------------
// An empty string / null buffer hashes to 0 (kNoHash), as legacy CalcHash(nil, ...).
[[nodiscard]] HashValue hashOf(QByteArrayView data) noexcept;
[[nodiscard]] HashValue hashOf(QStringView text) noexcept; // hashes the UTF-16 code units
[[nodiscard]] HashValue hashOf(const QUuid& id) noexcept;  // hashes the 16 raw bytes
[[nodiscard]] HashValue hashOf(double value) noexcept;
[[nodiscard]] HashValue hashOf(qint32 value) noexcept;

// Chaining: newHash = cityHash64WithSeed(bytes, size, running). Ports UpdateHashByX
// (u_HashFunctionByImpl.pas:133-142). A zero-size buffer leaves `running` untouched.
void hashUpdate(HashValue& running, QByteArrayView data) noexcept;
void hashUpdate(HashValue& running, QStringView text) noexcept;
void hashUpdate(HashValue& running, const QUuid& id) noexcept;
void hashUpdate(HashValue& running, double value) noexcept;
void hashUpdate(HashValue& running, qint32 value) noexcept;

// Ports UpdateHashByHash (u_HashFunctionByImpl.pas:176-182): hashes the 8 raw little-endian
// bytes of `value`, seeded with `running`. The one and only hash-combining primitive.
void hashUpdate(HashValue& running, HashValue value) noexcept;

} // namespace cg::core
