// SPDX-License-Identifier: GPL-3.0-only
#include "InstalledModsModel.h"

#include <QCollator>
#include <QObject>

#include <algorithm>

#include "minecraft/mod/Mod.h"

namespace Zero {
namespace Mods {
namespace Installed {

QString filterId(Filter filter)
{
    switch (filter) {
        case Filter::All:
            return QStringLiteral("all");
        case Filter::Enabled:
            return QStringLiteral("enabled");
        case Filter::Disabled:
            return QStringLiteral("disabled");
        case Filter::Issues:
            return QStringLiteral("issues");
        case Filter::Updates:
            return QStringLiteral("updates");
    }
    return QStringLiteral("all");
}

Filter filterFromId(const QString& id)
{
    for (const Filter filter : filtersFor(true)) {
        if (filterId(filter) == id) {
            return filter;
        }
    }
    return Filter::All;
}

QString filterLabel(Filter filter)
{
    switch (filter) {
        case Filter::All:
            return QObject::tr("All");
        case Filter::Enabled:
            return QObject::tr("Enabled");
        case Filter::Disabled:
            return QObject::tr("Disabled");
        case Filter::Issues:
            return QObject::tr("Issues");
        case Filter::Updates:
            return QObject::tr("Updates");
    }
    return {};
}

QList<Filter> allFilters()
{
    return { Filter::All, Filter::Enabled, Filter::Disabled, Filter::Issues };
}

QList<Filter> filtersFor(bool withUpdates)
{
    QList<Filter> filters = allFilters();
    if (withUpdates) {
        filters.append(Filter::Updates);
    }
    return filters;
}

QString sourceLabel(Source source)
{
    switch (source) {
        case Source::Modrinth:
            return QStringLiteral("Modrinth");
        case Source::CurseForge:
            return QStringLiteral("CurseForge");
        case Source::Local:
            break;
    }
    return QObject::tr("Local file");
}

QString updateKeyOf(const QString& id)
{
    QString key = id;
    if (key.endsWith(QLatin1String(".disabled"))) {
        key.chop(9);
    }
    return key;
}

Row rowFromMod(const Mod& mod)
{
    Row row;
    row.id = mod.internalId();
    row.updateKey = updateKeyOf(row.id);
    row.fileStamp = QStringLiteral("%1/%2").arg(mod.fileinfo().size()).arg(mod.fileinfo().lastModified().toMSecsSinceEpoch());
    row.fileName = mod.fileinfo().fileName();
    row.filePath = mod.fileinfo().absoluteFilePath();
    row.name = mod.name();
    if (row.name.trimmed().isEmpty()) {
        QString fallback = row.fileName;
        if (fallback.endsWith(QLatin1String(".disabled"))) {
            fallback.chop(9);
        }
        row.name = fallback;
    }
    row.version = mod.version();
    row.modId = mod.modId();
    row.enabled = mod.enabled();
    row.issues = mod.issues();
    row.requiredBy = mod.requiredByCount();
    row.homepage = mod.homepage();
    row.hasIcon = !mod.iconPath().isEmpty();
    if (const auto metadata = mod.metadata()) {
        row.hasMetadata = true;
        row.source = metadata->provider == ModPlatform::ResourceProvider::FLAME ? Source::CurseForge : Source::Modrinth;
        row.metadataHash = metadata->hash;
        row.metadataVersion = metadata->versionNumber;
        row.projectId = metadata->projectId.toString();
        row.updateLocked = metadata->lockUpdate;
        // CurseForge ids are numbers; anything else cannot be asked for.
        const bool numeric =
            !row.projectId.isEmpty() && std::all_of(row.projectId.begin(), row.projectId.end(), [](QChar c) { return c.isDigit(); });
        const bool idUsable = row.source == Source::CurseForge ? numeric && row.projectId.toLongLong() > 0 : !row.projectId.isEmpty();
        row.checkable = idUsable && !row.updateLocked;
        // Universal is also what a metadata file without a side defaults to, so only a one-sided mod says something.
        switch (metadata->side.value()) {
            case ModPlatform::SideTypeValue::ClientSide:
                row.environment = QObject::tr("Client");
                break;
            case ModPlatform::SideTypeValue::ServerSide:
                row.environment = QObject::tr("Server");
                break;
            default:
                break;
        }
    }
    return row;
}

bool matchesFilter(const Row& row, Filter filter)
{
    switch (filter) {
        case Filter::All:
            return true;
        case Filter::Enabled:
            return row.enabled;
        case Filter::Disabled:
            return !row.enabled;
        case Filter::Issues:
            return !row.issues.isEmpty() || row.hasDepProblems();
        case Filter::Updates:
            return row.hasUpdate();
    }
    return true;
}

bool matchesSearch(const Row& row, const QString& query)
{
    const QStringList words = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& word : words) {
        if (!row.name.contains(word, Qt::CaseInsensitive) && !row.modId.contains(word, Qt::CaseInsensitive) &&
            !row.fileName.contains(word, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

QList<int> visibleRows(const QList<Row>& rows, Filter filter, const QString& query)
{
    QList<int> out;
    for (int i = 0; i < rows.size(); ++i) {
        if (matchesFilter(rows.at(i), filter) && matchesSearch(rows.at(i), query)) {
            out.append(i);
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(out.begin(), out.end(), [&rows, &collator](int a, int b) {
        const int byName = collator.compare(rows.at(a).name, rows.at(b).name);
        if (byName != 0) {
            return byName < 0;
        }
        return collator.compare(rows.at(a).fileName, rows.at(b).fileName) < 0;
    });
    return out;
}

Counts countOf(const QList<Row>& rows)
{
    Counts counts;
    counts.total = int(rows.size());
    for (const Row& row : rows) {
        (row.enabled ? counts.enabled : counts.disabled) += 1;
        if (!row.issues.isEmpty() || row.hasDepProblems()) {
            counts.issues += 1;
        }
        if (row.hasDepProblems()) {
            counts.attention += 1;
        }
        if (row.hasUpdate()) {
            counts.updates += 1;
        }
    }
    return counts;
}

namespace {
QString modsCount(int count)
{
    return count == 1 ? QObject::tr("1 mod") : QObject::tr("%1 mods").arg(count);
}
}  // namespace

QString captionText(const Counts& counts, int shown, bool narrowed)
{
    if (narrowed) {
        return counts.total == 1 ? QObject::tr("%1 of 1 mod").arg(shown) : QObject::tr("%1 of %2 mods").arg(shown).arg(counts.total);
    }
    QString text = modsCount(counts.total);
    if (counts.disabled > 0) {
        text += QObject::tr(" · %1 disabled").arg(counts.disabled);
    }
    if (counts.attention > 0) {
        text += QStringLiteral(" · ") + attentionText(counts.attention);
    }
    if (counts.updates == 1) {
        text += QObject::tr(" · 1 update");
    } else if (counts.updates > 1) {
        text += QObject::tr(" · %1 updates").arg(counts.updates);
    }
    return text;
}

QString selectionText(int selected)
{
    return QObject::tr("%1 selected").arg(selected);
}

QString notForText(const QString& mcVersion)
{
    return mcVersion.isEmpty() ? QObject::tr("Not for this version") : QObject::tr("Not for %1").arg(mcVersion);
}

QList<PillSpec> pillsFor(const Row& row, const QString& mcVersion)
{
    QList<PillSpec> pills;
    for (const DepProblem& problem : row.depProblems) {
        // danger for what is missing or in conflict, warning for what can be fixed by enabling / updating; they outrank everything else
        const bool danger = problem.kind == DepKind::Missing || problem.kind == DepKind::Conflict;
        pills.append({ depPillText(problem), danger ? PillTone::Danger : PillTone::Warning, danger ? 9 : 8, kDepPillMaxWidth });
    }
    if (!row.enabled) {
        pills.append({ QObject::tr("Disabled"), PillTone::Plain, 4 });
    }
    if (!row.issues.isEmpty()) {
        pills.append({ notForText(mcVersion), PillTone::Warning, 6 });
    }
    if (row.requiredBy > 0) {
        pills.append({ QObject::tr("Required by %1").arg(row.requiredBy), PillTone::Plain, 5 });
    }
    if (!row.environment.isEmpty()) {
        pills.append({ row.environment, PillTone::Plain, 1 });
    }
    pills.append({ sourceLabel(row.source), PillTone::Plain, 3 });
    if (row.hasUpdate()) {
        pills.append({ updatePillText(row.updateVersion), PillTone::Accent, 10, kUpdatePillMaxWidth });
    }
    return pills;
}

QString rowToolTip(const Row& row)
{
    QStringList lines;
    lines << row.fileName;
    if (row.hasUpdate()) {
        const QString type = versionTypeLabel(row.updateType);
        lines << (type.isEmpty() ? updateToText(row.updateVersion) : QObject::tr("%1 (%2)").arg(updateToText(row.updateVersion), type));
    }
    for (const DepProblem& problem : row.depProblems) {
        lines << depFullText(problem);
    }
    for (const QString& issue : row.issues) {
        lines << issue;
    }
    if (!row.checkError.isEmpty()) {
        lines << checkFailedText(row.checkError).replace(QLatin1Char('\n'), QLatin1Char(' '));
    }
    return lines.join(QLatin1Char('\n'));
}

// ---- the Dependency Inspector ----------------------------------------------------------------------------------------------------

void overlayDependencies(QList<Row>& rows, const Inspection& inspection)
{
    for (Row& row : rows) {
        row.depProblems.clear();
        if (!row.enabled) {
            continue;  // a disabled mod's needs do not matter until it is enabled
        }
        const ModInspection* mod = inspection.find(row.filePath);
        if (!mod || (!mod->stamp.isEmpty() && mod->stamp != row.fileStamp)) {
            continue;
        }
        const Problems problems = problemsOf(*mod);
        const std::pair<DepKind, const QStringList*> groups[] = { { DepKind::Missing, &problems.missing },
                                                                  { DepKind::Disabled, &problems.disabled },
                                                                  { DepKind::WrongVersion, &problems.wrongVersion },
                                                                  { DepKind::Conflict, &problems.conflicts } };
        for (const auto& group : groups) {
            if (!group.second->isEmpty()) {
                row.depProblems.append({ group.first, *group.second });
            }
        }
    }
}

QString depPillText(const DepProblem& problem)
{
    const QString first = problem.names.value(0);
    const int more = int(problem.names.size()) - 1;
    switch (problem.kind) {
        case DepKind::Missing:
            return more > 0 ? QObject::tr("Needs %1 +%2").arg(first).arg(more) : QObject::tr("Needs %1").arg(first);
        case DepKind::Disabled:
            return more > 0 ? QObject::tr("Needs %1 · disabled +%2").arg(first).arg(more) : QObject::tr("Needs %1 · disabled").arg(first);
        case DepKind::WrongVersion:
            return more > 0 ? QObject::tr("Wrong %1 version +%2").arg(first).arg(more) : QObject::tr("Wrong %1 version").arg(first);
        case DepKind::Conflict:
            return more > 0 ? QObject::tr("Conflicts with %1 +%2").arg(first).arg(more) : QObject::tr("Conflicts with %1").arg(first);
    }
    return {};
}

QString depFullText(const DepProblem& problem)
{
    const QString names = problem.names.join(QStringLiteral(", "));
    switch (problem.kind) {
        case DepKind::Missing:
            return QObject::tr("Needs %1").arg(names);
        case DepKind::Disabled:
            return QObject::tr("Needs %1 · disabled").arg(names);
        case DepKind::WrongVersion:
            return QObject::tr("Wrong version of %1").arg(names);
        case DepKind::Conflict:
            return QObject::tr("Conflicts with %1").arg(names);
    }
    return {};
}

QString attentionText(int count)
{
    return count == 1 ? QObject::tr("1 needs attention") : QObject::tr("%1 need attention").arg(count);
}

// ---- the update check ------------------------------------------------------------------------------------------------------------

int updateCount(const UpdateResults& results)
{
    return int(std::count_if(results.entries.begin(), results.entries.end(), [](const CheckEntry& e) { return e.hasUpdate(); }));
}

int failureCount(const UpdateResults& results)
{
    return int(std::count_if(results.entries.begin(), results.entries.end(),
                             [](const CheckEntry& e) { return !e.hasUpdate() && !e.error.isEmpty(); }));
}

namespace {
/// The row an entry still fits: same mod (key), same packwiz hash, same file.
const Row* rowFor(const CheckEntry& entry, const QList<Row>& rows)
{
    for (const Row& row : rows) {
        if (row.updateKey == entry.key) {
            return row.metadataHash == entry.metadataHash && row.fileStamp == entry.fileStamp ? &row : nullptr;
        }
    }
    return nullptr;
}
}  // namespace

bool pruneResults(UpdateResults& results, const QList<Row>& rows)
{
    const qsizetype before = results.entries.size();
    results.entries.erase(std::remove_if(results.entries.begin(), results.entries.end(),
                                         [&rows](const CheckEntry& entry) { return rowFor(entry, rows) == nullptr; }),
                          results.entries.end());
    return results.entries.size() != before;
}

void overlayResults(QList<Row>& rows, const UpdateResults& results)
{
    for (Row& row : rows) {
        row.updateVersion.clear();
        row.updateType.clear();
        row.checkError.clear();
    }
    for (const CheckEntry& entry : results.entries) {
        for (Row& row : rows) {
            if (row.updateKey != entry.key) {
                continue;
            }
            if (row.metadataHash == entry.metadataHash && row.fileStamp == entry.fileStamp) {
                row.updateVersion = entry.newVersion;
                row.updateType = entry.versionType;
                row.checkError = entry.hasUpdate() ? QString() : entry.error;
            }
            break;
        }
    }
}

void dropEntries(UpdateResults& results, const QStringList& keys)
{
    results.entries.erase(std::remove_if(results.entries.begin(), results.entries.end(),
                                         [&keys](const CheckEntry& entry) { return keys.contains(entry.key); }),
                          results.entries.end());
}

bool canCheck(const Row& row, bool curseForgeAvailable)
{
    return row.checkable && (row.source != Source::CurseForge || curseForgeAvailable);
}

int unverifiableCount(const QList<Row>& rows, bool curseForgeAvailable)
{
    return int(std::count_if(rows.begin(), rows.end(),
                             [curseForgeAvailable](const Row& row) { return !row.updateLocked && !canCheck(row, curseForgeAvailable); }));
}

int matchUpdate(const QList<UpdateCandidate>& candidates, const QList<bool>& taken, const UpdateFacts& facts)
{
    int firstFree = -1;
    int sameVersion = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        const UpdateCandidate& c = candidates.at(i);
        if (c.source != facts.source || c.projectId != facts.projectId || (i < taken.size() && taken.at(i))) {
            continue;
        }
        if (!facts.oldHash.isEmpty() && c.hash == facts.oldHash) {
            return i;
        }
        if (sameVersion < 0 && !facts.oldVersion.isEmpty() && c.version == facts.oldVersion) {
            sameVersion = i;
        }
        if (firstFree < 0) {
            firstFree = i;
        }
    }
    return sameVersion >= 0 ? sameVersion : firstFree;
}

QString checkButtonText()
{
    return QObject::tr("Check for updates");
}

QString checkingButtonText()
{
    return QObject::tr("Checking\u2026");
}

QString checkingCaption(int count)
{
    return count == 1 ? QObject::tr("Checking 1 mod\u2026") : QObject::tr("Checking %1 mods\u2026").arg(count);
}

QString updateAllText(int count)
{
    return count == 1 ? QObject::tr("Update 1 mod") : QObject::tr("Update all %1").arg(count);
}

QString updatePillText(const QString& version)
{
    return QObject::tr("Update \u2192 %1").arg(version);
}

QString updateToText(const QString& version)
{
    return QObject::tr("Update to %1").arg(version);
}

QString updateSelectedText(int count)
{
    return count == 1 ? QObject::tr("Update 1 mod") : QObject::tr("Update %1 mods").arg(count);
}

QString checkAgainText()
{
    return QObject::tr("Check for updates again");
}

QString unverifiableText(int count)
{
    if (count <= 0) {
        return {};
    }
    return count == 1 ? QObject::tr("1 mod has no provider info and can't be checked.")
                      : QObject::tr("%1 mods have no provider info and can't be checked.").arg(count);
}

QString upToDateText()
{
    return QObject::tr("All mods are up to date");
}

QString nothingToCheckText()
{
    return QObject::tr("None of these mods can be checked for updates");
}

QString checkFailedText(const QString& reason)
{
    return QObject::tr("Couldn't check for updates: %1").arg(reason);
}

QString updatedText(const QStringList& names, const QString& newVersion)
{
    if (names.size() == 1) {
        return newVersion.isEmpty() ? QObject::tr("Updated %1").arg(names.first())
                                    : QObject::tr("Updated %1 to %2").arg(names.first(), newVersion);
    }
    return QObject::tr("Updated %1 mods").arg(names.size());
}

QString versionTypeLabel(const QString& type)
{
    if (type == QLatin1String("Release")) {
        return QObject::tr("Release");
    }
    if (type == QLatin1String("Beta")) {
        return QObject::tr("Beta");
    }
    if (type == QLatin1String("Alpha")) {
        return QObject::tr("Alpha");
    }
    return {};
}

bool everyCheckFailed(int checked, const UpdateResults& results)
{
    return checked > 0 && updateCount(results) == 0 && failureCount(results) >= checked;
}

// ---- enable / disable ------------------------------------------------------------------------------------------------------------

ToggleAction nextToggle(const QList<bool>& enabledFlags)
{
    if (enabledFlags.isEmpty()) {
        return ToggleAction::Enable;
    }
    return std::all_of(enabledFlags.begin(), enabledFlags.end(), [](bool on) { return on; }) ? ToggleAction::Disable : ToggleAction::Enable;
}

QString joinNames(const QStringList& names)
{
    if (names.isEmpty()) {
        return {};
    }
    if (names.size() == 1) {
        return names.first();
    }
    if (names.size() <= 3) {
        return QObject::tr("%1 and %2").arg(names.mid(0, names.size() - 1).join(QStringLiteral(", ")), names.last());
    }
    return QObject::tr("%1 and %2 more").arg(names.mid(0, 3).join(QStringLiteral(", "))).arg(names.size() - 3);
}

namespace {
QString shortName(const QString& name)
{
    constexpr int kMax = 28;
    return name.size() <= kMax ? name : name.left(kMax - 1) + QChar(0x2026);
}
}  // namespace

TogglePrompt togglePrompt(ToggleAction action, const QStringList& selectedNames, const QStringList& affectedNames)
{
    TogglePrompt prompt;
    if (affectedNames.isEmpty() || selectedNames.isEmpty()) {
        return prompt;
    }
    prompt.needed = true;
    prompt.allCount = int(selectedNames.size() + affectedNames.size());
    const bool oneSelected = selectedNames.size() == 1;
    const bool oneAffected = affectedNames.size() == 1;
    const QString selected = oneSelected ? selectedNames.first() : QObject::tr("%1 mods").arg(selectedNames.size());
    if (action == ToggleAction::Disable) {
        prompt.title = oneAffected ? QObject::tr("Disable %1 too?").arg(affectedNames.first())
                                   : QObject::tr("Disable %1 other mods too?").arg(affectedNames.size());
        prompt.body = oneAffected
                          ? (oneSelected ? QObject::tr("Disabling %1 affects %2, which requires it.")
                                         : QObject::tr("Disabling %1 affects %2, which requires them."))
                                .arg(selected, joinNames(affectedNames))
                          : (oneSelected ? QObject::tr("Disabling %1 affects %2, which require it.")
                                         : QObject::tr("Disabling %1 affects %2, which require them."))
                                .arg(selected, joinNames(affectedNames));
        prompt.onlyLabel = oneSelected ? QObject::tr("Disable only %1").arg(shortName(selectedNames.first()))
                                       : QObject::tr("Disable only the %1 selected").arg(selectedNames.size());
        prompt.allLabel = QObject::tr("Disable all %1").arg(prompt.allCount);
    } else {
        prompt.title = oneAffected ? QObject::tr("Enable %1 too?").arg(affectedNames.first())
                                   : QObject::tr("Enable %1 required mods too?").arg(affectedNames.size());
        const QString subject = oneSelected ? selected : QObject::tr("The %1 selected mods").arg(selectedNames.size());
        prompt.body = (oneSelected ? (oneAffected ? QObject::tr("%1 requires %2, which is disabled.")
                                                  : QObject::tr("%1 requires %2, which are disabled."))
                                   : (oneAffected ? QObject::tr("%1 require %2, which is disabled.")
                                                  : QObject::tr("%1 require %2, which are disabled.")))
                          .arg(subject, joinNames(affectedNames));
        prompt.onlyLabel = oneSelected ? QObject::tr("Enable only %1").arg(shortName(selectedNames.first()))
                                       : QObject::tr("Enable only the %1 selected").arg(selectedNames.size());
        prompt.allLabel = QObject::tr("Enable all %1").arg(prompt.allCount);
    }
    return prompt;
}

QString toggleFailureText(ToggleAction action, const QStringList& failedNames)
{
    const bool enable = action == ToggleAction::Enable;
    if (failedNames.size() == 1) {
        return (enable ? QObject::tr("Couldn't enable %1") : QObject::tr("Couldn't disable %1")).arg(failedNames.first());
    }
    return (enable ? QObject::tr("Couldn't enable %1 mods") : QObject::tr("Couldn't disable %1 mods")).arg(failedNames.size());
}

// ---- remove ----------------------------------------------------------------------------------------------------------------------

RemoveConfirm removeConfirm(const QStringList& names, Noun noun)
{
    RemoveConfirm confirm;
    confirm.title = names.size() == 1 ? QObject::tr("Remove %1?").arg(names.first())
                    : noun == Noun::Packs     ? QObject::tr("Remove %1 packs?").arg(names.size())
                    : noun == Noun::Shaders ? QObject::tr("Remove %1 shader packs?").arg(names.size())
                                            : QObject::tr("Remove %1 mods?").arg(names.size());
    confirm.listed = names.mid(0, kRemoveListMax);
    if (names.size() > kRemoveListMax) {
        confirm.more = QObject::tr("and %1 more").arg(names.size() - kRemoveListMax);
    }
    confirm.note = names.size() == 1 ? QObject::tr("It goes to the Recycle Bin.") : QObject::tr("They go to the Recycle Bin.");
    confirm.confirmLabel = QObject::tr("Remove");
    return confirm;
}

RemoveResult removeResult(const QStringList& removedNames, const QStringList& failedNames, Noun noun)
{
    RemoveResult result;
    QString removed;
    if (removedNames.size() == 1) {
        removed = QObject::tr("Removed %1").arg(removedNames.first());
    } else if (!removedNames.isEmpty()) {
        removed = (noun == Noun::Packs     ? QObject::tr("Removed %1 packs")
                   : noun == Noun::Shaders ? QObject::tr("Removed %1 shader packs")
                                           : QObject::tr("Removed %1 mods"))
                      .arg(removedNames.size());
    }
    QString failed;
    if (failedNames.size() == 1) {
        failed = QObject::tr("Couldn't move %1 to the Recycle Bin").arg(failedNames.first());
    } else if (!failedNames.isEmpty()) {
        failed = (noun == Noun::Packs     ? QObject::tr("Couldn't move %1 packs to the Recycle Bin")
                  : noun == Noun::Shaders ? QObject::tr("Couldn't move %1 shader packs to the Recycle Bin")
                                          : QObject::tr("Couldn't move %1 mods to the Recycle Bin"))
                     .arg(failedNames.size());
    }
    if (failed.isEmpty()) {
        result.text = removed;
        result.outcome = Outcome::Success;
    } else if (removed.isEmpty()) {
        result.text = failed;
        result.outcome = Outcome::Error;
    } else {
        result.text = removed + QStringLiteral(". ") + failed;
        result.outcome = Outcome::Warning;
    }
    return result;
}

}  // namespace Installed
}  // namespace Mods
}  // namespace Zero
