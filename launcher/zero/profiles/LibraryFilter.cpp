// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFilter.h"

#include <QCoreApplication>
#include <algorithm>

namespace Zero {
namespace LibraryFilter {

namespace {
int loaderRank(const QString& label)
{
    static const QStringList order = { QStringLiteral("Vanilla"), QStringLiteral("Fabric"), QStringLiteral("Forge"), QStringLiteral("NeoForge"),
                                       QStringLiteral("Quilt") };
    const qsizetype i = order.indexOf(label);
    return i < 0 ? int(order.size()) : int(i);
}
}  // namespace

bool Chip::operator==(const Chip& other) const
{
    if (kind != other.kind)
        return false;
    // tags compare case-insensitively, loaders by their label
    return kind == Kind::Tag ? value.compare(other.value, Qt::CaseInsensitive) == 0 : value == other.value;
}

QString loaderLabel(const QString& loader)
{
    return loader.isEmpty() ? QStringLiteral("Vanilla") : loader;
}

bool textMatches(const QString& text, const Facts& facts)
{
    const QString needle = text.trimmed();
    if (needle.isEmpty())
        return true;
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (facts.name.contains(needle, cs) || facts.group.contains(needle, cs) || facts.version.contains(needle, cs))
        return true;
    return std::any_of(facts.tags.cbegin(), facts.tags.cend(), [&](const QString& tag) { return tag.contains(needle, cs); });
}

bool chipMatches(const Chip& chip, const Facts& facts)
{
    switch (chip.kind) {
        case Kind::All:
            return true;
        case Kind::Favorites:
            return facts.favorite;
        case Kind::Running:
            return facts.running;
        case Kind::Loader:
            return loaderLabel(facts.loader) == chip.value;
        case Kind::Tag:
            return std::any_of(facts.tags.cbegin(), facts.tags.cend(), [&](const QString& t) { return t.compare(chip.value, Qt::CaseInsensitive) == 0; });
    }
    return true;
}

bool matches(const Chip& chip, const QString& text, const Facts& facts)
{
    return chipMatches(chip, facts) && textMatches(text, facts);
}

QList<ChipInfo> buildChips(const QList<Facts>& all)
{
    QList<ChipInfo> out;
    auto count = [&](const Chip& chip) {
        return int(std::count_if(all.cbegin(), all.cend(), [&](const Facts& f) { return chipMatches(chip, f); }));
    };
    for (const auto& [kind, label] : { std::pair{ Kind::All, QCoreApplication::translate("Zero::LibraryFilter", "All") }, std::pair{ Kind::Favorites, QCoreApplication::translate("Zero::LibraryFilter", "Favorites") },
                                       std::pair{ Kind::Running, QCoreApplication::translate("Zero::LibraryFilter", "Running") } }) {
        const Chip chip{ kind, {} };
        out.append({ chip, label, count(chip) });
    }

    QStringList loaders;
    QList<QStringList> tagLists;
    for (const Facts& f : all) {
        const QString label = loaderLabel(f.loader);
        if (!loaders.contains(label))
            loaders << label;
        tagLists << f.tags;
    }
    std::sort(loaders.begin(), loaders.end(), [](const QString& a, const QString& b) {
        const int ra = loaderRank(a), rb = loaderRank(b);
        return ra != rb ? ra < rb : a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const QString& loader : loaders) {
        const Chip chip{ Kind::Loader, loader };
        out.append({ chip, loader, count(chip) });
    }

    // tags: alphabetical, first spelling wins
    QStringList seen;
    QStringList tags;
    for (const QStringList& list : tagLists) {
        for (const QString& tag : list) {
            if (!seen.contains(tag.toLower())) {
                seen << tag.toLower();
                tags << tag;
            }
        }
    }
    std::sort(tags.begin(), tags.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    for (const QString& tag : tags) {
        const Chip chip{ Kind::Tag, tag };
        out.append({ chip, QStringLiteral("#") + tag, count(chip) });
    }
    return out;
}

Chip resolveChip(const Chip& active, const QList<ChipInfo>& chips)
{
    for (const ChipInfo& info : chips) {
        if (info.chip == active)
            return info.chip;
    }
    return Chip{};
}

}  // namespace LibraryFilter
}  // namespace Zero
