// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "TranslationsModel.h"

#include <QDebug>
#include <QLibraryInfo>
#include <QTranslator>
#include <algorithm>
#include <memory>
#include <utility>

#include "BuildConfig.h"
#include "FileSystem.h"
#include "Json.h"
#include "net/ChecksumValidator.h"
#include "net/NetJob.h"

#include "POTranslator.h"

#include "Application.h"
#include "settings/SettingsObject.h"
#include "zero/ZeroLanguage.h"

static constexpr QLatin1String g_defaultLangCode("en_US");

namespace {
enum class FileType : std::uint8_t { None, Qm, Po };

QString getSystemLocaleName()
{
    return QLocale::system().name();
}

QString getSystemLanguage()
{
    return getSystemLocaleName().split('_').front();
}
}  // namespace

struct Language {
    Language() : updated(true) {}

    explicit Language(QString _key) : key(std::move(_key)), updated(key == g_defaultLangCode) { locale = QLocale(key); }

    QString languageName() const
    {
        QString result;
        if (key == "es_UY") {
            result = u8"Español de Latinoamérica";
        } else if (key == "en_NZ") {
            result = u8"New Zealand English";  // No idea why qt translates this to just english and not to New Zealand English
        } else if (key == "en@pirate" || key == "en_US@pirate") {
            result = u8"Tongue of the High Seas";
        } else if (key == "en@uwu" || key == "en_US@uwu") {
            result = u8"Cute Engwish";
        } else if (key == "tok") {
            result = u8"toki pona";
        } else if (key == "zh_Hant_HK") {
            result = u8"繁體中文 (香港)";
        } else if (key == "nan") {
            result = u8"閩南語";  // Using traditional Chinese script. Not sure if we should use simplified instead?
        } else {
            result = locale.nativeLanguageName();
        }

        if (result.isEmpty()) {
            result = key;
        }
        return result;
    }

    float percentTranslated() const
    {
        if (total == 0) {
            return 100.0F;
        }
        return 100.0F * static_cast<float>(translated) / static_cast<float>(total);
    }

    void setTranslationStats(const unsigned _translated, const unsigned _untranslated, const unsigned _fuzzy)
    {
        translated = _translated;
        untranslated = _untranslated;
        fuzzy = _fuzzy;
        total = translated + untranslated + fuzzy;
    }

    bool isOfSameNameAs(const Language& other) const { return key == other.key; }

    bool isIdenticalTo(const Language& other) const
    {
        return (key == other.key && fileName == other.fileName && fileSize == other.fileSize && fileSha1 == other.fileSha1 &&
                translated == other.translated && fuzzy == other.fuzzy && total == other.total && localFileType == other.localFileType);
    }

    Language& apply(const Language& other)
    {
        if (!isOfSameNameAs(other)) {
            return *this;
        }
        fileName = other.fileName;
        fileSize = other.fileSize;
        fileSha1 = other.fileSha1;
        translated = other.translated;
        fuzzy = other.fuzzy;
        total = other.total;
        localFileType = other.localFileType;
        return *this;
    }

    QString key;
    QLocale locale;
    bool updated;

    QString fileName = QString();
    std::size_t fileSize = 0;
    QString fileSha1 = QString();

    unsigned translated = 0;
    unsigned untranslated = 0;
    unsigned fuzzy = 0;
    unsigned total = 0;

    FileType localFileType = FileType::None;
};

struct TranslationsModel::Private {
    QDir m_dir;

    // initial state is just english
    QList<Language> m_languages = { Language(g_defaultLangCode) };

    QString m_selectedLanguage = g_defaultLangCode;  // ZERO: a ZERO code (en_US or es), see zero/ZeroLanguage.h
    std::unique_ptr<QTranslator> m_qtTranslator;
    std::unique_ptr<QTranslator> m_qtBaseTranslator;  // ZERO: qtbase_<lang>.qm, where Qt 6 keeps the texts of its standard dialogs
    std::unique_ptr<QTranslator> m_appTranslator;
    std::unique_ptr<QTranslator> m_zeroTranslator;  // ZERO: its own strings, installed after the others so that they win

    Net::Request* m_indexTask = nullptr;
    QString m_downloadingTranslation;
    NetJob::Ptr m_downloadJob;
    NetJob::Ptr m_indexJob;
    QString m_nextDownload;

    QFileSystemWatcher* watcher = nullptr;

    bool m_noLanguageSet = false;
};

TranslationsModel::TranslationsModel(const QString& path, QObject* parent) : QAbstractListModel(parent)
{
    d = std::make_unique<Private>();
    d->m_dir.setPath(path);
    // ZERO: only English and Español; whatever Prism saved is read as the nearest of the two
    d->m_selectedLanguage = Zero::Language::initialCode(APPLICATION->settings()->get("Language").toString(), QLocale::system());
    FS::ensureFolderPathExists(path);
    reloadLocalFiles();

    d->watcher = new QFileSystemWatcher(this);
    connect(d->watcher, &QFileSystemWatcher::directoryChanged, this, &TranslationsModel::translationDirChanged);
    d->watcher->addPath(d->m_dir.canonicalPath());
}

TranslationsModel::~TranslationsModel() = default;

void TranslationsModel::translationDirChanged(const QString& path)
{
    qDebug() << "Dir changed:" << path;
    if (!d->m_noLanguageSet) {
        reloadLocalFiles();
    }
    selectLanguage(selectedLanguage());
}

void TranslationsModel::indexReceived()
{
    qDebug() << "Got translations index!";
    d->m_indexJob.reset();
    reloadLocalFiles();

    // ZERO: Prism's index is here. Install Prism's file when it is already on disk (ZERO's translator goes on top again) and download
    // it when it is not. Which language is chosen is ZERO's decision, not the system's (see zero/ZeroLanguage.h).
    const QString prismKey = prismKeyOfSelection();
    if (!prismKey.isEmpty()) {
        selectLanguage(selectedLanguage());
        updateLanguage(prismKey);
    }
}

namespace {
void readIndex(const QString& path, QMap<QString, Language>& languages)
{
    auto parse = [&languages, &path] -> Result<> {
        TRY_INTO(const auto& doc, Json::requireObject(path))

        TRY_INTO(const auto& fileType, Json::requireString(doc, "file_type"))
        if (fileType != "MMC-TRANSLATION-INDEX") {
            return std::unexpected("index file is of unknown file type " + fileType);
        }
        TRY_INTO(const auto& version, Json::requireInteger(doc, "version"))
        if (version > 2) {
            return std::unexpected(QString("index file is of unknown format version %1").arg(version));
        }
        TRY_INTO(const auto& langObjs, Json::requireObject(doc, "languages"))
        for (auto iter = langObjs.begin(); iter != langObjs.end(); ++iter) {
            if (iter.key() == g_defaultLangCode) {
                continue;
            }
            Language lang(iter.key());

            TRY_INTO(const auto& langObj, Json::requireObject(iter.value()))
            lang.setTranslationStats(langObj.value("translated").toInt(), langObj.value("untranslated").toInt(),
                                     langObj.value("fuzzy").toInt());
            TRY_INTO(lang.fileName, Json::requireString(langObj, "file"))
            TRY_INTO(lang.fileSha1, Json::requireString(langObj, "sha1"))
            TRY_INTO(lang.fileSize, Json::requireInteger(langObj, "size"))

            languages.insert(lang.key, lang);
        }
        return {};
    };
    if (auto res = parse(); !res) {
        qCritical() << "Translations Download Failed:" << res.error();
    }
}
}  // namespace

void TranslationsModel::reloadLocalFiles()
{
    QMap<QString, Language> languages = { { g_defaultLangCode, Language(g_defaultLangCode) } };

    const auto indexPath = d->m_dir.absoluteFilePath("index_v2.json");
    if (!QFileInfo::exists(indexPath)) {
        if (Zero::Language::isSpanish(d->m_selectedLanguage))  // ZERO: English needs nothing from Prism's index
            downloadIndex();
        return;
    }
    readIndex(indexPath, languages);
    // ZERO: only English and Español are offered; Prism's other languages are dropped from the list
    for (auto it = languages.begin(); it != languages.end();) {
        if (it.key() != g_defaultLangCode && !Zero::Language::isSpanish(it.key()))
            it = languages.erase(it);
        else
            ++it;
    }
    auto entries = d->m_dir.entryInfoList({ "mmc_*.qm", "*.po" }, QDir::Files | QDir::NoDotAndDotDot);
    for (auto& entry : entries) {
        auto completeSuffix = entry.completeSuffix();
        QString langCode;
        FileType fileType = FileType::None;
        if (completeSuffix == "qm") {
            langCode = entry.baseName().remove(0, 4);
            fileType = FileType::Qm;
        } else if (completeSuffix == "po") {
            langCode = entry.baseName();
            fileType = FileType::Po;
        } else {
            continue;
        }

        if (langCode != g_defaultLangCode && !Zero::Language::isSpanish(langCode))
            continue;  // ZERO: a file of a language ZERO does not offer
        auto langIter = languages.find(langCode);
        if (langIter != languages.end()) {
            auto& language = *langIter;
            language.localFileType = std::max(fileType, language.localFileType);
        } else {
            if (fileType == FileType::Po) {
                Language localFound(langCode);
                localFound.localFileType = FileType::Po;
                languages.insert(langCode, localFound);
            }
        }
    }

    // changed and removed languages
    for (auto iter = d->m_languages.begin(); iter != d->m_languages.end();) {
        auto& language = *iter;
        auto row = iter - d->m_languages.begin();

        auto updatedLanguageIter = languages.find(language.key);
        if (updatedLanguageIter != languages.end()) {
            if (language.isIdenticalTo(*updatedLanguageIter)) {
                languages.remove(language.key);
            } else {
                language.apply(*updatedLanguageIter);
                emit dataChanged(index(row), index(row));
                languages.remove(language.key);
            }
            ++iter;
        } else {
            beginRemoveRows(QModelIndex(), row, row);
            iter = d->m_languages.erase(iter);
            endRemoveRows();
        }
    }
    // added languages
    if (languages.isEmpty()) {
        return;
    }
    beginInsertRows(QModelIndex(), 0, d->m_languages.size() + languages.size() - 1);
    for (auto& language : languages) {
        d->m_languages.append(language);
    }

    const auto comp = [systemLocale = getSystemLocaleName(), systemLanguage = getSystemLanguage()](const Language& a, const Language& b) {
        if (a.key != b.key) {
            if (a.key == systemLocale || a.key == systemLanguage) {
                return true;
            }
            if (b.key == systemLocale || b.key == systemLanguage) {
                return false;
            }
        }
        return a.languageName().toLower() < b.languageName().toLower();
    };
    std::ranges::sort(d->m_languages, comp);
    endInsertRows();
}

namespace {
enum class Column : std::uint8_t { Language, Completeness };
}

QVariant TranslationsModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const int row = index.row();
    const auto column = static_cast<Column>(index.column());

    if (row < 0 || row >= d->m_languages.size()) {
        return {};
    }

    auto& lang = d->m_languages[row];
    switch (role) {
        case Qt::DisplayRole: {
            switch (column) {
                case Column::Language: {
                    return lang.languageName();
                }
                case Column::Completeness: {
                    return QString("%1%").arg(lang.percentTranslated(), 3, 'f', 1);
                }
            }
            qWarning("TranslationModel::data not implemented when role is DisplayRole");
        }
        case Qt::ToolTipRole: {
            return tr("%1:\n%2 translated\n%3 fuzzy\n%4 total")
                .arg(lang.key, QString::number(lang.translated), QString::number(lang.fuzzy), QString::number(lang.total));
        }
        case Qt::UserRole:
            return lang.key;
        default:
            return {};
    }
}

QVariant TranslationsModel::headerData(int section, const Qt::Orientation orientation, const int role) const
{
    auto column = static_cast<Column>(section);
    if (role == Qt::DisplayRole) {
        switch (column) {
            case Column::Language: {
                return tr("Language");
            }
            case Column::Completeness: {
                return tr("Completeness");
            }
        }
    } else if (role == Qt::ToolTipRole) {
        switch (column) {
            case Column::Language: {
                return tr("The native language name.");
            }
            case Column::Completeness: {
                return tr("Completeness is the percentage of fully translated strings, not counting automatically guessed ones.");
            }
        }
    }
    return QAbstractListModel::headerData(section, orientation, role);
}

int TranslationsModel::rowCount([[maybe_unused]] const QModelIndex& parent) const
{
    return d->m_languages.size();
}

int TranslationsModel::columnCount([[maybe_unused]] const QModelIndex& parent) const
{
    return 2;
}

QList<Language>::Iterator TranslationsModel::findLanguage(const QString& key) const
{
    return std::ranges::find_if(d->m_languages, [key](const Language& lang) { return lang.key == key; });
}

std::optional<Language> TranslationsModel::findLanguageAsOptional(const QString& key) const
{
    auto found = findLanguage(key);
    if (found != d->m_languages.end()) {
        return *found;
    }
    return {};
}

void TranslationsModel::setUseSystemLocale(const bool useSystemLocale) const
{
    // ZERO: dates and numbers follow the chosen language (the setting stays what Prism saved, but is not read any more)
    APPLICATION->settings()->set("UseSystemLocale", useSystemLocale);
    QLocale::setDefault(Zero::Language::localeFor(d->m_selectedLanguage, QLocale::system()));
}

QString TranslationsModel::prismKeyOfSelection() const
{
    QStringList available;
    for (const auto& language : d->m_languages) {
        available.append(language.key);
    }
    return Zero::Language::prismKey(d->m_selectedLanguage, QLocale::system(), available);
}

bool TranslationsModel::selectLanguage(QString key) const
{
    // ZERO: there are two languages, English and Español. `key` is a ZERO code ("en_US", "es"), a value Prism saved ("es_ES",
    // "de_DE": the nearest of the two is taken) or empty (follow Windows). Prism's own translation is found through its index and
    // installed as upstream does; ZERO's own translator goes on top of everything (zero/ZeroLanguage.h).
    d->m_selectedLanguage = Zero::Language::initialCode(key, QLocale::system());
    const QString zeroCode = d->m_selectedLanguage;
    const QString langCode = prismKeyOfSelection();
    const auto langPtr = langCode.isEmpty() ? std::optional<Language>() : findLanguageAsOptional(langCode);

    // uninstall existing translators if there are any
    for (auto* translator : { &d->m_zeroTranslator, &d->m_appTranslator, &d->m_qtBaseTranslator, &d->m_qtTranslator }) {
        if (*translator) {
            QCoreApplication::removeTranslator(translator->get());
            translator->reset();
        }
    }

    /*
     * FIXME: potential source of crashes:
     * In a multithreaded application, the default locale should be set at application startup, before any non-GUI threads are created.
     * This function is not reentrant.
     */
    QLocale::setDefault(Zero::Language::localeFor(zeroCode, QLocale::system()));

    // if it's the default UI language, finish: English has no translator at all
    if (!Zero::Language::isSpanish(zeroCode)) {
        return true;
    }

    // otherwise install new translations
    bool successful = false;
    // Qt's own texts (the standard buttons and dialogs). The Windows package ships no Qt translations, so this is often absent.
    const auto loadQt = [&successful, &zeroCode](std::unique_ptr<QTranslator>& slot, const QString& baseName) {
        slot = std::make_unique<QTranslator>();
        if (slot->load(baseName + zeroCode, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            qDebug() << "Loading Qt Language File" << (baseName + zeroCode);
            if (QCoreApplication::installTranslator(slot.get())) {
                successful = true;
                return;
            }
            qCritical() << "Loading Qt Language File failed.";
        }
        slot.reset();
    };
    loadQt(d->m_qtTranslator, QStringLiteral("qt_"));
    loadQt(d->m_qtBaseTranslator, QStringLiteral("qtbase_"));

    if (!langPtr.has_value()) {
        qDebug() << "Prism's translation for" << zeroCode << "is not known yet (its index is not here); ZERO's own strings are still translated";
    } else if (langPtr->localFileType == FileType::Po) {
        qDebug() << "Loading Application Language File for" << langCode.toLocal8Bit().constData() << "...";
        d->m_appTranslator = std::make_unique<POTranslator>(FS::PathCombine(d->m_dir.path(), langCode + ".po"));
        if (!d->m_appTranslator->isEmpty()) {
            if (!QCoreApplication::installTranslator(d->m_appTranslator.get())) {
                qCritical() << "Installing Application Language File failed.";
                d->m_appTranslator.reset();
            } else {
                successful = true;
            }
        } else {
            qCritical() << "Loading Application Language File failed.";
            d->m_appTranslator.reset();
        }
    } else if (langPtr->localFileType == FileType::Qm) {
        d->m_appTranslator = std::make_unique<QTranslator>();
        if (d->m_appTranslator->load("mmc_" + langCode, d->m_dir.path())) {
            qDebug() << "Loading Application Language File for" << langCode.toLocal8Bit().constData() << "...";
            if (!QCoreApplication::installTranslator(d->m_appTranslator.get())) {
                qCritical() << "Installing Application Language File failed.";
                d->m_appTranslator.reset();
            } else {
                successful = true;
            }
        } else {
            d->m_appTranslator.reset();
        }
    } else {
        d->m_appTranslator.reset();
    }

    // ZERO's own strings last: the translator installed last is asked first
    d->m_zeroTranslator = Zero::Language::loadZeroTranslator(zeroCode);
    if (d->m_zeroTranslator) {
        if (QCoreApplication::installTranslator(d->m_zeroTranslator.get())) {
            successful = true;
        } else {
            qCritical() << "Installing ZERO's translation failed.";
            d->m_zeroTranslator.reset();
        }
    }
    return successful;
}

QModelIndex TranslationsModel::selectedIndex() const
{
    auto found = findLanguage(prismKeyOfSelection());
    if (found != d->m_languages.end()) {
        return index(std::distance(d->m_languages.begin(), found), 0, QModelIndex());
    }
    return {};
}

QString TranslationsModel::selectedLanguage() const
{
    return d->m_selectedLanguage;
}

void TranslationsModel::downloadIndex()
{
    if (d->m_indexJob || d->m_downloadJob) {
        return;
    }
    qDebug() << "Downloading Translations Index...";
    d->m_indexJob.reset(new NetJob("Translations Index", APPLICATION->network()));
    const MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("translations", "index_v2.json");
    entry->setStale(true);
    auto task = Net::Request::makeCached(QUrl(BuildConfig.TRANSLATION_FILES_URL + "index_v2.json"), entry);
    d->m_indexTask = task.get();
    d->m_indexJob->addNetAction(task);
    d->m_indexJob->setAskRetry(false);
    connect(d->m_indexJob.get(), &NetJob::failed, this, &TranslationsModel::indexFailed);
    connect(d->m_indexJob.get(), &NetJob::succeeded, this, &TranslationsModel::indexReceived);
    d->m_indexJob->start();
}

void TranslationsModel::updateLanguage(const QString& key)
{
    if (key == g_defaultLangCode) {
        qWarning() << "Cannot update builtin language" << key;
        return;
    }
    auto found = findLanguageAsOptional(key);
    if (!found.has_value()) {
        qWarning() << "Cannot update invalid language" << key;
        return;
    }
    if (!found->updated) {
        downloadTranslation(key);
    }
}

void TranslationsModel::downloadTranslation(const QString& key)
{
    if (d->m_downloadJob) {
        d->m_nextDownload = key;
        return;
    }
    auto lang = findLanguageAsOptional(key);
    if (!lang.has_value()) {
        qWarning() << "Will not download an unknown translation" << key;
        return;
    }

    d->m_downloadingTranslation = key;
    const MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("translations", "mmc_" + key + ".qm");
    entry->setStale(true);

    auto dl = Net::Request::makeCached(QUrl(BuildConfig.TRANSLATION_FILES_URL + lang->fileName), entry);
    dl->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, lang->fileSha1));
    dl->setProgress(dl->getProgress(), lang->fileSize);

    d->m_downloadJob.reset(new NetJob("Translation for " + key, APPLICATION->network()));
    d->m_downloadJob->addNetAction(dl);
    d->m_downloadJob->setAskRetry(false);

    connect(d->m_downloadJob.get(), &NetJob::succeeded, this, &TranslationsModel::dlGood);
    connect(d->m_downloadJob.get(), &NetJob::failed, this, &TranslationsModel::dlFailed);

    d->m_downloadJob->start();
}

void TranslationsModel::downloadNext()
{
    if (!d->m_nextDownload.isEmpty()) {
        downloadTranslation(d->m_nextDownload);
        d->m_nextDownload.clear();
    }
}

void TranslationsModel::dlFailed(const QString& reason)
{
    qCritical() << "Translations Download Failed:" << reason;
    d->m_downloadJob.reset();
    downloadNext();
}

void TranslationsModel::dlGood()
{
    qDebug() << "Got translation:" << d->m_downloadingTranslation;

    if (d->m_downloadingTranslation == prismKeyOfSelection()) {
        selectLanguage(d->m_selectedLanguage);
    }
    d->m_downloadJob.reset();
    downloadNext();
}

void TranslationsModel::indexFailed(const QString& reason) const
{
    qCritical() << "Translations Index Download Failed:" << reason;
    d->m_indexJob.reset();
}
