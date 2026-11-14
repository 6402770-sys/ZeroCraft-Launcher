// SPDX-License-Identifier: GPL-3.0-only
#include "InstalledPacks.h"

#include <QCollator>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>
#include <optional>

#include "zero/mods/Versions.h"

namespace Zero {
namespace Content {
namespace Packs {

// ---- the description ------------------------------------------------------------------------------------------------------------

QString plainDescription(const QString& description)
{
    QString text = description;
    // the tags Prism's processComponent() writes (and the usual ones around them); a "<" that starts none of them is text
    static const QRegularExpression breaks(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("</?(?:span|u|s|a|b|i|em|strong|font|p|div)\\b[^>]*>"),
                                         QRegularExpression::CaseInsensitiveOption);
    text.replace(breaks, QStringLiteral(" "));
    text.remove(tags);
    // legacy formatting: the section sign and the character after it (colour, bold, reset, ... and the `x` of the hex form)
    static const QRegularExpression codes(QStringLiteral("§."), QRegularExpression::DotMatchesEverythingOption);
    text.remove(codes);
    text.remove(QChar(0x00A7));
    // entities last, so that an escaped "<b>" in the text is not taken for a tag
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text.simplified();
}

// ---- the pack format ------------------------------------------------------------------------------------------------------------

namespace {
/// Is `mc` between the bounds? nullopt when a bound or the version cannot be ordered (snapshots).
std::optional<bool> within(const QString& first, const QString& last, const QString& mc)
{
    const auto low = Mods::compareVersions(first, mc);
    const auto high = Mods::compareVersions(mc, last);
    if (!low || !high) {
        return std::nullopt;
    }
    return *low <= 0 && *high <= 0;
}
}  // namespace

PackFit packFit(int packFormat, const FormatMap& mappings, const QString& mcVersion)
{
    PackFit fit;
    if (packFormat <= 0 || mcVersion.trimmed().isEmpty()) {
        return fit;
    }
    const auto it = mappings.constFind({ packFormat, 0 });
    if (it == mappings.constEnd()) {
        return fit;
    }
    const QString first = it.value().first.toString();
    const QString last = it.value().second.toString();
    const auto inside = within(first, last, mcVersion.trimmed());
    if (!inside) {
        return fit;
    }
    fit.kind = *inside ? FitKind::Fits : FitKind::MadeFor;
    if (!*inside) {
        fit.madeFor = first;
    }
    return fit;
}

QString madeForText(const QString& version)
{
    return QObject::tr("Made for %1").arg(version);
}

// ---- rows -----------------------------------------------------------------------------------------------------------------------

QString packNameOf(const QString& fileName, bool folder)
{
    QString name = fileName;
    if (folder) {
        return name;
    }
    if (name.endsWith(QLatin1String(".disabled"), Qt::CaseInsensitive)) {
        name.chop(9);
    }
    if (name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        name.chop(4);
    }
    return name;
}

QString entryNameOf(const PackRow& row)
{
    return row.hidden ? QString() : row.fileName;
}

void overlayActive(QList<PackRow>& rows, const QStringList& activeTopFirst)
{
    for (PackRow& row : rows) {
        row.active = false;
        row.activeIndex = -1;
        if (row.hidden) {
            continue;
        }
        const int index = int(activeTopFirst.indexOf(row.fileName));
        if (index >= 0) {
            row.active = true;
        }
    }
    // the position among the packs that are there: stale entries (files that are gone) take no place
    int next = 0;
    for (const QString& name : activeTopFirst) {
        for (PackRow& row : rows) {
            if (row.active && row.fileName == name && row.activeIndex < 0) {
                row.activeIndex = next++;
                break;
            }
        }
    }
}

QStringList activatableNames(const QList<PackRow>& rows)
{
    QStringList out;
    for (const PackRow& row : rows) {
        if (!row.hidden) {
            out.append(row.fileName);
        }
    }
    return out;
}

QString filterId(Filter filter)
{
    switch (filter) {
        case Filter::All:
            return QStringLiteral("all");
        case Filter::On:
            return QStringLiteral("on");
        case Filter::Off:
            return QStringLiteral("off");
    }
    return QStringLiteral("all");
}

Filter filterFromId(const QString& id)
{
    for (const Filter filter : allFilters()) {
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
        case Filter::On:
            return QObject::tr("On");
        case Filter::Off:
            return QObject::tr("Off");
    }
    return {};
}

QList<Filter> allFilters()
{
    return { Filter::All, Filter::On, Filter::Off };
}

bool matchesFilter(const PackRow& row, Filter filter)
{
    switch (filter) {
        case Filter::All:
            return true;
        case Filter::On:
            return row.active;
        case Filter::Off:
            return !row.active;
    }
    return true;
}

bool matchesSearch(const PackRow& row, const QString& query)
{
    const QStringList words = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& word : words) {
        if (!row.name.contains(word, Qt::CaseInsensitive) && !row.fileName.contains(word, Qt::CaseInsensitive) &&
            !row.description.contains(word, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

QString onHeaderText()
{
    return QObject::tr("On — top wins");
}

QString availableHeaderText()
{
    return QObject::tr("Available");
}

QString defaultRowName()
{
    return QObject::tr("Default resources");
}

QString defaultRowNote()
{
    return QObject::tr("The game's own textures, always the last in line");
}

namespace {
bool lessByName(const PackRow& a, const PackRow& b)
{
    static const QCollator collator = []() {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    const int byName = collator.compare(a.name, b.name);
    return byName != 0 ? byName < 0 : collator.compare(a.fileName, b.fileName) < 0;
}
}  // namespace

QList<ListEntry> layoutList(const QList<PackRow>& rows, Filter filter, const QString& query, bool ordered)
{
    QList<int> shown;
    for (int i = 0; i < rows.size(); ++i) {
        if (matchesFilter(rows.at(i), filter) && matchesSearch(rows.at(i), query)) {
            shown.append(i);
        }
    }
    const auto byName = [&rows](int a, int b) { return lessByName(rows.at(a), rows.at(b)); };

    QList<ListEntry> out;
    if (!ordered) {
        std::sort(shown.begin(), shown.end(), byName);
        for (const int i : shown) {
            ListEntry entry;
            entry.row = i;
            out.append(entry);
        }
        return out;
    }

    QList<int> on;
    QList<int> available;
    for (const int i : shown) {
        (rows.at(i).active ? on : available).append(i);
    }
    std::sort(on.begin(), on.end(), [&rows](int a, int b) { return rows.at(a).activeIndex < rows.at(b).activeIndex; });
    std::sort(available.begin(), available.end(), [&rows, &byName](int a, int b) {
        if (rows.at(a).hidden != rows.at(b).hidden) {
            return !rows.at(a).hidden;  // the hidden files are last
        }
        return byName(a, b);
    });

    // "Default resources" takes part in a search like a pack; it is not an Off pack.
    const bool defaultShown = filter != Filter::Off && (query.simplified().isEmpty() || defaultRowName().contains(query.simplified(), Qt::CaseInsensitive));
    if (!on.isEmpty() || defaultShown) {
        ListEntry header;
        header.kind = ListEntry::Kind::Header;
        header.text = onHeaderText();
        out.append(header);
        for (int n = 0; n < on.size(); ++n) {
            ListEntry entry;
            entry.row = on.at(n);
            entry.onIndex = rows.at(on.at(n)).activeIndex;
            out.append(entry);
        }
        if (defaultShown) {
            ListEntry entry;
            entry.kind = ListEntry::Kind::Default;
            entry.text = defaultRowName();
            out.append(entry);
        }
    }
    if (!available.isEmpty()) {
        ListEntry header;
        header.kind = ListEntry::Kind::Header;
        header.text = availableHeaderText();
        out.append(header);
        for (const int i : available) {
            ListEntry entry;
            entry.row = i;
            out.append(entry);
        }
    }
    return out;
}

Counts countOf(const QList<PackRow>& rows)
{
    Counts counts;
    counts.total = int(rows.size());
    for (const PackRow& row : rows) {
        counts.on += row.active ? 1 : 0;
        counts.hidden += row.hidden ? 1 : 0;
    }
    return counts;
}

QString captionText(const Counts& counts, int shown, bool narrowed, bool ordered)
{
    const QString noun = counts.total == 1 ? QObject::tr("pack") : QObject::tr("packs");
    if (narrowed) {
        return QObject::tr("%1 of %2 %3").arg(shown).arg(counts.total).arg(noun);
    }
    if (!ordered) {
        return QObject::tr("%1 %2").arg(counts.total).arg(noun);
    }
    return QObject::tr("%1 %2 · %3 on").arg(counts.total).arg(noun).arg(counts.on);
}

QString selectionText(int selected)
{
    return QObject::tr("%1 selected").arg(selected);
}

QList<Mods::Installed::PillSpec> pillsFor(const PackRow& row)
{
    using Mods::Installed::PillSpec;
    using Mods::Installed::PillTone;
    QList<PillSpec> pills;
    PillSpec source;
    source.text = row.source == Source::Modrinth ? QStringLiteral("Modrinth")
                  : row.source == Source::CurseForge ? QStringLiteral("CurseForge")
                                                     : QObject::tr("Local file");
    source.priority = 2;
    pills.append(source);
    if (row.folder) {
        PillSpec folder;
        folder.text = QObject::tr("Folder");
        folder.priority = 3;
        pills.append(folder);
    }
    if (row.hidden) {
        PillSpec hidden;
        hidden.text = QObject::tr("Hidden from the game");
        hidden.tone = PillTone::Warning;
        hidden.priority = Mods::Installed::kKeepPriority;
        pills.append(hidden);
    }
    if (row.fit.kind == FitKind::MadeFor) {
        PillSpec made;
        made.text = madeForText(row.fit.madeFor);
        made.tone = PillTone::Warning;
        made.priority = Mods::Installed::kKeepPriority;
        pills.append(made);
    }
    return pills;
}

QString rowToolTip(const PackRow& row)
{
    QStringList lines;
    lines.append(row.fileName);
    if (!row.projectName.isEmpty() && row.projectName != row.name) {
        lines.append(row.projectName);
    }
    if (row.hidden) {
        lines.append(QObject::tr("The game does not see this file. Unhide it to use the pack."));
    }
    if (row.fit.kind == FitKind::MadeFor) {
        lines.append(madeForText(row.fit.madeFor));
    }
    return lines.join(QLatin1Char('\n'));
}

QStringList moved(const QStringList& names, int from, int to)
{
    if (from < 0 || from >= names.size() || to < 0 || to >= names.size() || from == to) {
        return names;
    }
    QStringList out = names;
    out.move(from, to);
    return out;
}

QString turnedOnText(const QString& name)
{
    return QObject::tr("Turned on %1").arg(name);
}

QString turnedOffText(const QString& name)
{
    return QObject::tr("Turned off %1").arg(name);
}

QString movedToTopText(const QString& name)
{
    return QObject::tr("Moved %1 to the top").arg(name);
}

QString movedUpText(const QString& name)
{
    return QObject::tr("Moved %1 up").arg(name);
}

QString movedDownText(const QString& name)
{
    return QObject::tr("Moved %1 down").arg(name);
}

QString movedToBottomText(const QString& name)
{
    return QObject::tr("Moved %1 to the bottom").arg(name);
}

QString movedText(const QString& name)
{
    return QObject::tr("Moved %1").arg(name);
}

QString restoredText()
{
    return QObject::tr("Restored the previous order");
}

QString addedAndOnText(const QString& pack, const QString& instance)
{
    return QObject::tr("Added %1 to %2 and turned it on").arg(pack, instance);
}

}  // namespace Packs
}  // namespace Content
}  // namespace Zero
