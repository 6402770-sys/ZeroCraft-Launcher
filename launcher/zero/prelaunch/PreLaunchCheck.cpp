// SPDX-License-Identifier: GPL-3.0-only
#include "PreLaunchCheck.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QUrl>

#include <algorithm>

#include "DesktopServices.h"
#include "FileSystem.h"
#include "zero/content/PackRollback.h"
#include "zero/mods/ModHealthScanner.h"
#include "zero/mods/ModRepair.h"

namespace Zero {
namespace PreLaunch {

namespace {

constexpr int kModsCheckVersion = 1;  // bump when the rules of the mods check change: cached "all fine" verdicts are dropped

QString fileLabel(const QString& name)
{
    return name;
}

Fix urlFix(const QString& url)
{
    Fix f;
    f.kind = Fix::Kind::OpenUrl;
    f.label = QCoreApplication::translate("Zero::PreLaunch", "More info");
    f.targets = { url };
    return f;
}

// ---------------------------------------------------------------------------------------------------------------------- malware

QList<Item> malwareItems(const Env& env, const RunOptions& options, CheckResult* stats, bool* complete)
{
    QList<Item> items;
    const Security::SignatureList& list = env.list();
    const QString modsDir = env.modsDir();
    if (!QFileInfo(modsDir).isDir() || !list.error.isEmpty() || list.items.isEmpty()) {
        return items;
    }
    Security::ScanCache cache;
    cache.load(Security::ScanCache::pathFor(env.instanceRoot), list.versionKey);
    Security::FolderScanOptions scanOptions;
    scanOptions.cachedOnly = options.cachedOnly;
    scanOptions.progress = options.progress;
    const Security::FolderScan scan = Security::scanModsFolder(modsDir, list, cache, scanOptions);
    if (!scan.complete) {
        *complete = false;
        return items;
    }
    cache.save();
    if (stats) {
        stats->jarsScanned += scan.scanned;
        stats->jarsFromCache += scan.fromCache;
    }
    for (const Security::InfectedJar& jar : scan.infected) {
        Item item;
        item.id = QStringLiteral("malware:") + jar.fileName;
        item.severity = Severity::Block;
        item.category = Category::Malware;
        item.iconName = QStringLiteral("shield-check");
        item.title = QObject::tr("%1 contains known malware").arg(fileLabel(jar.fileName));
        QStringList names;
        QString infoUrl;
        for (const Security::Hit& hit : jar.hits) {
            if (!names.contains(hit.name)) {
                names.append(hit.name);
            }
            if (infoUrl.isEmpty()) {
                infoUrl = hit.infoUrl;
            }
        }
        item.detail = QObject::tr("It matches %1. Don't start the game with this file: it can steal your accounts and files.").arg(names.join(QStringLiteral(", ")));
        Fix recycle;
        recycle.kind = Fix::Kind::RecycleFile;
        recycle.label = QCoreApplication::translate("Zero::PreLaunch", "Move to Recycle Bin");
        recycle.targets = { jar.path };
        recycle.danger = true;
        item.fixes.append(recycle);
        Fix show;
        show.kind = Fix::Kind::ShowFile;
        show.label = QCoreApplication::translate("Zero::PreLaunch", "Show file");
        show.targets = { jar.path };
        item.fixes.append(show);
        if (!infoUrl.isEmpty()) {
            item.fixes.append(urlFix(infoUrl));
        }
        items.append(item);
    }
    return items;
}

QList<Item> systemItems(const Env& env)
{
    QList<Item> items;
    for (const Security::SystemTrace& trace : Security::findSystemTraces(env.system)) {
        Item item;
        item.id = QStringLiteral("system:") + trace.id;
        item.severity = Severity::Block;
        item.category = Category::System;
        item.iconName = QStringLiteral("shield-check");
        item.title = QObject::tr("This PC shows signs of fractureiser");
        item.detail = QObject::tr("%1 (%2). If you never ran an infected mod this is a false alarm; if you did, follow the cleanup steps before you sign in to anything.")
                          .arg(trace.description, trace.path);
        Fix show;
        show.kind = Fix::Kind::ShowFile;
        show.label = QCoreApplication::translate("Zero::PreLaunch", "Show in folder");
        show.targets = { trace.path };
        item.fixes.append(show);
        if (!trace.infoUrl.isEmpty()) {
            item.fixes.append(urlFix(trace.infoUrl));
        }
        Fix again;
        again.kind = Fix::Kind::Recheck;
        again.label = QCoreApplication::translate("Zero::PreLaunch", "Check again");
        item.fixes.append(again);
        items.append(item);
    }
    return items;
}

// ---------------------------------------------------------------------------------------------------------------------- mods

bool isLaunchRelevant(const Mods::Finding& f)
{
    if (f.confidence != Mods::Confidence::Confirmed) {
        return false;
    }
    switch (f.kind) {
        case Mods::FindingKind::DuplicateMod:
        case Mods::FindingKind::WrongLoader:
        case Mods::FindingKind::MinecraftVersion:
        case Mods::FindingKind::MissingDependency:
            return true;
        default:
            return false;
    }
}

Item itemForFinding(const Mods::Finding& f)
{
    Item item;
    QStringList files = f.fileNames;
    files.sort();
    item.id = QStringLiteral("mods:%1:%2:%3").arg(Mods::findingKindId(f.kind), files.join(QLatin1Char('|')), f.relatedId);
    item.severity = Severity::Warn;
    item.category = Category::Mods;
    item.iconName = f.kind == Mods::FindingKind::DuplicateMod ? QStringLiteral("layers") : QStringLiteral("puzzle");
    item.title = f.title;
    item.detail = f.detail;
    if (!f.actions.isEmpty()) {
        Fix fix;
        fix.kind = Fix::Kind::DisableJars;
        fix.actions = f.actions;
        if (f.kind == Mods::FindingKind::DuplicateMod) {
            fix.label = f.actions.size() == 1 ? QCoreApplication::translate("Zero::PreLaunch", "Disable the older copy") : QCoreApplication::translate("Zero::PreLaunch", "Disable the older copies");
        } else {
            fix.label = QCoreApplication::translate("Zero::PreLaunch", "Disable this mod");
        }
        item.fixes.append(fix);
    } else {
        Fix open;
        open.kind = Fix::Kind::OpenMods;
        open.label = QCoreApplication::translate("Zero::PreLaunch", "Open Mods");
        item.fixes.append(open);
    }
    return item;
}

QList<Item> modsItems(const Env& env, const RunOptions& options, bool* complete)
{
    QList<Item> items;
    if (!env.instanceChecksEnabled) {
        return items;
    }
    const QString modsDir = env.modsDir();
    const Mods::InstanceContext context = env.context ? env.context() : Mods::readInstanceContext(env.instanceRoot);
    if (context.loader == Mods::InstanceLoader::None || !QFileInfo(modsDir).isDir()) {
        return items;
    }
    const QString fingerprint = modsFingerprint(modsDir, context);
    Security::ScanCache cache;
    cache.load(Security::ScanCache::pathFor(env.instanceRoot), env.list().versionKey);
    if (cache.modsCheckClean(fingerprint)) {
        return items;
    }
    if (options.cachedOnly) {
        *complete = false;
        return items;
    }
    const Mods::Report report = Mods::scanModsFolder(modsDir, context);
    for (const Mods::Finding& f : report.findings) {
        if (isLaunchRelevant(f)) {
            items.append(itemForFinding(f));
        }
    }
    cache.setModsCheck(fingerprint, items.isEmpty());
    cache.save();
    return items;
}

// ---------------------------------------------------------------------------------------------------------------------- packs

QList<Item> packItems(const Env& env)
{
    QList<Item> items;
    if (!env.instanceChecksEnabled || !env.packOffer) {
        return items;
    }
    const PackOffer offer = env.packOffer();
    if (!offer.valid) {
        return items;
    }
    Item item;
    item.id = QStringLiteral("packs:rollback");
    item.severity = Severity::Warn;
    item.category = Category::Packs;
    item.iconName = QStringLiteral("images");
    item.title = Content::Packs::Rollback::promptTitle();
    item.detail = Content::Packs::Rollback::promptBody(offer.suspects);
    Fix restore;
    restore.kind = Fix::Kind::RestorePacks;
    restore.label = offer.suspects.isEmpty() ? Content::Packs::Rollback::restoreAllText() : Content::Packs::Rollback::restoreWithoutText(offer.suspects);
    item.fixes.append(restore);
    Fix keep;
    keep.kind = Fix::Kind::KeepPacksOff;
    keep.label = QCoreApplication::translate("Zero::PreLaunch", "Leave them off");
    item.fixes.append(keep);
    items.append(item);
    return items;
}

int categoryOrder(Category c)
{
    return int(c);
}

void sortItems(QList<Item>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.severity != b.severity) {
            return a.severity == Severity::Block;
        }
        return categoryOrder(a.category) < categoryOrder(b.category);
    });
}

}  // namespace

QString Env::modsDir() const
{
    return QDir(gameRoot).filePath(QStringLiteral("mods"));
}

const Security::SignatureList& Env::list() const
{
    return signatures ? *signatures : Security::builtinSignatures();
}

QString modsFingerprint(const QString& modsDir, const Mods::InstanceContext& context)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(kModsCheckVersion) + '\n');
    hash.addData((context.mcVersion + QLatin1Char('|') + Mods::instanceLoaderId(context.loader) + QLatin1Char('|') + context.loaderVersion).toUtf8());
    const QFileInfoList files = QDir(modsDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& info : files) {
        hash.addData(QStringLiteral("\n%1|%2|%3").arg(info.fileName()).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()).toUtf8());
    }
    return QString::fromLatin1(hash.result().toHex());
}

QList<Item> runCategory(const Env& env, Category category)
{
    if (!env.enabled) {
        return {};
    }
    bool complete = true;
    switch (category) {
        case Category::Malware:
            return malwareItems(env, {}, nullptr, &complete);
        case Category::System:
            return systemItems(env);
        case Category::Mods:
            return modsItems(env, {}, &complete);
        case Category::Packs:
            return packItems(env);
    }
    return {};
}

CheckResult runChecks(const Env& env, const RunOptions& options)
{
    CheckResult result;
    if (!env.enabled) {
        return result;
    }
    QElapsedTimer timer;
    timer.start();
    result.items += malwareItems(env, options, &result, &result.complete);
    if (result.complete) {
        result.items += systemItems(env);
        result.items += modsItems(env, options, &result.complete);
    }
    if (result.complete) {
        result.items += packItems(env);
        sortItems(result.items);
    } else {
        result.items.clear();
    }
    result.elapsedMs = timer.elapsed();
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------- the session

Session::Session(Env env, Hooks hooks, QList<Item> items, Rerun rerun)
    : m_env(std::move(env)), m_hooks(std::move(hooks)), m_items(std::move(items)), m_rerun(std::move(rerun))
{
    if (!m_hooks.recycle) {
        m_hooks.recycle = [](const QString& path) { return FS::trash(path); };
    }
    if (!m_hooks.showFile) {
        m_hooks.showFile = [](const QString& path) {
            const QFileInfo info(path);
            if (info.isFile()) {
                QProcess process;
                process.setProgram(QStringLiteral("explorer.exe"));
                process.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
                process.startDetached();
            } else if (info.isDir()) {
                DesktopServices::openPath(info);
            } else if (info.absoluteDir().exists()) {
                DesktopServices::openPath(info.absoluteDir().absolutePath());
            }
        };
    }
    if (!m_hooks.openUrl) {
        m_hooks.openUrl = [](const QString& url) { DesktopServices::openUrl(QUrl(url)); };
    }
}

int Session::openCount() const
{
    int n = 0;
    for (const Item& item : m_items) {
        n += item.fixed ? 0 : 1;
    }
    return n;
}

int Session::openBlockCount() const
{
    int n = 0;
    for (const Item& item : m_items) {
        n += (!item.fixed && item.severity == Severity::Block) ? 1 : 0;
    }
    return n;
}

QString Session::summary() const
{
    const int n = openCount();
    if (n == 0) {
        return QObject::tr("Nothing left to fix.");
    }
    return n == 1 ? QObject::tr("We found 1 thing that may stop the game from working.")
                  : QObject::tr("We found %1 things that may stop the game from working.").arg(n);
}

QString Session::playLabel() const
{
    return openCount() == 0 ? QObject::tr("Play") : QObject::tr("Play anyway");
}

Item* Session::find(const QString& id)
{
    for (Item& item : m_items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

void Session::recheck(Category category)
{
    const QList<Item> fresh = m_rerun ? m_rerun(category) : runCategory(m_env, category);
    for (Item& item : m_items) {
        if (item.category != category || item.fixed) {
            continue;
        }
        const auto it = std::find_if(fresh.begin(), fresh.end(), [&](const Item& f) { return f.id == item.id; });
        if (it == fresh.end()) {
            item.fixed = true;
            item.fixedText = QObject::tr("Fixed");
            item.error.clear();
        } else {
            const QString error = item.error;
            item = *it;  // the row as it is now (the same culprit may have fewer copies left)
            item.error = error;
        }
    }
    for (const Item& f : fresh) {
        if (!find(f.id)) {
            m_items.append(f);
        }
    }
    sortItems(m_items);
}

Session::FixResult Session::applyFix(const QString& itemId, int fixIndex)
{
    FixResult result;
    Item* item = find(itemId);
    if (!item || fixIndex < 0 || fixIndex >= item->fixes.size()) {
        result.error = QObject::tr("That fix is not available any more.");
        return result;
    }
    const Fix fix = item->fixes.at(fixIndex);
    const Category category = item->category;
    item->error.clear();

    switch (fix.kind) {
        case Fix::Kind::OpenUrl:
            m_hooks.openUrl(fix.targets.value(0));
            result.ok = true;
            return result;
        case Fix::Kind::ShowFile:
            m_hooks.showFile(fix.targets.value(0));
            result.ok = true;
            return result;
        case Fix::Kind::OpenMods:
            result.ok = true;
            result.leavesCheck = true;
            return result;
        case Fix::Kind::Recheck:
            recheck(category);
            result.ok = true;
            return result;
        case Fix::Kind::RecycleFile: {
            const QString path = fix.targets.value(0);
            if (QFileInfo::exists(path) && !m_hooks.recycle(path)) {
                result.error = QObject::tr("Couldn't move %1 to the Recycle Bin.").arg(QFileInfo(path).fileName());
                find(itemId)->error = result.error;
                return result;
            }
            recheck(category);
            result.ok = true;
            return result;
        }
        case Fix::Kind::DisableJars: {
            const Mods::RepairBatchResult batch = Mods::applyAll(m_env.modsDir(), fix.actions);
            recheck(category);
            if (!batch.firstError.isEmpty()) {
                result.error = batch.firstError;
                if (Item* again = find(itemId)) {
                    again->error = result.error;
                }
                return result;
            }
            result.ok = true;
            return result;
        }
        case Fix::Kind::RestorePacks: {
            const QString error = m_env.restorePacks ? m_env.restorePacks() : QObject::tr("The packs can't be restored now.");
            if (!error.isEmpty()) {
                result.error = error;
                item->error = error;
                return result;
            }
            recheck(category);
            result.ok = true;
            return result;
        }
        case Fix::Kind::KeepPacksOff:
            if (m_env.dismissPacks) {
                m_env.dismissPacks();
            }
            item->fixed = true;
            item->fixedText = QObject::tr("Left off");
            result.ok = true;
            return result;
    }
    return result;
}

Decision Session::play(const std::function<bool()>& confirmBlock)
{
    if (hasOpenBlock() && !(confirmBlock && confirmBlock())) {
        return Decision::Cancel;
    }
    if (m_optOut && m_hooks.setSkipInstance) {
        m_hooks.setSkipInstance(true);
    }
    for (const Item& item : m_items) {
        if (item.category == Category::Packs && !item.fixed && m_env.dismissPacks) {
            m_env.dismissPacks();  // "play anyway" with the packs still off = "play without them": not asked again for this marker
        }
    }
    return Decision::Play;
}

}  // namespace PreLaunch
}  // namespace Zero
