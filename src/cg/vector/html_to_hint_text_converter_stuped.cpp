// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/vector/html_to_hint_text_converter_stuped.h"

#include "cg/core/delphi_text.h"

#include <QChar>

namespace cg::vector {

namespace {

// The Delphi helpers work on 1-based positions; the port keeps them so that the statements below
// stay comparable with the legacy unit. LowerCase/UpperCase/Trim are the Delphi text helpers of core.
using core::delphiLowerCase;
using core::delphiTrim;
using core::delphiUpperCase;

int pos(const QString& needle, const QString& hay) {
    return static_cast<int>(hay.indexOf(needle)) + 1;
}

int posEx(const QString& needle, const QString& hay, int start) {
    return static_cast<int>(hay.indexOf(needle, start - 1)) + 1;
}

// Delphi Copy: 1-based, a count <= 0 or a start past the end gives ''.
QString copy(const QString& s, int start, int count) {
    if (count <= 0 || start > s.size()) {
        return {};
    }
    return s.mid(start - 1, count);
}

// Delphi StringReplace [rfReplaceAll(, rfIgnoreCase)]: an empty pattern leaves the text unchanged.
QString stringReplace(QString s, const QString& pattern, const QString& with, bool ignoreCase = false) {
    if (pattern.isEmpty()) {
        return s;
    }
    return s.replace(pattern, with, ignoreCase ? Qt::CaseInsensitive : Qt::CaseSensitive);
}

// Local function MidStr of THtmlToHintTextConverterStuped.HTML2Txt.
QString midStr(const QString& str, const QString& open, const QString& close, bool include) {
    const QString upStr = delphiUpperCase(str);
    int ini = 0;
    int fim = 0;
    if (!include) {
        ini = pos(delphiUpperCase(open), upStr) + static_cast<int>(open.size());
        fim = posEx(delphiUpperCase(close), upStr, ini) + 1;
    } else {
        ini = pos(delphiUpperCase(open), upStr);
        fim = posEx(delphiUpperCase(close), upStr, ini + static_cast<int>(open.size())) + 1;
    }
    return copy(str, ini, fim - ini);
}

} // namespace

QString stupedHtmlToTextConverter(const QString& source) {
    // FEAT-GEO-037 (mark hints). Order and literals as legacy; `&#171` has no ';' (L-124).
    static const struct {
        const char16_t* from;
        const char16_t* to;
    } kRules[] = {
        {u"&#36;", u"$"},   {u"&#37;", u"%"},  {u"&#39;", u"'"},  {u"&amp;", u"&"},   {u"&#38;", u"&"},
        {u"&quot;", u"\""}, {u"&#34;", u"\""}, {u"&lt;", u"<"},   {u"&#60;", u"<"},   {u"&gt;", u">"},
        {u"&#62;", u">"},   {u"&nbsp;", u" "}, {u"&#160;", u" "}, {u"&raquo;", u"»"}, {u"&#187;", u"»"},
        {u"&laquo;", u"«"}, {u"&#171", u"«"},  {u"&copy;", u"©"}, {u"&#169;", u"©"},  {u"&reg;", u"®"},
        {u"&#174;", u"®"},
    };
    QString result = source;
    for (const auto& r : kRules) {
        result = stringReplace(result, QString::fromUtf16(r.from), QString::fromUtf16(r.to));
    }
    return result;
}

QString HtmlToHintTextConverterStuped::convert(const QString& name, const QString& description) const {
    QString result;

    if (!name.isEmpty()) {
        // if name found in description - dont show name
        const bool nameInDesc = pos(name, description) > 0;
        if (!nameInDesc) {
            result = pos(QStringLiteral("<"), name) > 0 ? html2Txt(name) : name;
        }
    }

    if (!description.isEmpty()) {
        if (!result.isEmpty()) {
            result += QStringLiteral("\r\n");
        }
        result += pos(QStringLiteral("<"), description) > 0 ? html2Txt(description) : description;
    }

    result = delphiTrim(result);

    int i = 1;
    int j = 0;
    while (i < result.size() && i != 0) {
        ++j;
        const QChar ch = result[i - 1];
        if (ch == u'\r' || ch == u'\n') {
            j = 0;
        }
        if (j > 40 && ch == u' ' && result.size() - i > 5) {
            if (i > 500) {
                result.insert(i - 1, QStringLiteral("..."));
                result.remove(i + 3 - 1, result.size() - i + 3);
                i = 0;
                continue;
            }
            result.remove(i - 1, 1);
            result.insert(i - 1, QStringLiteral("\r\n"));
            j = 0;
        }
        ++i;
    }
    return result;
}

QString HtmlToHintTextConverterStuped::html2Txt(QString origHtml) {
    int i = pos(QStringLiteral("<!-- sas.cut -->"), delphiLowerCase(origHtml));
    if (i > 0) {
        origHtml = origHtml.left(i - 1) + QStringLiteral("-->");
    }
    i = pos(QStringLiteral("<body"), delphiLowerCase(origHtml));
    if (i > 0) {
        origHtml = origHtml.mid(i - 1);
        origHtml = origHtml.mid(pos(QStringLiteral(">"), origHtml)); // Mid(s, Pos('>', s) + 1)
        i = pos(QStringLiteral("</body>"), delphiLowerCase(origHtml));
        if (i > 0) {
            origHtml = copy(origHtml, 1, i - 1);
        }
    }
    while (pos(QStringLiteral("  "), origHtml) > 0) {
        origHtml = stringReplace(origHtml, QStringLiteral("  "), QStringLiteral(" "));
    }
    origHtml = stringReplace(origHtml, QStringLiteral("<br>"), QStringLiteral("\r\n"), true);
    origHtml = stringReplace(origHtml, QStringLiteral("<br />"), QStringLiteral("\r\n"), true);
    origHtml = stringReplace(origHtml, QStringLiteral("</div>"), QStringLiteral("\r\n\r\n"), true);
    if (pos(QStringLiteral("<style"), origHtml) > 0) {
        // Not the whole style block: from after "<style" up to the '<' of "</style>"; the tag loop below
        // removes the rest.
        const QString noHtml = midStr(origHtml, QStringLiteral("<style"), QStringLiteral("</style>"), false);
        origHtml = stringReplace(origHtml, noHtml, QString(), true);
    }
    while (pos(QStringLiteral("<"), origHtml) > 0) {
        const QString noHtml = midStr(origHtml, QStringLiteral("<"), QStringLiteral(">"), true);
        if (noHtml.isEmpty()) {
            break;
        }
        origHtml = stringReplace(origHtml, noHtml, QString(), true);
    }
    origHtml = stupedHtmlToTextConverter(origHtml);
    // Legacy replaces these accented-letter entities by a literal '?' (L-125).
    static const char16_t* const kAccented[] = {
        u"&aacute;", u"&atilde;", u"&ccedil;", u"&eacute;", u"&ecirc;",  u"&iacute;", u"&oacute;",
        u"&ocirc;",  u"&otilde;", u"&Aacute;", u"&Atilde;", u"&Ccedil;", u"&Eacute;", u"&Ecirc;",
        u"&Iacute;", u"&Oacute;", u"&Ocirc;",  u"&Otilde;", u"&Uacute;", u"&uacute;", u"&uuml;",
    };
    for (const char16_t* e : kAccented) {
        origHtml = stringReplace(origHtml, QString::fromUtf16(e), QStringLiteral("?"));
    }
    return origHtml;
}

} // namespace cg::vector
