// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chart Grabber contributors. Derived from SAS.Planet, Copyright (C) 2007-Present,
// SAS.Planet development team; modified.

#include "cg/marks/mark_system_config.h"

#include "cg/core/log.h"
#include "cg/marks/c_mark_system.h"
#include "cg/marks/mormot/mark_system_impl_config_orm.h"
#include "cg/marks/sml/mark_system_impl_config_sml.h"

#include <QCoreApplication>

#include <stdexcept>
#include <utility>

namespace cg::marks {

namespace {

// u_MarkSystemConfig.pas:92-147
class MarkSystemConfigStatic final : public IMarkSystemConfigStatic {
public:
    MarkSystemConfigStatic(int id, const QUuid& databaseGUID, QString displayName,
                           MarkSystemImplConfigStaticPtr implConfig)
        : id_(id), databaseGUID_(databaseGUID), displayName_(std::move(displayName)),
          implConfig_(std::move(implConfig)) {}
    [[nodiscard]] int id() const override { return id_; }
    [[nodiscard]] QUuid databaseGUID() const override { return databaseGUID_; }
    [[nodiscard]] QString displayName() const override { return displayName_; }
    [[nodiscard]] MarkSystemImplConfigStaticPtr implConfig() const override { return implConfig_; }

private:
    int id_;
    QUuid databaseGUID_;
    QString displayName_;
    MarkSystemImplConfigStaticPtr implConfig_;
};

// u_MarkSystemConfig.pas:149-164 (_GetDefaultConfig)
MarkSystemConfigStaticPtr defaultConfig(int id, const QUuid& db) {
    MarkSystemImplConfigStaticPtr impl;
    if (db == cSMLMarksDbGUID) {
        impl = std::make_shared<MarkSystemImplConfigSML>(QStringLiteral("marks.sml"), false);
    } else if (db == cORMSQLiteMarksDbGUID) {
        impl = std::make_shared<MarkSystemImplConfigORM>(QStringLiteral("Marks.db3"), false, QString(),
                                                         QString(), QString());
    } else {
        throw std::invalid_argument(QStringLiteral("MarkSystemConfig: Unknown Database GUID: %1")
                                        .arg(db.toString().toUpper())
                                        .toStdString());
    }
    return std::make_shared<MarkSystemConfigStatic>(
        id, db, QCoreApplication::translate("MarkSystem", "My Marks"), impl);
}

QString message(const char* source, int item) {
    return QCoreApplication::translate("MarkSystem", source).arg(item);
}

} // namespace

QStringList MarkSystemConfig::readErrors() const {
    const ReadGuard guard = readLock();
    return readErrors_;
}

void MarkSystemConfig::doReadConfig(const core::IConfigProviderPtr& src) {
    readErrors_.clear();
    if (src) {
        const core::IConfigProviderPtr config = src->subItem(u"MarkSystemConfig");
        if (config) {
            const int count = config->readInt(u"Count", 0);
            activeConfigId_ = config->readInt(u"ActiveIndex", -1);
            for (int i = 0; i < count; ++i) {
                const QString confId = QStringLiteral("Item") + QString::number(i + 1) + u'_';

                const QUuid databaseGUID = config->readUuid(confId + QStringLiteral("Database"), QUuid());
                if (databaseGUID.isNull()) {
                    readErrors_ << message(
                        QT_TRANSLATE_NOOP("MarkSystem",
                                          "MarkSystemConfig: Item #%1 Database GUID read error!"),
                        i + 1);
                    continue;
                }

                const QString displayName =
                    config->readString(confId + QStringLiteral("DisplayName"), QString());

                const QUuid implGUID = config->readUuid(confId + QStringLiteral("Impl"), QUuid());
                if (implGUID.isNull()) {
                    readErrors_ << message(
                        QT_TRANSLATE_NOOP("MarkSystem", "MarkSystemConfig: Item #%1 Impl GUID read error!"),
                        i + 1);
                    continue;
                }

                const QString fileName = config->readString(confId + QStringLiteral("FileName"), QString());
                const bool isReadOnly = config->readBool(confId + QStringLiteral("IsReadOnly"), false);

                MarkSystemImplConfigStaticPtr impl;
                if (implGUID == iidMarkSystemImplConfigSML) {
                    impl = std::make_shared<MarkSystemImplConfigSML>(fileName, isReadOnly);
                } else if (implGUID == iidMarkSystemImplConfigORM) {
                    const QString userName =
                        config->readString(confId + QStringLiteral("UserName"), QString());
                    const QString pass = config->readString(confId + QStringLiteral("Password"), QString());
                    // Cardinal := ReadInteger(...): a negative value wraps (:238), reproduced.
                    const auto cacheSize =
                        static_cast<quint32>(config->readInt(confId + QStringLiteral("CacheSizeMb"), 100));
                    const QString forcedSchemaName =
                        config->readString(confId + QStringLiteral("ForcedSchemaName"), QString());
                    impl = std::make_shared<MarkSystemImplConfigORM>(
                        fileName, isReadOnly, userName, QString(), pass, cacheSize, forcedSchemaName);
                } else {
                    readErrors_ << QCoreApplication::translate(
                                       "MarkSystem", "MarkSystemConfig: Item #%1 has unknown Impl GUID: %2")
                                       .arg(i + 1)
                                       .arg(implGUID.toString().toUpper());
                    continue;
                }

                const MarkSystemConfigStaticPtr item =
                    std::make_shared<MarkSystemConfigStatic>(newId(), databaseGUID, displayName, impl);
                list_.add(item->id(), item);
            }
        }
    }
    if (list_.count() == 0) {
        const MarkSystemConfigStaticPtr item = defaultConfig(newId(), cORMSQLiteMarksDbGUID);
        list_.add(item->id(), item);
        activeConfigId_ = item->id();
    }
    if (activeConfigId_ == 0) {
        activeConfigId_ = 1;
    }
}

void MarkSystemConfig::doWriteConfig(const core::IConfigWriterPtr& dst) const {
    const core::IConfigWriterPtr config = dst->orCreateSubItem(u"MarkSystemConfig");

    config->deleteValues(); // clear section

    config->writeInt(u"Count", list_.count());
    config->writeInt(u"ActiveIndex", activeConfigId_);

    int count = 0;
    for (const MarkSystemConfigStaticPtr& item : list_.values()) {
        const MarkSystemImplConfigStaticPtr impl = item->implConfig();

        ++count;
        const QString confId = QStringLiteral("Item") + QString::number(count) + u'_';

        config->writeUuid(confId + QStringLiteral("Database"), item->databaseGUID());
        config->writeString(confId + QStringLiteral("DisplayName"), item->displayName());

        config->writeString(confId + QStringLiteral("FileName"), impl->fileName());
        config->writeBool(confId + QStringLiteral("IsReadOnly"), impl->isReadOnly());

        if (dynamic_cast<const IMarkSystemImplConfigSML*>(impl.get()) != nullptr) {
            config->writeUuid(confId + QStringLiteral("Impl"), iidMarkSystemImplConfigSML);
        } else if (const auto* orm = dynamic_cast<const IMarkSystemImplConfigORM*>(impl.get())) {
            config->writeUuid(confId + QStringLiteral("Impl"), iidMarkSystemImplConfigORM);
            config->writeString(confId + QStringLiteral("UserName"), orm->userName());
            config->writeString(confId + QStringLiteral("Password"), orm->password());
            config->writeInt(confId + QStringLiteral("CacheSizeMb"), static_cast<qint32>(orm->cacheSizeMb()));
            config->writeString(confId + QStringLiteral("ForcedSchemaName"), orm->forcedSchemaName());
        } else {
            // Legacy: modal ShowErrorMessage (:320); the loop continues with the keys written so far.
            qCWarning(core::logMarks).noquote() << message(
                QT_TRANSLATE_NOOP("MarkSystem", "MarkSystemConfig: Item #%1 has unknown Impl interface!"),
                count);
        }
    }
}

int MarkSystemConfig::count() const {
    const ReadGuard guard = readLock();
    return list_.count();
}

MarkSystemConfigStaticPtr MarkSystemConfig::byId(int id) const {
    const ReadGuard guard = readLock();
    return list_.getById(id); // null when absent
}

int MarkSystemConfig::activeConfigId() const {
    const ReadGuard guard = readLock();
    return activeConfigId_;
}

void MarkSystemConfig::setActiveConfigId(int id) {
    const WriteGuard guard = writeLock();
    if (activeConfigId_ != id) {
        activeConfigId_ = id;
        setChanged();
    }
}

MarkSystemConfigStaticPtr MarkSystemConfig::activeConfig() const {
    const ReadGuard guard = readLock();
    if (activeConfigId_ > 0) {
        return list_.getById(activeConfigId_);
    }
    return nullptr;
}

void MarkSystemConfig::deleteById(int id) {
    const WriteGuard guard = writeLock();
    if (list_.isExists(id)) {
        list_.remove(id);
        if (activeConfigId_ == id) {
            const std::vector<qintptr> ids = list_.ids();
            activeConfigId_ = ids.empty() ? 0 : static_cast<int>(ids.front());
        }
        setChanged();
    }
}

int MarkSystemConfig::add(const QUuid& databaseGUID, const QString& displayName,
                          const MarkSystemImplConfigStaticPtr& implConfig, bool setAsActive) {
    if (!implConfig) {
        return 0; // D-77: legacy dereferences nil in DoWriteConfig (:306), an access violation
    }
    const WriteGuard guard = writeLock();
    const MarkSystemConfigStaticPtr config =
        std::make_shared<MarkSystemConfigStatic>(newId(), databaseGUID, displayName, implConfig);
    list_.add(config->id(), config);
    if (setAsActive) {
        activeConfigId_ = config->id();
    }
    setChanged();
    return config->id();
}

void MarkSystemConfig::update(int id, const QUuid& databaseGUID, const QString& displayName,
                              const MarkSystemImplConfigStaticPtr& implConfig) {
    if (!implConfig) {
        return; // D-77: see add()
    }
    const WriteGuard guard = writeLock();
    if (list_.isExists(id)) {
        list_.replace(id,
                      std::make_shared<MarkSystemConfigStatic>(id, databaseGUID, displayName, implConfig));
        setChanged();
    }
}

std::vector<MarkSystemConfigStaticPtr> MarkSystemConfig::idListStatic() const {
    const ReadGuard guard = readLock();
    return list_.values();
}

} // namespace cg::marks
