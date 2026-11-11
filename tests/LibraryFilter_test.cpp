// SPDX-License-Identifier: GPL-3.0-only
// LibraryFilter: each chip kind, the text search, their combination, chip building and the fallback of a vanished chip.
#include <QTest>

#include "zero/profiles/LibraryFilter.h"

using namespace Zero::LibraryFilter;

namespace {

Facts inst(const QString& name, const QString& loader, const QStringList& tags = {}, bool favorite = false, bool running = false,
           const QString& version = QStringLiteral("1.20.1"), const QString& group = QString())
{
    Facts f;
    f.name = name;
    f.loader = loader;
    f.tags = tags;
    f.favorite = favorite;
    f.running = running;
    f.version = version;
    f.group = group;
    return f;
}

QStringList names(const QList<Facts>& all, const Chip& chip, const QString& text = QString())
{
    QStringList out;
    for (const Facts& f : all) {
        if (matches(chip, text, f))
            out << f.name;
    }
    return out;
}

const QList<Facts> kLibrary = {
    inst("Survival", "Fabric", { "pvp", "Friends" }, true, false, "1.20.1", "Worlds"),
    inst("Creative", "", { "build" }, false, true, "1.19.4"),
    inst("Tech Pack", "Forge", { "tech", "pvp" }, true, true, "1.18.2", "Packs"),
    inst("Newest", "NeoForge", {}, false, false, "1.21.1"),
    inst("Shaders", "Quilt", { "Friends" }, false, false, "1.20.4"),
};

}  // namespace

class LibraryFilterTest : public QObject {
    Q_OBJECT
   private slots:
    void allMatchesEverything() { QCOMPARE(names(kLibrary, Chip{}).size(), 5); }

    void favoritesChip()
    {
        QCOMPARE(names(kLibrary, { Kind::Favorites, {} }), (QStringList{ "Survival", "Tech Pack" }));
    }

    void runningChip()
    {
        QCOMPARE(names(kLibrary, { Kind::Running, {} }), (QStringList{ "Creative", "Tech Pack" }));
    }

    void loaderChips()
    {
        QCOMPARE(names(kLibrary, { Kind::Loader, "Vanilla" }), (QStringList{ "Creative" }));  // empty loader = vanilla
        QCOMPARE(names(kLibrary, { Kind::Loader, "Fabric" }), (QStringList{ "Survival" }));
        QCOMPARE(names(kLibrary, { Kind::Loader, "NeoForge" }), (QStringList{ "Newest" }));
        QCOMPARE(names(kLibrary, { Kind::Loader, "Forge" }), (QStringList{ "Tech Pack" }));  // not NeoForge
    }

    void tagChipsAreCaseInsensitive()
    {
        QCOMPARE(names(kLibrary, { Kind::Tag, "pvp" }), (QStringList{ "Survival", "Tech Pack" }));
        QCOMPARE(names(kLibrary, { Kind::Tag, "PVP" }), (QStringList{ "Survival", "Tech Pack" }));
        QCOMPARE(names(kLibrary, { Kind::Tag, "friends" }), (QStringList{ "Survival", "Shaders" }));
        QCOMPARE(names(kLibrary, { Kind::Tag, "nothing" }), QStringList());
    }

    void textSearchCoversNameGroupVersionAndTags()
    {
        const Chip all;
        QCOMPARE(names(kLibrary, all, "tech"), (QStringList{ "Tech Pack" }));   // name and tag
        QCOMPARE(names(kLibrary, all, "WORLDS"), (QStringList{ "Survival" }));  // group, case-insensitive
        QCOMPARE(names(kLibrary, all, "1.19"), (QStringList{ "Creative" }));    // version
        QCOMPARE(names(kLibrary, all, "frie"), (QStringList{ "Survival", "Shaders" }));  // tag substring
        QCOMPARE(names(kLibrary, all, "   ").size(), 5);                       // blank = no search
        QCOMPARE(names(kLibrary, all, "  creative ").size(), 1);               // trimmed
    }

    void chipAndTextCombineWithAnd()
    {
        QCOMPARE(names(kLibrary, { Kind::Favorites, {} }, "tech"), (QStringList{ "Tech Pack" }));
        QCOMPARE(names(kLibrary, { Kind::Favorites, {} }, "creative"), QStringList());
        QCOMPARE(names(kLibrary, { Kind::Tag, "pvp" }, "1.18"), (QStringList{ "Tech Pack" }));
        QCOMPARE(names(kLibrary, { Kind::Running, {} }, "1.19"), (QStringList{ "Creative" }));
        QCOMPARE(names(kLibrary, { Kind::Loader, "Fabric" }, "zzz"), QStringList());
    }

    void chipsAreBuiltFromTheLibrary()
    {
        const auto chips = buildChips(kLibrary);
        QStringList labels;
        for (const auto& c : chips)
            labels << c.label;
        QCOMPARE(labels, (QStringList{ "All", "Favorites", "Running", "Vanilla", "Fabric", "Forge", "NeoForge", "Quilt", "#build", "#Friends", "#pvp",
                                       "#tech" }));
        QCOMPARE(chips[0].count, 5);
        QCOMPARE(chips[1].count, 2);
        QCOMPARE(chips[2].count, 2);
        for (const auto& c : chips) {
            if (c.label == "#pvp")
                QCOMPARE(c.count, 2);
            if (c.label == "#Friends")
                QCOMPARE(c.count, 2);
            if (c.label == "Vanilla")
                QCOMPARE(c.count, 1);
        }
    }

    void emptyLibraryStillOffersTheFixedChips()
    {
        const auto chips = buildChips({});
        QCOMPARE(chips.size(), 3);
        QCOMPARE(chips[1].count, 0);
    }

    void tagSpellingFollowsTheFirstInstance()
    {
        const auto chips = buildChips({ inst("a", "", { "PvP" }), inst("b", "", { "pvp" }) });
        QCOMPARE(chips.last().label, QStringLiteral("#PvP"));
        QCOMPARE(chips.last().count, 2);
    }

    void unusualLoadersSortAfterTheKnownOnes()
    {
        const auto chips = buildChips({ inst("a", "LiteLoader"), inst("b", "Quilt"), inst("c", "Fabric") });
        QStringList labels;
        for (const auto& c : chips)
            labels << c.label;
        QCOMPARE(labels.mid(3), (QStringList{ "Fabric", "Quilt", "LiteLoader" }));
    }

    void aVanishedChipFallsBackToAll()
    {
        QList<Facts> library = kLibrary;
        const Chip pvp{ Kind::Tag, "pvp" };
        QCOMPARE(resolveChip(pvp, buildChips(library)), pvp);
        QCOMPARE(resolveChip(Chip{ Kind::Tag, "PVP" }, buildChips(library)).value, QStringLiteral("pvp"));  // list spelling

        library[0].tags = { "Friends" };  // the two instances lose "pvp"
        library[2].tags = { "tech" };
        QCOMPARE(resolveChip(pvp, buildChips(library)), Chip{});

        const Chip fabric{ Kind::Loader, "Fabric" };
        library.removeAt(0);  // the only Fabric instance is gone
        QCOMPARE(resolveChip(fabric, buildChips(library)), Chip{});
        // fixed chips never vanish
        QCOMPARE(resolveChip(Chip{ Kind::Running, {} }, buildChips({})), (Chip{ Kind::Running, {} }));
    }
};

QTEST_GUILESS_MAIN(LibraryFilterTest)
#include "LibraryFilter_test.moc"
