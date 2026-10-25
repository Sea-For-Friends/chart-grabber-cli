// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/core/archive_reader_zip.h"

#include "cg/core/log.h"

#include <archive.h>
#include <archive_entry.h>
#include <memory>
#include <mutex>
#include <vector>

namespace cg::core {
namespace {

constexpr size_t kReadBlock = size_t{64} * 1024;

struct ArchiveDeleter {
    void operator()(archive* a) const noexcept {
        archive_read_close(a);
        archive_read_free(a);
    }
};
using ArchivePtr = std::unique_ptr<archive, ArchiveDeleter>;

[[nodiscard]] ArchivePtr openMemory(const QByteArray& bytes) {
    ArchivePtr a(archive_read_new());
    if (!a) {
        return nullptr;
    }
    archive_read_support_format_zip(a.get());
    if (archive_read_open_memory(a.get(), bytes.constData(), static_cast<size_t>(bytes.size())) !=
        ARCHIVE_OK) {
        return nullptr;
    }
    return a;
}

[[nodiscard]] QString entryName(archive_entry* entry, const QString& label, int index) {
    const char* utf8 = archive_entry_pathname_utf8(entry);
    if (utf8 == nullptr && archive_entry_pathname(entry) == nullptr) {
        qCWarning(logConfig) << "ArchiveReaderZip:" << label << ": the name of entry" << index
                             << "cannot be converted; the item is unreachable by name";
    }
    // Needs the process LC_CTYPE to be able to convert the name (Qt sets it from the environment); in
    // the "C" locale a non-ASCII name comes back empty.
    QString name =
        utf8 != nullptr ? QString::fromUtf8(utf8) : QString::fromUtf8(archive_entry_pathname(entry));
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // kpidPath of 7-Zip's zip handler: ReplaceToOsSlashes_Remove_TailSlash (a directory "X/" is "X").
    if (name.endsWith(QLatin1Char('/'))) {
        name.chop(1);
    }
    return name;
}

struct Item {
    QString name;
    bool isDir;
};

class ArchiveReaderZip final : public IArchiveReader {
public:
    ArchiveReaderZip(QByteArray bytes, std::vector<Item> items, QString label, qint64 maxItemBytes)
        : bytes_(std::move(bytes)), items_(std::move(items)), label_(std::move(label)),
          maxItemBytes_(maxItemBytes) {}

    [[nodiscard]] int itemsCount() const override { return static_cast<int>(items_.size()); }

    // TArchiveReadBy7Zip.GetItemByName: the first item with that name (SameText there; exact name
    // first, then case-insensitive here, D-48). Slashes in itemName are normalised like item names.
    [[nodiscard]] std::optional<QByteArray> itemByName(const QString& itemName) const override {
        QString wanted = itemName;
        wanted.replace(QLatin1Char('\\'), QLatin1Char('/'));
        for (const Qt::CaseSensitivity cs : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
            for (int i = 0; i < itemsCount(); ++i) {
                if (items_[static_cast<size_t>(i)].name.compare(wanted, cs) == 0) {
                    QString ignored;
                    return itemByIndex(i, ignored);
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] QString itemNameByIndex(int itemIndex) const override {
        return items_[static_cast<size_t>(itemIndex)].name;
    }

    // A directory gives nil and an empty name. An item that cannot be extracted (encrypted,
    // unsupported method, damaged data, over the size cap) gives nil with its name -- legacy raised
    // there (D-63): here it is logged and kept in status().
    [[nodiscard]] std::optional<QByteArray> itemByIndex(int itemIndex, QString& itemName) const override {
        const Item& item = items_[static_cast<size_t>(itemIndex)];
        if (item.isDir) {
            itemName.clear();
            return std::nullopt;
        }
        itemName = item.name;
        ArchivePtr a = openMemory(bytes_);
        if (!a) {
            return failed(item, QStringLiteral("cannot reopen the archive"));
        }
        archive_entry* entry = nullptr;
        for (int i = 0; i <= itemIndex; ++i) {
            const int rc = archive_read_next_header(a.get(), &entry);
            if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
                return failed(item, QStringLiteral("cannot walk to the item"));
            }
            if (i < itemIndex) {
                archive_read_data_skip(a.get());
            }
        }
        if (archive_entry_is_encrypted(entry) != 0) {
            return failed(item, QStringLiteral("encrypted"));
        }
        if (archive_entry_size_is_set(entry) != 0 && archive_entry_size(entry) > maxItemBytes_) {
            return failed(item, QStringLiteral("larger than the size limit"));
        }
        QByteArray data;
        std::vector<char> block(kReadBlock);
        for (;;) {
            const la_ssize_t got = archive_read_data(a.get(), block.data(), block.size());
            if (got < 0) {
                return failed(item, QString::fromUtf8(archive_error_string(a.get())));
            }
            if (got == 0) {
                break;
            }
            data.append(block.data(), static_cast<qsizetype>(got));
            if (data.size() > maxItemBytes_) {
                return failed(item, QStringLiteral("larger than the size limit"));
            }
        }
        return data;
    }

    [[nodiscard]] Result<void> status() const override {
        const std::scoped_lock lock(statusMutex_);
        return status_;
    }

private:
    std::optional<QByteArray> failed(const Item& item, const QString& why) const {
        const QString what =
            label_ + QStringLiteral(": cannot extract ") + item.name + QStringLiteral(": ") + why;
        qCWarning(logConfig) << "ArchiveReaderZip:" << what;
        const std::scoped_lock lock(statusMutex_);
        if (status_.has_value()) {
            status_ = fail(ErrorCode::DecompressError, what); // the first error stays
        }
        return std::nullopt;
    }

    QByteArray bytes_;
    std::vector<Item> items_;
    QString label_;
    qint64 maxItemBytes_;
    mutable std::mutex statusMutex_;
    mutable Result<void> status_;
};

} // namespace

Result<IArchiveReaderPtr> openArchiveReaderZip(const QByteArray& zipBytes, const QString& label,
                                               qint64 maxItemBytes) {
    ArchivePtr a = openMemory(zipBytes);
    if (!a) {
        return fail(ErrorCode::ParseError, QStringLiteral("not a readable zip archive"));
    }
    std::vector<Item> items;
    for (;;) {
        archive_entry* entry = nullptr;
        const int rc = archive_read_next_header(a.get(), &entry);
        if (rc == ARCHIVE_EOF) {
            break;
        }
        if (rc != ARCHIVE_OK && rc != ARCHIVE_WARN) {
            return fail(ErrorCode::ParseError, QStringLiteral("damaged zip archive: ") +
                                                   QString::fromUtf8(archive_error_string(a.get())));
        }
        items.push_back({entryName(entry, label, static_cast<int>(items.size())),
                         archive_entry_filetype(entry) == AE_IFDIR});
        archive_read_data_skip(a.get());
    }
    if (items.empty() && zipBytes.isEmpty()) {
        return fail(ErrorCode::ParseError, QStringLiteral("empty file is not a zip archive"));
    }
    return IArchiveReaderPtr{
        std::make_shared<ArchiveReaderZip>(zipBytes, std::move(items), label, maxItemBytes)};
}

} // namespace cg::core
