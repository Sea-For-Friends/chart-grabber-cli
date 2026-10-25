// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

// New code (no legacy unit). Inherits: CmdLineArgProcessor/u_CmdLineArgProcessorAPI.pas:67-134 (switch form,
// help), u_CmdLineArgProcessorHelpers.pas:124-143 (decimal point)
#include "cg/app/job.h"

#include "cg/core/config_provider.h"
#include "cg/core/config_provider_ini.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace cg::app {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(Job)
};

// Setter: returns an empty string on success, else the message without the option name.
// base: directory a relative path is resolved against ("" = the current directory).
using Setter = std::function<QString(Job&, const QString& value, const QString& base)>;

enum class Group : std::uint8_t { Job, Download, Common };

struct Opt {
    const char* flag;      // long name; for a short-only option the one letter
    const char* key;       // INI key in [Job] / [Download]; nullptr = not a job-file key
    const char* valueName; // nullptr = switch without value
    const char* help;
    Group group;
    bool isArea;
    Setter set;              // empty = handled by parseJob itself
    bool repeatable = false; // every value of the flag is applied, in order (T-0291: --maps)
};

QString quoted(const QString& text) {
    return QStringLiteral("\"%1\"").arg(text);
}

QString absPath(const QString& value, const QString& base) {
    if (base.isEmpty()) {
        return QFileInfo(value).absoluteFilePath();
    }
    // T-0360: forward slashes as QFileInfo gives them for a flag (T-0335); a job-file "charts\x" must not
    // print mixed.
    return QDir::fromNativeSeparators(QDir(base).absoluteFilePath(value));
}

Setter strOpt(QString Job::* member, bool trim = false) {
    return [member, trim](Job& job, const QString& value, const QString&) {
        job.*member = trim ? value.trimmed() : value;
        return QString();
    };
}

Setter pathOpt(QString Job::* member) {
    return [member](Job& job, const QString& value, const QString& base) {
        job.*member = absPath(value, base);
        return QString();
    };
}

Setter intOpt(int Job::* member, int lo, int hi) {
    return [member, lo, hi](Job& job, const QString& value, const QString&) {
        bool ok = false;
        const int number = QLocale::c().toInt(value, &ok);
        if (!ok || number < lo || number > hi) {
            return Tr::tr("%1 is not an integer from %2 to %3").arg(quoted(value)).arg(lo).arg(hi);
        }
        job.*member = number;
        return QString();
    };
}

Setter boolOpt(bool Job::* member) {
    return [member](Job& job, const QString& value, const QString&) {
        const QString text = value.trimmed();
        if (text == QLatin1String("1") || text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
            job.*member = true;
        } else if (text == QLatin1String("0") ||
                   text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
            job.*member = false;
        } else {
            return Tr::tr("%1 is not a boolean (1, 0, true, false)").arg(quoted(value));
        }
        return QString();
    };
}

// YYYY-MM-DD (00:00) or YYYY-MM-DDThh:mm:ss, local time. QDateTime::fromString with a format is strict on the
// field width; the round trip rejects anything that Qt would normalise.
Setter dateOpt(QDateTime Job::* member) {
    return [member](Job& job, const QString& value, const QString&) {
        QDateTime parsed;
        if (value.size() == 10) {
            const QDate date = QDate::fromString(value, QStringLiteral("yyyy-MM-dd"));
            if (date.isValid() && date.toString(QStringLiteral("yyyy-MM-dd")) == value) {
                parsed = date.startOfDay();
            }
        } else {
            const QString format = QStringLiteral("yyyy-MM-dd'T'hh:mm:ss");
            parsed = QDateTime::fromString(value, format);
            if (parsed.isValid() && parsed.toString(format) != value) {
                parsed = QDateTime();
            }
        }
        if (!parsed.isValid()) {
            return Tr::tr("%1 is not a date (YYYY-MM-DD or YYYY-MM-DDThh:mm:ss)").arg(quoted(value));
        }
        job.*member = parsed;
        return QString();
    };
}

std::optional<double> parseDouble(const QString& text) {
    bool ok = false;
    const double number = QLocale::c().toDouble(text.trimmed(), &ok);
    if (!ok || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

// Exactly `count` decimal numbers separated by commas, '.' as the decimal point (str2r).
std::optional<std::vector<double>> parseNumbers(const QString& text, int count) {
    const QStringList parts = text.split(u',');
    if (parts.size() != count) {
        return std::nullopt;
    }
    std::vector<double> numbers;
    for (const QString& part : parts) {
        const std::optional<double> number = parseDouble(part);
        if (!number) {
            return std::nullopt;
        }
        numbers.push_back(*number);
    }
    return numbers;
}

QString setBBox(Job& job, const QString& value, const QString&) {
    const auto numbers = parseNumbers(value, 4);
    if (!numbers) {
        return Tr::tr("%1 is not W,S,E,N in decimal degrees").arg(quoted(value));
    }
    job.bbox = geo::LonLatRect{(*numbers)[0], (*numbers)[3], (*numbers)[2], (*numbers)[1]};
    job.areaKind = AreaKind::BBox;
    return QString();
}

QString setCorridor(Job& job, const QString& value, const QString&) {
    const std::optional<double> number = parseDouble(value);
    if (!number || *number <= 0.0) {
        return Tr::tr("%1 is not a number greater than 0").arg(quoted(value));
    }
    job.corridorNm = *number;
    return QString();
}

QString setZooms(Job& job, const QString& value, const QString&) {
    auto zooms = parseZoomList(value);
    if (!zooms) {
        return zooms.error();
    }
    job.zooms = std::move(*zooms);
    return QString();
}

QString setTime(Job& job, const QString& value, const QString&) {
    auto time = parseSnapshotTime(value.trimmed());
    if (!time) {
        return time.error();
    }
    job.time = *time;
    return QString();
}

QString setAt(Job& job, const QString& value, const QString&) {
    const auto numbers = parseNumbers(value, 2);
    if (!numbers) {
        return Tr::tr("%1 is not LON,LAT in decimal degrees").arg(quoted(value));
    }
    job.probeAt = geo::LonLat{(*numbers)[0], (*numbers)[1]};
    return QString();
}

Setter areaPath(QString Job::* member, AreaKind kind) {
    return [member, kind](Job& job, const QString& value, const QString& base) {
        job.*member = absPath(value, base);
        job.areaKind = kind;
        return QString();
    };
}

// Order of the rows is the order of helpText().
const std::vector<Opt>& optionTable() {
    static const std::vector<Opt> table = {
        // job
        {"job",
         nullptr,
         "FILE",
         QT_TRANSLATE_NOOP("Job", "read the options below from an INI job file"),
         Group::Job,
         false,
         {}},
        {"source", "Source", "ID",
         QT_TRANSLATE_NOOP("Job", "base map: GUID (with or without braces) or name"), Group::Job, false,
         strOpt(&Job::source)},
        {"overlay", "Overlay", "ID", QT_TRANSLATE_NOOP("Job", "overlay layer: GUID or name"), Group::Job,
         false, strOpt(&Job::overlay)},
        {"bbox", "BBox", "W,S,E,N",
         QT_TRANSLATE_NOOP("Job", "area: rectangle in decimal degrees, '.' as decimal point"), Group::Job,
         true, setBBox},
        {"polygon", "Polygon", "FILE", QT_TRANSLATE_NOOP("Job", "area: .hlg selection file"), Group::Job,
         true, areaPath(&Job::polygonFile, AreaKind::PolygonFile)},
        {"route", "Route", "FILE", QT_TRANSLATE_NOOP("Job", "area: GPX file, the route to follow"),
         Group::Job, true, areaPath(&Job::routeFile, AreaKind::Route)},
        {"route-name", "RouteName", "NAME",
         QT_TRANSLATE_NOOP("Job", "which route or track of the GPX file, by its name"), Group::Job, false,
         strOpt(&Job::routeName)},
        {"corridor-nm", "CorridorNm", "N",
         QT_TRANSLATE_NOOP("Job", "total width of the corridor in nautical miles; required with --route"),
         Group::Job, false, setCorridor},
        {"zooms", "Zooms", "LIST", QT_TRANSLATE_NOOP("Job", "zoom levels, 0-based, such as 8-12,14"),
         Group::Job, false, setZooms},
        {"time", "Time", "VALUE",
         QT_TRANSLATE_NOOP("Job", "frame of a snapshot source: 2026-10-18T12:00:00Z or +24h; "
                                  "default latest"),
         Group::Job, false, setTime},
        {"out", "Out", "PATH", QT_TRANSLATE_NOOP("Job", "output file of the base map, or a directory"),
         Group::Job, false, pathOpt(&Job::out)},
        {"overlay-out", "OverlayOut", "FILE", QT_TRANSLATE_NOOP("Job", "output file of the overlay"),
         Group::Job, false, pathOpt(&Job::overlayOut)},
        {"name", "Name", "TEXT", QT_TRANSLATE_NOOP("Job", "MBTiles name of the base file"), Group::Job, false,
         strOpt(&Job::name, true)},
        {"description", "Description", "TEXT",
         QT_TRANSLATE_NOOP("Job", "MBTiles description of the base file"), Group::Job, false,
         strOpt(&Job::description, true)},
        {"attribution", "Attribution", "TEXT",
         QT_TRANSLATE_NOOP("Job", "MBTiles attribution of the base file"), Group::Job, false,
         strOpt(&Job::attribution, true)},
        {"overwrite", "Overwrite", nullptr, QT_TRANSLATE_NOOP("Job", "replace an existing output file"),
         Group::Job, false, boolOpt(&Job::overwrite)},
        // download
        {"threads", "Threads", "N", QT_TRANSLATE_NOOP("Job", "workers per source, 1 to 64"), Group::Download,
         false, intOpt(&Job::threads, 1, 64)},
        {"restart", "Restart", nullptr,
         QT_TRANSLATE_NOOP("Job", "ignore saved sessions, scan from the first tile"), Group::Download, false,
         boolOpt(&Job::restart)},
        {"replace", "Replace", nullptr, QT_TRANSLATE_NOOP("Job", "download tiles that already exist"),
         Group::Download, false, boolOpt(&Job::replace)},
        {"replace-if-size-differs", "ReplaceIfSizeDiffers", nullptr,
         QT_TRANSLATE_NOOP("Job", "replace an existing tile when its size differs"), Group::Download, false,
         boolOpt(&Job::replaceIfSizeDiffers)},
        {"replace-older-than", "ReplaceOlderThan", "DATE",
         QT_TRANSLATE_NOOP("Job",
                           "replace an existing tile older than DATE (YYYY-MM-DD or YYYY-MM-DDThh:mm:ss)"),
         Group::Download, false, dateOpt(&Job::replaceOlderThan)},
        {"retry-missing", "RetryMissing", nullptr,
         QT_TRANSLATE_NOOP("Job", "request tiles marked \"not on the server\" again"), Group::Download, false,
         boolOpt(&Job::retryMissing)},
        {"retry-missing-older-than", "RetryMissingOlderThan", "DATE",
         QT_TRANSLATE_NOOP("Job", "only markers older than DATE"), Group::Download, false,
         dateOpt(&Job::retryMissingOlderThan)},
        {"autosave-min", "AutosaveMin", "N",
         QT_TRANSLATE_NOOP("Job", "session autosave interval in minutes; 0 = only at the end"),
         Group::Download, false, intOpt(&Job::autosaveMin, 0, 1440 * 365)},
        {"stall-timeout", "StallTimeout", "SEC",
         QT_TRANSLATE_NOOP("Job", "stall timeout in seconds; 0 = wait for ever"), Group::Download, false,
         intOpt(&Job::stallTimeoutSec, 0, 86400 * 365)},
        {"max-connection-errors", "MaxConnectionErrors", "N",
         QT_TRANSLATE_NOOP("Job", "stop after N tiles in a row failed with a connection error; 0 = never"),
         Group::Download, false, intOpt(&Job::maxConnectionErrors, 0, 1000000)},
        {"save-retries", "SaveRetries", "N", QT_TRANSLATE_NOOP("Job", "retries of a failed tile save"),
         Group::Download, false, intOpt(&Job::saveRetries, 0, 1000)},
        {"strict", "Strict", nullptr,
         QT_TRANSLATE_NOOP("Job", "exit 4 when a tile is neither present nor marked missing"),
         Group::Download, false, boolOpt(&Job::strict)},
        {"allow-webp", "AllowWebp", nullptr, QT_TRANSLATE_NOOP("Job", "accept WebP tiles"), Group::Download,
         false, boolOpt(&Job::allowWebp)},
        {"proxy", "Proxy", "URL",
         QT_TRANSLATE_NOOP("Job",
                           "proxy: [http|https|socks4|socks4a|socks5|socks5h://][user:pass@]host[:port]"),
         Group::Download, false, strOpt(&Job::proxy)},
        {"assume-tile-kb", "AssumeTileKb", "N",
         QT_TRANSLATE_NOOP("Job", "mean tile size in KB when the cache gives no sample"), Group::Download,
         false, intOpt(&Job::assumeTileKb, 0, 1000000)},
        // common (not job-file keys)
        {"maps", nullptr, "DIR",
         QT_TRANSLATE_NOOP("Job", "folder of map-source definitions (*.zmp, *.source.json); required; repeat "
                                  "for more folders, a later folder wins on the same GUID"),
         Group::Common, false,
         [](Job& job, const QString& value, const QString& base) {
             if (value.isEmpty()) {
                 return Tr::tr("an empty folder name");
             }
             job.mapsDirs.push_back(absPath(value, base));
             return QString();
         },
         true},
        {"cache", nullptr, "DIR", QT_TRANSLATE_NOOP("Job", "cache directory"), Group::Common, false,
         pathOpt(&Job::cacheDir)},
        {"filter", nullptr, "TEXT",
         QT_TRANSLATE_NOOP("Job", "sources, probe: only maps whose name contains TEXT"), Group::Common, false,
         strOpt(&Job::filter)},
        {"at", nullptr, "LON,LAT", QT_TRANSLATE_NOOP("Job", "probe: point of the probe tile (default 0,0)"),
         Group::Common, false, setAt},
        {"probe-zoom", nullptr, "Z", QT_TRANSLATE_NOOP("Job", "probe: zoom of the probe tile (default 10)"),
         Group::Common, false, intOpt(&Job::probeZoom, 0, 23)},
        {"v", nullptr, nullptr, QT_TRANSLATE_NOOP("Job", "print every worker log line"), Group::Common, false,
         [](Job& job, const QString&, const QString&) {
             if (job.verbosity < 0) {
                 return Tr::tr("cannot be combined with -q");
             }
             job.verbosity = 1;
             return QString();
         }},
        {"q", nullptr, nullptr, QT_TRANSLATE_NOOP("Job", "errors and results only"), Group::Common, false,
         [](Job& job, const QString&, const QString&) {
             if (job.verbosity > 0) {
                 return Tr::tr("cannot be combined with -v");
             }
             job.verbosity = -1;
             return QString();
         }},
        {"format", nullptr, "FORMAT",
         QT_TRANSLATE_NOOP("Job", "text (default) or tsv: machine-readable records on stdout"), Group::Common,
         false,
         [](Job& job, const QString& value, const QString&) {
             if (value == QLatin1String("text")) {
                 job.format = OutputFormat::Text;
             } else if (value == QLatin1String("tsv")) {
                 job.format = OutputFormat::Tsv;
             } else {
                 return Tr::tr("expected text or tsv");
             }
             return QString();
         }},
        // before no-progress: the table is applied in order, so --no-progress wins over --progress=auto
        {"progress", nullptr, "MODE",
         QT_TRANSLATE_NOOP("Job", "auto (default), lines (machine-readable records on stderr) or off"),
         Group::Common, false,
         [](Job& job, const QString& value, const QString&) {
             if (value == QLatin1String("auto")) {
                 job.progressMode = ProgressMode::Auto;
             } else if (value == QLatin1String("lines")) {
                 job.progressMode = ProgressMode::Lines;
             } else if (value == QLatin1String("off")) {
                 job.progressMode = ProgressMode::Off;
             } else {
                 return Tr::tr("expected auto, lines or off");
             }
             job.progress = job.progressMode != ProgressMode::Off;
             return QString();
         }},
        {"no-progress", nullptr, nullptr, QT_TRANSLATE_NOOP("Job", "no progress line"), Group::Common, false,
         [](Job& job, const QString&, const QString&) {
             job.progressMode = ProgressMode::Off;
             job.progress = false;
             return QString();
         }},
        {"stop-on-stdin", nullptr, nullptr,
         QT_TRANSLATE_NOOP(
             "Job", "stop gracefully (exit 143) on a \"stop\" line or EOF on stdin, which must be a pipe"),
         Group::Common, false,
         [](Job& job, const QString&, const QString&) {
             job.stopOnStdin = true;
             return QString();
         }},
        {"h",
         nullptr,
         nullptr,
         QT_TRANSLATE_NOOP("Job", "print this help and exit"),
         Group::Common,
         false,
         {}},
        {"version",
         nullptr,
         nullptr,
         QT_TRANSLATE_NOOP("Job", "print the version and exit"),
         Group::Common,
         false,
         {}},
    };
    return table;
}

QString displayName(const Opt& opt) {
    const QString flag = QString::fromLatin1(opt.flag);
    return (flag.size() == 1 ? QStringLiteral("-") : QStringLiteral("--")) + flag;
}

QStringList optionNames(const Opt& opt) {
    if (QLatin1String(opt.flag) == QLatin1String("h")) {
        return {QStringLiteral("h"), QStringLiteral("help")};
    }
    return {QString::fromLatin1(opt.flag)};
}

struct Command2 {
    const char* word;
    Command command;
};
constexpr Command2 kCommands[] = {
    {"sources", Command::Sources}, {"estimate", Command::Estimate}, {"download", Command::Download},
    {"export", Command::Export},   {"run", Command::Run},           {"resume", Command::Resume},
    {"probe", Command::Probe},
};

std::unexpected<CliError> usageError(QString message) {
    return std::unexpected(CliError{2, std::move(message)});
}

// Spec 1.7 S-3: two areas are a refusal (exit 3, no usage line), not a syntax error.
std::unexpected<CliError> areaClashError(QString message) {
    return std::unexpected(CliError{3, std::move(message)});
}

QString usageLine() {
    return Tr::tr("Usage: chart-grabber-cli COMMAND [options]; chart-grabber-cli --help");
}

// Applies one value; the message names the option.
QString applyOne(Job& job, const Opt& opt, const QString& value, const QString& base, const QString& origin) {
    const QString error = opt.set(job, value, base);
    if (error.isEmpty()) {
        return {};
    }
    const QString where = origin.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(origin);
    return QStringLiteral("%1: %2%3").arg(displayName(opt), error, where);
}

// areaClash is set when the error is the second area (exit 3); every other error is a usage error.
QString applyJobFile(Job& job, const QString& path, bool& areaClash) {
    // ConfigProviderIni::open succeeds on a missing file (TIniFile approach): a typo must not pass silently.
    if (!QFileInfo::exists(path)) {
        return Tr::tr("--job: cannot read %1").arg(quoted(path));
    }
    auto opened = core::ConfigProviderIni::open(path);
    if (!opened) {
        return Tr::tr("--job: cannot read %1: %2").arg(quoted(path), opened.error().what());
    }
    const QString base = QFileInfo(path).absolutePath();
    int areas = 0;
    for (const Group group : {Group::Job, Group::Download}) {
        const QString section = group == Group::Job ? QStringLiteral("Job") : QStringLiteral("Download");
        const core::IConfigProviderPtr provider = (*opened)->subItem(section);
        if (!provider) {
            continue;
        }
        for (const QString& key : provider->valueNames()) {
            const auto row = std::ranges::find_if(optionTable(), [&](const Opt& opt) {
                return opt.key != nullptr && opt.group == group &&
                       key.compare(QLatin1String(opt.key), Qt::CaseInsensitive) == 0;
            });
            if (row == optionTable().end()) {
                return Tr::tr("unknown key %1 in [%2] of %3").arg(quoted(key), section, path);
            }
            const QString origin = Tr::tr("key %1 in [%2] of %3").arg(key, section, path);
            QString error = applyOne(job, *row, provider->readString(key), base, origin);
            if (!error.isEmpty()) {
                return error;
            }
            if (row->isArea && ++areas > 1) {
                areaClash = true;
                return Tr::tr("--bbox, --polygon, --route: only one may be given (%1)").arg(path);
            }
        }
    }
    return {};
}

void clearArea(Job& job) {
    job.areaKind = AreaKind::None;
    job.bbox = geo::LonLatRect{};
    job.polygonFile.clear();
    job.routeFile.clear();
}

QString checkJob(const Job& job) {
    switch (job.command) {
    case Command::Estimate:
    case Command::Download:
    case Command::Export:
    case Command::Run:
        break;
    default:
        return {};
    }
    if (job.areaKind == AreaKind::None) {
        return Tr::tr("--bbox, --polygon, --route: one of them is required");
    }
    if (job.areaKind == AreaKind::Route && job.corridorNm <= 0.0) {
        return Tr::tr("--corridor-nm: required with --route");
    }
    if (job.areaKind != AreaKind::Route && job.corridorNm > 0.0) {
        return Tr::tr("--corridor-nm: only valid with --route");
    }
    if (job.source.isEmpty() && job.overlay.isEmpty()) {
        return Tr::tr("--source, --overlay: at least one is required");
    }
    if ((job.command == Command::Export || job.command == Command::Run) && job.out.isEmpty()) {
        return Tr::tr("--out: required for export and run");
    }
    return {};
}

} // namespace

std::expected<std::vector<quint8>, QString> parseZoomList(const QString& text) {
    static const QRegularExpression item(QStringLiteral("^(\\d{1,3})(?:-(\\d{1,3}))?$"));
    const QString bad =
        Tr::tr("%1 is not a zoom list (0-based, such as 8-12,14; levels 0 to 23)").arg(quoted(text));
    std::vector<quint8> zooms;
    for (const QString& part : text.split(u',')) {
        const QRegularExpressionMatch match = item.match(part.trimmed());
        if (!match.hasMatch()) {
            return std::unexpected(bad);
        }
        const int from = match.captured(1).toInt();
        const int to = match.captured(2).isEmpty() ? from : match.captured(2).toInt();
        if (from > to || to > 23) {
            return std::unexpected(bad);
        }
        for (int zoom = from; zoom <= to; ++zoom) {
            zooms.push_back(static_cast<quint8>(zoom));
        }
    }
    std::ranges::sort(zooms);
    zooms.erase(std::unique(zooms.begin(), zooms.end()), zooms.end());
    return zooms;
}

std::expected<Job, CliError> parseJob(const QStringList& arguments) {
    QCommandLineParser parser; // parse(), never process(): process() would exit the process
    for (const Opt& opt : optionTable()) {
        QCommandLineOption option(optionNames(opt), QString());
        if (opt.valueName != nullptr) {
            option.setValueName(QString::fromLatin1(opt.valueName));
        }
        parser.addOption(option);
    }
    // --datadir= / --portable belong to AppPaths::detect: accepted and ignored here.
    parser.addOption(QCommandLineOption(QStringLiteral("datadir"), QString(), QStringLiteral("DIR")));
    parser.addOption(QCommandLineOption(QStringLiteral("portable")));
    if (!parser.parse(arguments)) {
        return usageError(parser.errorText());
    }

    Job job;
    if (parser.isSet(QStringLiteral("h"))) {
        job.command = Command::Help;
        return job;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        job.command = Command::Version;
        return job;
    }

    const QStringList positionals = parser.positionalArguments();
    if (positionals.isEmpty()) {
        return usageError(Tr::tr("no command given. %1").arg(usageLine()));
    }
    const auto word = std::ranges::find_if(
        kCommands, [&](const Command2& entry) { return positionals.first() == QLatin1String(entry.word); });
    if (word == std::end(kCommands)) {
        return usageError(Tr::tr("unknown command %1. %2").arg(quoted(positionals.first()), usageLine()));
    }
    job.command = word->command;
    if (job.command == Command::Resume) {
        job.slsFiles = positionals.mid(1);
        if (job.slsFiles.isEmpty()) {
            return usageError(Tr::tr("resume: at least one .sls file is required"));
        }
    } else if (positionals.size() > 1) {
        return usageError(Tr::tr("unexpected argument %1").arg(quoted(positionals.at(1))));
    }

    // The file first, the flags second: a flag beats the same key of the file.
    if (parser.isSet(QStringLiteral("job"))) {
        bool areaClash = false;
        const QString error = applyJobFile(job, parser.value(QStringLiteral("job")), areaClash);
        if (!error.isEmpty()) {
            return areaClash ? areaClashError(error) : usageError(error);
        }
    }
    const auto isGiven = [&](const Opt& opt) {
        return opt.set && parser.isSet(QString::fromLatin1(opt.flag));
    };
    const auto areaFlags =
        std::ranges::count_if(optionTable(), [&](const Opt& opt) { return opt.isArea && isGiven(opt); });
    if (areaFlags > 1) {
        return areaClashError(Tr::tr("--bbox, --polygon, --route: only one may be given"));
    }
    if (areaFlags == 1) {
        clearArea(job); // a flag's area replaces the file's
    }
    for (const Opt& opt : optionTable()) {
        if (!isGiven(opt)) {
            continue;
        }
        const QString flag = QString::fromLatin1(opt.flag);
        const QStringList values = opt.repeatable             ? parser.values(flag)
                                   : opt.valueName != nullptr ? QStringList{parser.value(flag)}
                                                              : QStringList{QStringLiteral("1")};
        for (const QString& value : values) {
            const QString error = applyOne(job, opt, value, QString(), QString());
            if (!error.isEmpty()) {
                return usageError(error);
            }
        }
    }

    const QString error = checkJob(job);
    if (!error.isEmpty()) {
        return usageError(error);
    }
    return job;
}

QString helpText() {
    QString text;
    text += Tr::tr("Usage:") + QLatin1Char('\n');
    const char* const usage[] = {
        "chart-grabber-cli sources   [--filter TEXT] [common]",
        "chart-grabber-cli estimate  JOB [common]",
        "chart-grabber-cli download  JOB [download options] [common]",
        "chart-grabber-cli export    JOB [common]",
        "chart-grabber-cli run       JOB [download options] [common]",
        "chart-grabber-cli resume    FILE.sls [FILE.sls ...] [download options] [common]",
        "chart-grabber-cli probe     [--filter TEXT] [--at LON,LAT] [--probe-zoom Z] [common]",
        "chart-grabber-cli -h | --help | --version",
    };
    for (const char* line : usage) {
        text += QStringLiteral("    ") + QString::fromLatin1(line) + QLatin1Char('\n');
    }
    const struct {
        Group group;
        const char* title;
    } groups[] = {
        {Group::Job, QT_TRANSLATE_NOOP("Job", "Job options (flags or --job FILE, section [Job]):")},
        {Group::Download, QT_TRANSLATE_NOOP("Job", "Download options (flags or section [Download]):")},
        {Group::Common, QT_TRANSLATE_NOOP("Job", "Common options:")},
    };
    for (const auto& group : groups) {
        text += QLatin1Char('\n') + Tr::tr(group.title) + QLatin1Char('\n');
        for (const Opt& opt : optionTable()) {
            if (opt.group != group.group) {
                continue;
            }
            QString names = QLatin1String(opt.flag) == QLatin1String("h") ? QStringLiteral("-h, --help")
                                                                          : displayName(opt);
            if (opt.valueName != nullptr) {
                names += QStringLiteral("=") + QString::fromLatin1(opt.valueName);
            }
            text += QStringLiteral("    %1  %2\n").arg(names.leftJustified(30), Tr::tr(opt.help));
        }
        if (group.group == Group::Common) {
            text += QStringLiteral("    %1  %2\n")
                        .arg(QStringLiteral("--datadir=DIR, --portable").leftJustified(30),
                             Tr::tr("data directory, portable mode"));
        }
    }
    text += QLatin1Char('\n') +
            Tr::tr("Zoom levels are 0-based (OpenCPN, MBTiles); SAS.Planet shows them 1-based.") +
            QLatin1Char('\n');
    text += Tr::tr("--corridor-nm is the total width of the corridor.") + QLatin1Char('\n');
    text += Tr::tr("Map sources are not built in: every command except -h and --version needs at least one "
                   "--maps folder.") +
            QLatin1Char('\n');
    text +=
        Tr::tr("probe requests one tile per source (the tile under --at at --probe-zoom) and prints GUID, "
               "name and result; exit 0 when every probed source passed, 4 otherwise. Sources that cannot "
               "be downloaded are skipped. probe writes the probed tile into the cache.") +
        QLatin1Char('\n');
    text += QLatin1Char('\n') + Tr::tr("Examples:") + QLatin1Char('\n');
    text += QStringLiteral("    chart-grabber-cli estimate --maps=./map-sources --source=\"OpenStreetMap\" "
                           "--bbox=24.5,59.3,24.9,59.5 --zooms=8-12\n");
    text += QStringLiteral("    chart-grabber-cli run --job=passage.conf --maps=./map-sources --threads=4 "
                           "--out=passage.mbtiles\n");
    return text;
}

} // namespace cg::app
