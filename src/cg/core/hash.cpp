// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/hash.h"

#include <QtEndian>

#include <array>
#include <bit>
#include <cityhash/city.h>
#include <cstring>

namespace cg::core {
namespace {

constexpr std::uint64_t crc64Poly = 0xC96C5795D7870F42ULL;
constexpr std::uint32_t crc32Poly = 0xEDB88320U;

constexpr std::array<std::uint64_t, 256> makeCrc64Table() {
    std::array<std::uint64_t, 256> table{};
    for (std::uint64_t i = 0; i < 256; ++i) {
        std::uint64_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0 ? (value >> 1U) ^ crc64Poly : value >> 1U;
        }
        table[i] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> makeCrc32Table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0 ? (value >> 1U) ^ crc32Poly : value >> 1U;
        }
        table[i] = value;
    }
    return table;
}

constexpr std::array<std::uint64_t, 256> crc64Table = makeCrc64Table();
constexpr std::array<std::uint32_t, 256> crc32Table = makeCrc32Table();

// The 16 bytes of a Delphi TGUID as they sit in memory: D1, D2 and D3 little-endian, D4 raw.
// Legacy hashed exactly this buffer (u_HashFunctionByImpl.pas:160-166).
[[nodiscard]] std::array<std::uint8_t, 16> uuidBytes(const QUuid& id) noexcept {
    std::array<std::uint8_t, 16> bytes{};
    qToLittleEndian<quint32>(id.data1, bytes.data());
    qToLittleEndian<quint16>(id.data2, bytes.data() + 4);
    qToLittleEndian<quint16>(id.data3, bytes.data() + 6);
    std::memcpy(bytes.data() + 8, static_cast<const void*>(id.data4), 8);
    return bytes;
}

[[nodiscard]] std::array<std::uint8_t, 8> doubleBytes(double value) noexcept {
    std::array<std::uint8_t, 8> bytes{};
    qToLittleEndian<quint64>(std::bit_cast<quint64>(value), bytes.data());
    return bytes;
}

[[nodiscard]] std::array<std::uint8_t, 4> int32Bytes(qint32 value) noexcept {
    std::array<std::uint8_t, 4> bytes{};
    qToLittleEndian<qint32>(value, bytes.data());
    return bytes;
}

[[nodiscard]] std::array<std::uint8_t, 8> hashBytes(HashValue value) noexcept {
    std::array<std::uint8_t, 8> bytes{};
    qToLittleEndian<quint64>(value, bytes.data());
    return bytes;
}

} // namespace

HashValue cityHash64(const void* data, std::size_t size) noexcept {
    return CityHash64(static_cast<const char*>(data), size);
}

HashValue cityHash64WithSeed(const void* data, std::size_t size, HashValue seed) noexcept {
    return CityHash64WithSeed(static_cast<const char*>(data), size, seed);
}

HashValue crc64(const void* data, std::size_t size) noexcept {
    return crc64WithSeed(data, size, ~HashValue{0});
}

HashValue crc64WithSeed(const void* data, std::size_t size, HashValue seed) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    HashValue crc = seed;
    for (std::size_t i = 0; i < size; ++i) {
        crc = (crc >> 8U) ^ crc64Table[(crc ^ bytes[i]) & 0xFFU];
    }
    return crc;
}

std::uint32_t crc32(std::uint32_t prev, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = ~prev;
    for (std::size_t i = 0; i < size; ++i) {
        crc = (crc >> 8U) ^ crc32Table[(crc ^ bytes[i]) & 0xFFU];
    }
    return ~crc;
}

HashValue hashOf(QByteArrayView data) noexcept {
    if (data.data() == nullptr) {
        return 0; // u_HashFunctionCityHash.pas:59-63: CalcHash(nil, ...) is 0
    }
    return cityHash64(data.data(), static_cast<std::size_t>(data.size()));
}

HashValue hashOf(QStringView text) noexcept {
    if (text.isEmpty()) {
        return 0; // an empty Delphi string is nil; CalcHashByString -> CalcHash(nil, 0) = 0
                  // (u_HashFunctionCityHash.pas:59-63)
    }
    return cityHash64(text.utf16(), static_cast<std::size_t>(text.size()) * sizeof(char16_t));
}

HashValue hashOf(const QUuid& id) noexcept {
    const auto bytes = uuidBytes(id);
    return cityHash64(bytes.data(), bytes.size());
}

HashValue hashOf(double value) noexcept {
    const auto bytes = doubleBytes(value);
    return cityHash64(bytes.data(), bytes.size());
}

HashValue hashOf(qint32 value) noexcept {
    const auto bytes = int32Bytes(value);
    return cityHash64(bytes.data(), bytes.size());
}

void hashUpdate(HashValue& running, QByteArrayView data) noexcept {
    if (data.isEmpty()) {
        return; // u_HashFunctionByImpl.pas:133-142 skips empty buffers
    }
    running = cityHash64WithSeed(data.data(), static_cast<std::size_t>(data.size()), running);
}

void hashUpdate(HashValue& running, QStringView text) noexcept {
    if (text.isEmpty()) {
        return;
    }
    running =
        cityHash64WithSeed(text.utf16(), static_cast<std::size_t>(text.size()) * sizeof(char16_t), running);
}

void hashUpdate(HashValue& running, const QUuid& id) noexcept {
    const auto bytes = uuidBytes(id);
    running = cityHash64WithSeed(bytes.data(), bytes.size(), running);
}

void hashUpdate(HashValue& running, double value) noexcept {
    const auto bytes = doubleBytes(value);
    running = cityHash64WithSeed(bytes.data(), bytes.size(), running);
}

void hashUpdate(HashValue& running, qint32 value) noexcept {
    const auto bytes = int32Bytes(value);
    running = cityHash64WithSeed(bytes.data(), bytes.size(), running);
}

void hashUpdate(HashValue& running, HashValue value) noexcept {
    const auto bytes = hashBytes(value);
    running = cityHash64WithSeed(bytes.data(), bytes.size(), running);
}

} // namespace cg::core
