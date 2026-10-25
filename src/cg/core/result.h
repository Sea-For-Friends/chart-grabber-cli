// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: nothing. The coding standard names `cg::Result<T>` as the carrier for expected
// failures; legacy signalled them inconsistently (HResult, nil interfaces, raised exceptions).
//
// A Result either holds the value the caller asked for or an Error describing why it could not
// be produced. Programmer errors stay exceptions/asserts and never travel in a Result.

#include <QString>

#include <cstdint>
#include <expected>
#include <utility>

#if !defined(__cpp_lib_expected)
#error "cg::core requires <expected> (libstdc++ 13+ in C++23 mode); see docs/handoffs/T-0002A.md"
#endif

namespace cg::core {

enum class ErrorCode : std::uint8_t {
    None = 0,
    NotFound,        // key/section/file absent
    ParseError,      // malformed input (ini line, mime string, hex blob)
    IoError,         // open/read/write failed
    Unsupported,     // format recognised but not handled
    Cancelled,       // std::stop_token was signalled
    DecompressError, // a recognised compressed container's payload failed to inflate; distinct
                     // from ParseError because some legacy callers do NOT treat this as a bad
                     // value (see u_BerkeleyDBValueZlib.pas ZlibDecompress / EZDecompressionError)
};

class Error {
public:
    Error() = default;
    Error(ErrorCode code, QString what) : code_(code), what_(std::move(what)) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const QString& what() const noexcept { return what_; }

private:
    ErrorCode code_ = ErrorCode::None;
    QString what_;
};

template <class T, class E = Error> using Result = std::expected<T, E>;

using Unexpected = std::unexpected<Error>;

[[nodiscard]] inline Unexpected fail(ErrorCode code, QString what) {
    return Unexpected{Error{code, std::move(what)}};
}

} // namespace cg::core
