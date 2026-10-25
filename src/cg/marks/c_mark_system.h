// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#pragma once
// Ports: c_MarkSystem.pas; the constant CMarksSystemInternalURL of Src/Browser/c_InternalBrowser.pas:43
// (spec marks.md section 1.3: it folds into M1, its users are the two mark-id implementations).

#include <QCoreApplication>
#include <QString>
#include <QStringView>
#include <QUuid>

namespace cg::marks {

inline constexpr QUuid cSMLMarksDbGUID{0xF5A9FB07, 0xFF9B, 0x4B4A, 0x98, 0xD3, 0xAB,
                                       0x8C,       0x5F,   0x24,   0xF3, 0x6B};
inline constexpr QUuid cORMSQLiteMarksDbGUID{0x776BC34B, 0x7A4B, 0x4237, 0xBE, 0x9A, 0xD9,
                                             0x05,       0x22,   0x8F,   0xDA, 0x1A};
inline constexpr QUuid cORMMongoDbMarksDbGUID{0xAA5DF0DA, 0xC785, 0x4C1F, 0xAD, 0xF5, 0x2F,
                                              0xC9,       0x01,   0x46,   0xBE, 0x5B};
inline constexpr QUuid cORMODBCMarksDbGUID{0x03578F25, 0x094A, 0x4C2F, 0x86, 0x09, 0x5D,
                                           0x7E,       0x76,   0x69,   0x9B, 0xA3};
inline constexpr QUuid cORMZDBCMarksDbGUID{0x4D7A6F63, 0x412E, 0x46E3, 0xAF, 0xD0, 0x4C,
                                           0x0E,       0x8B,   0x9A,   0xF2, 0xC5};

inline constexpr QStringView cSMLMarksDbDefFileName = u"marks";
inline constexpr QStringView cSMLMarksDbFileExt = u".sml";
inline constexpr QStringView cORMSQLiteMarksDbDefFileName = u"Marks";
inline constexpr QStringView cORMSQLiteMarksDbFileExt = u".db3";

// c_InternalBrowser.pas:43 = CSASInternalURLPrefix ('sas://') + CMarksSystemInternalDomain ('Placemarks') +
// '/'
inline constexpr QStringView cMarksSystemInternalURL = u"sas://Placemarks/";

// resourcestring rs*MarksDbName
inline QString rsSMLMarksDbName() {
    return QCoreApplication::translate("MarkSystem", "SML");
}
inline QString rsORMSQLiteMarksDbName() {
    return QCoreApplication::translate("MarkSystem", "SQLite3");
}
inline QString rsORMMongoDbMarksDbName() {
    return QCoreApplication::translate("MarkSystem", "MongoDB");
}
inline QString rsORMODBCMarksDbName() {
    return QCoreApplication::translate("MarkSystem", "DBMS (ODBC driver)");
}
inline QString rsORMZDBCMarksDbName() {
    return QCoreApplication::translate("MarkSystem", "DBMS (Native driver)");
}

} // namespace cg::marks
