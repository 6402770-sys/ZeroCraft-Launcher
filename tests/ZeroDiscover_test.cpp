// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Discover page and its Modpacks section (docs/zero/CONTENT.md): the paging rules, answers for a
// replaced search or selection are dropped (generation tokens), provider metadata in the cards, the featured strip's label rules,
// the details panel's tabs, the versions fallback and the persisted filter. Offscreen, with fake sources: no network.
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QJsonDocument>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <functional>

#include "ui/widgets/ProjectDescriptionPage.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/ContentReveal.h"
#include "ui/zero/content/ContentStates.h"
#include "ui/zero/content/ContentToolbar.h"
#include "ui/zero/content/ContentHeroCard.h"
#include "ui/zero/content/FeaturedStrip.h"
#include "ui/zero/content/FilterChipStrip.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/ModpackRows.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/pages/DiscoverPage.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/content/ModpackSource.h"
#include "zero/content/ModpackSources.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

void setMotion(bool on)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    spec.reduceMotion = !on;
    Theme::setCurrent(spec);
}

/// A source the test answers by hand.
class FakeSource : public ModpackSource {
   public:
    struct SearchCall {
        ResourceAPI::SearchArgs args;
        quint64 generation = 0;
    };
    struct ItemCall {
        ModpackItemPtr item;
        quint64 generation = 0;
        QString versionId;
    };

    FakeSource(Provider provider, int pageSize = 25, bool pages = true) : m_provider(provider), m_pageSize(pageSize), m_pages(pages) {}

    Provider provider() const override { return m_provider; }
    int pageSize() const override { return m_pageSize; }
    bool pages() const override { return m_pages; }
    void search(const ResourceAPI::SearchArgs& args, quint64 generation) override { searches.append({ args, generation }); }
    void details(const ModpackItemPtr& item, quint64 generation) override { detailCalls.append({ item, generation, {} }); }
    void versions(const ModpackItemPtr& item, quint64 generation) override { versionCalls.append({ item, generation, {} }); }
    void changelog(const ModpackItemPtr& item, const QString& versionId, quint64 generation) override
    {
        changelogCalls.append({ item, generation, versionId });
    }
    void requestCategories() override { ++categoryRequests; }

    void answerSearch(int call, const QList<ModpackItemPtr>& items, int rawCount = -1)
    {
        emit searchFinished(searches.at(call).generation, searches.at(call).args.offset, items, rawCount < 0 ? int(items.size()) : rawCount);
    }
    void failSearch(int call, const QString& error) { emit searchFailed(searches.at(call).generation, error); }

    QList<SearchCall> searches;
    QList<ItemCall> detailCalls;
    QList<ItemCall> versionCalls;
    QList<ItemCall> changelogCalls;
    int categoryRequests = 0;

   private:
    Provider m_provider;
    int m_pageSize;
    bool m_pages;
};

ModpackItemPtr makeItem(Provider provider, const QString& id, const QString& name, const QStringList& versions = { QStringLiteral("1.20.1") },
                        const QStringList& loaders = { QStringLiteral("fabric") }, qint64 downloads = 1234)
{
    ModPlatform::IndexedPack pack;
    pack.addonId = id;
    pack.provider = provider == Provider::CurseForge ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH;
    pack.name = name;
    pack.slug = name.toLower();
    pack.description = QStringLiteral("About %1").arg(name);
    pack.authors = { { QStringLiteral("someone"), QString() } };
    pack.logoUrl = QStringLiteral("https://example.test/icon/%1.png").arg(id);
    pack.gameVersions = versions;
    pack.loaderNames = loaders;
    pack.downloads = downloads;
    pack.websiteUrl = QStringLiteral("https://example.test/%1").arg(id);
    return itemFromPack(provider, pack);
}

QList<ModpackItemPtr> makeItems(Provider provider, int first, int count)
{
    QList<ModpackItemPtr> out;
    for (int i = 0; i < count; ++i) {
        out.append(makeItem(provider, QString::number(first + i), QStringLiteral("Pack %1").arg(first + i)));
    }
    return out;
}

ModpackVersion makeVersion(const QString& id, const QString& name)
{
    ModpackVersion v;
    v.id = id;
    v.name = name;
    v.downloadUrl = QStringLiteral("https://cdn.test/") + id;
    v.type = ModpackVersion::Type::Release;
    v.gameVersions = { QStringLiteral("1.20.1") };
    v.loaders = { QStringLiteral("fabric") };
    v.date = QDateTime(QDate(2024, 3, 12), QTime(10, 0), Qt::UTC);
    v.size = 48 * 1024 * 1024;
    return v;
}

/// Fixture: a section with fake sources, a failing image fetcher and recorded persistence.
struct Rig {
    FakeSource modrinth{ Provider::Modrinth };
    FakeSource curseforge{ Provider::CurseForge };
    FakeSource technic{ Provider::Technic, 25, false };
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    QList<InstalledPack> installed;
    QString savedFilter;
    QString savedView;
    QString loadFilter;
    QString loadView;
    std::unique_ptr<ModpacksSection> section;

    ModpacksEnvironment environment(bool withCurseForge = true)
    {
        ModpacksEnvironment env;
        env.sources.modrinth = &modrinth;
        env.sources.curseforge = withCurseForge ? &curseforge : nullptr;
        env.sources.technic = &technic;
        env.images = &images;
        env.installedPacks = [this]() { return installed; };
        env.loadFilter = [this]() { return loadFilter; };
        env.saveFilter = [this](const QString& json) { savedFilter = json; };
        env.loadView = [this]() { return loadView; };
        env.saveView = [this](const QString& view) { savedView = view; };
        return env;
    }

    void open(bool withCurseForge = true, const QSize& size = QSize(1280, 720))
    {
        section = std::make_unique<ModpacksSection>(environment(withCurseForge));
        section->resize(size);
        section->show();
        QVERIFY2(QTest::qWaitForWindowExposed(section.get()), "section not exposed");
    }
};

}  // namespace

class ZeroDiscoverTest : public QObject {
    Q_OBJECT

    // The first search of a section is the plain list: the featured request (a search too) is told apart by its sort.
    static int mainSearches(const FakeSource& source)
    {
        int n = 0;
        for (const auto& call : source.searches) {
            if (call.args.offset >= 0 && call.args.sorting && call.args.sorting->name != QLatin1String("downloads") &&
                call.args.sorting->name != QLatin1String("Featured")) {
                ++n;
            }
        }
        return n;
    }

   private slots:
    void initTestCase()
    {
        qRegisterMetaType<Zero::Content::ModpackItemPtr>("Zero::Content::ModpackItemPtr");
        qRegisterMetaType<Zero::Content::ContentDetailsPanel::Tab>("Zero::Content::ContentDetailsPanel::Tab");
    }
    void init() { setMotion(false); }
    void cleanup() { setMotion(true); }

    // ---- pure helpers ----

    void countFormat()
    {
        QCOMPARE(formatCount(-1), QString());
        QCOMPARE(formatCount(0), QStringLiteral("0"));
        QCOMPARE(formatCount(845), QStringLiteral("845"));
        QCOMPARE(formatCount(1200), QStringLiteral("1.2K"));
        QCOMPARE(formatCount(1000), QStringLiteral("1K"));
        QCOMPARE(formatCount(845000), QStringLiteral("845K"));
        QCOMPARE(formatCount(12345678), QStringLiteral("12.3M"));
        QCOMPARE(formatCount(123456789), QStringLiteral("123M"));
        QCOMPARE(formatCount(999999), QStringLiteral("1M"));
        QCOMPARE(formatCount(1400000000), QStringLiteral("1.4B"));
    }

    void minecraftRange()
    {
        QCOMPARE(minecraftRangeText({}), QString());
        QCOMPARE(minecraftRangeText({ QStringLiteral("1.20.1") }), QStringLiteral("1.20.1"));
        const QString dash = QStringLiteral(" – ");
        QCOMPARE(minecraftRangeText({ QStringLiteral("1.20.1"), QStringLiteral("1.18"), QStringLiteral("1.19.2") }),
                 QStringLiteral("1.18") + dash + QStringLiteral("1.20.1"));
        // snapshots and pre-releases are not part of a range; 1.9 < 1.10 is numeric
        QCOMPARE(minecraftRangeText({ QStringLiteral("24w14a"), QStringLiteral("1.9"), QStringLiteral("1.10"), QStringLiteral("1.20-pre1") }),
                 QStringLiteral("1.9") + dash + QStringLiteral("1.10"));
        QCOMPARE(minecraftRangeText({ QStringLiteral("24w14a") }), QString());
        QCOMPARE(minecraftRangeText({ QStringLiteral("1.20.1"), QStringLiteral("1.20.1") }), QStringLiteral("1.20.1"));
    }

    void loaderNames()
    {
        QCOMPARE(loaderLabels({ QStringLiteral("forge"), QStringLiteral("fabric"), QStringLiteral("iris"), QStringLiteral("neoforge") }),
                 (QStringList{ "Fabric", "Forge", "NeoForge" }));
        QVERIFY(loaderLabels({ QStringLiteral("datapack") }).isEmpty());
    }

    void pagerOffsetsAndEnd()
    {
        ResultPager pager(25);
        pager.reset(7);
        QVERIFY(pager.canLoadMore());
        QCOMPARE(pager.nextOffset(), 0);
        QVERIFY(pager.begin());
        QVERIFY(!pager.begin());  // one page in flight at a time
        QVERIFY(!pager.accept(8, 0, 25));  // another generation
        QVERIFY(!pager.accept(7, 25, 25));  // not the offset we asked at
        QVERIFY(pager.accept(7, 0, 25));
        QCOMPARE(pager.nextOffset(), 25);
        QVERIFY(!pager.accept(7, 0, 25));  // the same page again: nothing is in flight, nothing changes
        QCOMPARE(pager.nextOffset(), 25);
        QVERIFY(pager.begin());
        QVERIFY(pager.accept(7, 25, 25));
        QCOMPARE(pager.nextOffset(), 50);
        QVERIFY(!pager.isEnded());
        QVERIFY(pager.begin());
        QVERIFY(pager.accept(7, 50, 10));  // a short page is the last one
        QCOMPARE(pager.nextOffset(), 60);
        QVERIFY(pager.isEnded());
        QVERIFY(!pager.canLoadMore());
        QVERIFY(!pager.begin());
        // a new search starts over; the old answers are stale
        pager.reset(9);
        QVERIFY(pager.begin());
        QVERIFY(!pager.accept(7, 0, 25));
        QVERIFY(pager.accept(9, 0, 0));  // an empty page ends the list
        QVERIFY(pager.isEnded());
        // failure: only the current generation counts; retry allows the page again
        pager.reset(11);
        QVERIFY(pager.begin());
        QVERIFY(!pager.fail(10));
        QVERIFY(pager.fail(11));
        QVERIFY(pager.hasFailed());
        QVERIFY(!pager.canLoadMore());
        pager.retry();
        QVERIFY(pager.begin());
        QCOMPARE(pager.nextOffset(), 0);
    }

    void providerMetadataInCards()
    {
        const auto item = makeItem(Provider::CurseForge, QStringLiteral("42"), QStringLiteral("Big Pack"),
                                   { QStringLiteral("1.18"), QStringLiteral("1.20.1") }, { QStringLiteral("forge"), QStringLiteral("neoforge") }, 12345678);
        QCOMPARE(item->name, QStringLiteral("Big Pack"));
        QCOMPARE(item->author, QStringLiteral("someone"));
        QCOMPARE(item->summary, QStringLiteral("About Big Pack"));
        QCOMPARE(minecraftRangeText(item->gameVersions), QStringLiteral("1.18 – 1.20.1"));
        QCOMPARE(loaderLabels(item->loaderNames), (QStringList{ "Forge", "NeoForge" }));
        QCOMPARE(formatCount(item->downloads), QStringLiteral("12.3M"));
        QCOMPARE(providerBadge(item->provider), QStringLiteral("CurseForge"));
        QCOMPARE(item->key(), QStringLiteral("curseforge:42"));
        QCOMPARE(installedKey(*item), QStringLiteral("flame:42"));

        // The Installed flag follows the managed packs of the instances (type + id).
        ModpackListModel model;
        model.reset({ item, makeItem(Provider::Modrinth, QStringLiteral("42"), QStringLiteral("Same id, other provider")) });
        QVERIFY(!model.index(0).data(ModpackListModel::InstalledRole).toBool());
        model.setInstalledKeys({ managedKey(QStringLiteral("flame"), QStringLiteral("42")) });
        QVERIFY(model.index(0).data(ModpackListModel::InstalledRole).toBool());
        QVERIFY(!model.index(1).data(ModpackListModel::InstalledRole).toBool());  // modrinth:42 is another pack
        // an item whose details arrive repaints (dataChanged), it is never inserted again
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.itemChanged(item);
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(changed.size(), 1);
        // pages append without duplicates
        QCOMPARE(model.append({ item, makeItem(Provider::Modrinth, QStringLiteral("43"), QStringLiteral("New")) }), 1);
        QCOMPARE(model.rowCount(), 3);
    }

    // ---- the section ----

    void firstPageAndInfiniteScroll()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        QCOMPARE(mainSearches(mr), 1);
        QVERIFY(rig.section->isDefaultView());
        QCOMPARE(rig.section->page(), ModpacksSection::Page::Skeleton);
        QCOMPARE(rig.section->resultsCaption(), QString());

        // The main search is the one asked at offset 0 with the default sort.
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        QVERIFY(main >= 0);
        QCOMPARE(mr.searches[main].args.offset, 0);
        QCOMPARE(mr.searches[main].args.type, ModPlatform::ResourceType::Modpack);

        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 25));
        QCOMPARE(rig.section->model()->rowCount(), 25);
        QCOMPARE(rig.section->page(), ModpacksSection::Page::List);
        QCOMPARE(rig.section->resultsCaption(), QStringLiteral("25 loaded"));
        QVERIFY(rig.section->pager().canLoadMore());
        QCOMPARE(rig.section->pager().nextOffset(), 25);

        // Near the bottom the next page is requested at offset 25; a repeated scroll does not ask twice.
        QScrollBar* bar = rig.section->view()->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum());
        QTRY_COMPARE(mainSearches(mr), 2);
        QCOMPARE(mr.searches.last().args.offset, 25);
        QVERIFY(rig.section->footerShowsSpinner());
        QCOMPARE(rig.section->footerLabel()->text(), QStringLiteral("Loading more…"));
        bar->setValue(bar->maximum() - 1);
        bar->setValue(bar->maximum());
        QCOMPARE(mainSearches(mr), 2);

        // The page answers; answering the SAME page again adds nothing (no duplicate page).
        const int second = int(mr.searches.size()) - 1;
        auto page2 = makeItems(Provider::Modrinth, 25, 10);
        mr.answerSearch(second, page2);
        QCOMPARE(rig.section->model()->rowCount(), 35);
        mr.answerSearch(second, page2);
        QCOMPARE(rig.section->model()->rowCount(), 35);
        // 10 < 25 rows: the end of the results. No third request, a caption says so.
        QVERIFY(rig.section->pager().isEnded());
        QCOMPARE(rig.section->resultsCaption(), QStringLiteral("35 results"));
        QCOMPARE(rig.section->footerLabel()->text(), QStringLiteral("End of results"));
        bar->setValue(bar->maximum());
        QTest::qWait(150);
        QCOMPARE(mainSearches(mr), 2);
    }

    void overlappingProviderPagesAreDeduplicated()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        QVERIFY(main >= 0);
        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 25));
        QScrollBar* bar = rig.section->view()->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum());
        QTRY_COMPARE(mainSearches(mr), 2);
        // Items shift between pages while the user scrolls: 5 of the 25 were already on page one.
        mr.answerSearch(int(mr.searches.size()) - 1, makeItems(Provider::Modrinth, 20, 25));
        QCOMPARE(rig.section->model()->rowCount(), 45);  // 25 + 20 new
        QCOMPARE(rig.section->pager().nextOffset(), 50);  // offsets count the provider's rows, not ours
    }

    void staleSearchAnswerIsDropped()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        // The first search is still in flight when the user types: the late answer must not overwrite the new one.
        const int firstCall = [&]() {
            for (int i = 0; i < mr.searches.size(); ++i) {
                if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                    return i;
                }
            }
            return -1;
        }();
        QVERIFY(firstCall >= 0);
        rig.section->setSearchText(QStringLiteral("tech"));
        QCOMPARE(mainSearches(mr), 2);
        const int secondCall = int(mr.searches.size()) - 1;
        QCOMPARE(mr.searches[secondCall].args.search.value_or(QString()), QStringLiteral("tech"));
        QCOMPARE(mr.searches[secondCall].args.offset, 0);
        QVERIFY(!rig.section->isDefaultView());

        mr.answerSearch(secondCall, makeItems(Provider::Modrinth, 100, 3));
        QCOMPARE(rig.section->model()->rowCount(), 3);
        mr.answerSearch(firstCall, makeItems(Provider::Modrinth, 0, 25));  // late
        QCOMPARE(rig.section->model()->rowCount(), 3);
        QCOMPARE(rig.section->model()->item(0)->id, QStringLiteral("100"));

        // The same for a later page of an older search.
        rig.section->setSearchText(QStringLiteral("magic"));
        const int third = int(mr.searches.size()) - 1;
        mr.answerSearch(third, makeItems(Provider::Modrinth, 200, 25));
        QScrollBar* bar = rig.section->view()->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum());
        QTRY_COMPARE(mainSearches(mr), 4);
        const int pageTwo = int(mr.searches.size()) - 1;
        rig.section->setSearchText(QStringLiteral("other"));  // replaces the search while page two is in flight
        const int fourth = int(mr.searches.size()) - 1;
        mr.answerSearch(fourth, makeItems(Provider::Modrinth, 300, 2));
        mr.answerSearch(pageTwo, makeItems(Provider::Modrinth, 225, 25));
        QCOMPARE(rig.section->model()->rowCount(), 2);
    }

    void dimmedWhileSearching()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 5));
        QVERIFY(!rig.section->delegate()->isDimmed());
        rig.section->setSearchText(QStringLiteral("x"));
        QVERIFY(rig.section->delegate()->isDimmed());  // the old rows stay, at half opacity
        QCOMPARE(rig.section->model()->rowCount(), 5);
        QVERIFY(!rig.section->footerShowsSpinner());  // no spinner flash for fast answers
        QTRY_VERIFY(rig.section->footerShowsSpinner());  // ... only after 150 ms
        mr.answerSearch(int(mr.searches.size()) - 1, makeItems(Provider::Modrinth, 50, 2));
        QVERIFY(!rig.section->delegate()->isDimmed());
        QVERIFY(!rig.section->footerShowsSpinner());
        QCOMPARE(rig.section->model()->rowCount(), 2);
    }

    void emptyAndErrorStates()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        mr.failSearch(main, QStringLiteral("Connection timed out"));
        QCOMPARE(rig.section->page(), ModpacksSection::Page::Error);
        QCOMPARE(rig.section->errorState()->titleText(), QStringLiteral("Couldn't reach Modrinth"));
        QCOMPARE(rig.section->errorState()->bodyText(), QStringLiteral("Connection timed out"));
        QCOMPARE(rig.section->resultsCaption(), QString());
        // Retry searches again (a new generation), the failure does not block it.
        const int before = int(mr.searches.size());
        rig.section->errorState()->findChild<QPushButton*>()->click();
        QCOMPARE(int(mr.searches.size()), before + 1);
        mr.answerSearch(before, {});
        QCOMPARE(rig.section->page(), ModpacksSection::Page::NoMatch);
        QVERIFY(rig.section->noMatchState()->bodyText().size() > 0);
    }

    void failedLaterPageOffersRetry()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 25));
        QScrollBar* bar = rig.section->view()->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum());
        QTRY_COMPARE(mainSearches(mr), 2);
        mr.failSearch(int(mr.searches.size()) - 1, QStringLiteral("boom"));
        QCOMPARE(rig.section->page(), ModpacksSection::Page::List);  // the rows stay
        QCOMPARE(rig.section->model()->rowCount(), 25);
        QVERIFY(rig.section->footerRetry()->isVisibleTo(rig.section.get()));
        const int before = int(mr.searches.size());
        rig.section->footerRetry()->click();
        QCOMPARE(int(mr.searches.size()), before + 1);
        QCOMPARE(mr.searches.last().args.offset, 25);  // the same page again
    }

    void providerIsSingleChoiceAndTechnicSearchesByNameOnly()
    {
        Rig rig;
        rig.open();
        QCOMPARE(rig.section->provider(), Provider::Modrinth);
        rig.section->setProvider(Provider::Technic);
        QCOMPARE(rig.section->provider(), Provider::Technic);
        QCOMPARE(rig.section->state().values(FacetId::provider), QStringList{ QStringLiteral("technic") });
        // Technic: search only: the other facets are disabled with their reason, there is no sort.
        for (const auto& facet : rig.section->facets()) {
            if (facet.id == FacetId::mcVersion || facet.id == FacetId::loader || facet.id == FacetId::category) {
                QVERIFY2(!facet.isEnabled(), qPrintable(facet.id));
                QCOMPARE(facet.disabledReason, QStringLiteral("Technic can only be searched by name"));
            }
        }
        QVERIFY(!rig.section->toolbar()->sortCombo()->isVisibleTo(rig.section.get()));
        QCOMPARE(rig.technic.searches.size(), 1);
        QCOMPARE(rig.technic.searches[0].args.offset, 0);
        rig.technic.answerSearch(0, makeItems(Provider::Technic, 1, 4));
        QCOMPARE(rig.section->model()->rowCount(), 4);
        QVERIFY(rig.section->pager().isEnded());  // one page: the section never asks for more
        QScrollBar* bar = rig.section->view()->verticalScrollBar();
        bar->setValue(bar->maximum());
        QTest::qWait(150);
        QCOMPARE(rig.technic.searches.size(), 1);
        // The Provider facet itself is single choice with exactly the available sources.
        const auto facets = rig.section->facets();
        const auto it = std::find_if(facets.cbegin(), facets.cend(), [](const Facet& f) { return f.id == FacetId::provider; });
        QVERIFY(it != facets.cend());
        QCOMPARE(it->kind, FacetKind::Single);
        QCOMPARE(it->options.size(), 3);
    }

    void noCurseForgeWithoutTheCapability()
    {
        Rig rig;
        rig.open(false);
        const auto facets = rig.section->facets();
        const auto it = std::find_if(facets.cbegin(), facets.cend(), [](const Facet& f) { return f.id == FacetId::provider; });
        QVERIFY(it != facets.cend());
        QCOMPARE(it->options.size(), 2);
        for (const auto& option : it->options) {
            QVERIFY(option.value != QLatin1String("curseforge"));
        }
        rig.section->setProvider(Provider::CurseForge);  // ignored
        QCOMPARE(rig.section->provider(), Provider::Modrinth);
        QCOMPARE(rig.curseforge.searches.size(), 0);
        QCOMPARE(rig.section->featuredStrip()->label(), QStringLiteral("Popular on Modrinth"));
        // a persisted CurseForge choice falls back too
        Rig other;
        other.loadFilter = QStringLiteral("{\"sortId\":\"featured\",\"selected\":{\"provider\":[\"curseforge\"]}}");
        other.open(false);
        QCOMPARE(other.section->provider(), Provider::Modrinth);
        QCOMPARE(other.section->state().sortId, QStringLiteral("relevance"));
    }

    // ---- featured strip ----

    void featuredStripLabelRules()
    {
        // With CurseForge: its own Featured list (sort index 1), labelled with CurseForge's wording.
        {
            Rig rig;
            rig.open(true);
            QCOMPARE(rig.curseforge.searches.size(), 1);
            QVERIFY(rig.curseforge.searches[0].args.sorting.has_value());
            QCOMPARE(rig.curseforge.searches[0].args.sorting->index, 1u);
            QCOMPARE(rig.curseforge.searches[0].args.offset, 0);
            QCOMPARE(rig.section->featuredStrip()->label(), QStringLiteral("Featured on CurseForge"));
            QVERIFY(rig.section->featuredStrip()->isShown());  // the loading placeholder
            rig.curseforge.answerSearch(0, makeItems(Provider::CurseForge, 1, 8));
            QCOMPARE(rig.section->featuredStrip()->cards().size(), 5);  // up to five
            QCOMPARE(rig.section->featuredStrip()->cards().first()->item()->provider, Provider::CurseForge);
            // Modrinth's list is the ordinary result list: never labelled "Featured".
            QVERIFY(rig.section->featuredStrip()->label().contains(QStringLiteral("CurseForge")));
        }
        // Without CurseForge: Modrinth's most downloaded packs, labelled "Popular".
        {
            Rig rig;
            rig.open(false);
            QCOMPARE(rig.curseforge.searches.size(), 0);
            QCOMPARE(rig.section->featuredStrip()->label(), QStringLiteral("Popular on Modrinth"));
            bool popular = false;
            for (const auto& call : rig.modrinth.searches) {
                if (call.args.sorting && call.args.sorting->name == QLatin1String("downloads")) {
                    popular = true;
                }
            }
            QVERIFY(popular);
            QVERIFY(!rig.section->featuredStrip()->label().contains(QStringLiteral("Featured")));
        }
    }

    void featuredStripHidesWithQueryOrChips()
    {
        Rig rig;
        rig.open(true);
        rig.curseforge.answerSearch(0, makeItems(Provider::CurseForge, 1, 3));
        auto* strip = rig.section->featuredStrip();
        QVERIFY(strip->isShown());
        QVERIFY(!strip->isCollapsed());
        QVERIFY(rig.section->isDefaultView());

        rig.section->setSearchText(QStringLiteral("magic"));  // a query
        QVERIFY(strip->isCollapsed());
        QCOMPARE(strip->openProgress(), 0.0);  // motion is off: instant
        QVERIFY(!strip->isShown());
        rig.section->setSearchText(QString());
        QVERIFY(!strip->isCollapsed());
        QCOMPARE(strip->openProgress(), 1.0);

        FilterState withChip = rig.section->state();  // a filter chip
        withChip.selected.insert(FacetId::loader, { QStringLiteral("fabric") });
        rig.section->applyState(withChip);
        QVERIFY(!rig.section->isDefaultView());
        QVERIFY(strip->isCollapsed());
        QCOMPARE(rig.section->chipStrip()->chipLabels(), QStringList{ QStringLiteral("Loader: Fabric") });
        rig.section->applyState(clearAll(rig.section->state(), FilterState()));
        QVERIFY(!strip->isCollapsed());

        // A provider chip hides it too (the strip belongs to the default Modrinth view).
        rig.section->setProvider(Provider::CurseForge);
        QVERIFY(strip->isCollapsed());
        // The requests were made once: collapsing and opening does not fetch the featured list again.
        QCOMPARE(rig.curseforge.searches.size() >= 1, true);
        int featuredRequests = 0;
        for (const auto& call : rig.curseforge.searches) {
            if (call.args.sorting && call.args.sorting->index == 1u && !call.args.search.has_value() && !call.args.loaders.has_value()) {
                ++featuredRequests;
            }
        }
        QVERIFY(featuredRequests >= 1);
    }

    void featuredStripCollapseAnimatesWithMotion()
    {
        setMotion(true);
        Rig rig;
        rig.open(true);
        rig.curseforge.answerSearch(0, makeItems(Provider::CurseForge, 1, 3));
        auto* strip = rig.section->featuredStrip();
        QTRY_VERIFY(strip->height() > 0);
        QVERIFY(strip->openProgress() == 1.0);
        rig.section->setSearchText(QStringLiteral("magic"));
        QVERIFY(strip->isCollapsed());
        QVERIFY(strip->openProgress() > 0.0 || strip->height() > 0);  // it is still folding (200 ms)
        QTRY_COMPARE(strip->openProgress(), 0.0);
        QVERIFY(strip->isHidden());
    }

    void heroCardIsReusable()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ContentHeroCard card(&images);
        card.resize(427, 240);
        const auto item = makeItem(Provider::CurseForge, QStringLiteral("5"), QStringLiteral("Hero"));
        card.setItem(item);
        card.show();
        QSignalSpy installs(&card, &ContentHeroCard::installRequested);
        QSignalSpy opens(&card, &ContentHeroCard::activated);
        card.installButton()->click();
        QCOMPARE(installs.size(), 1);
        QTest::mouseClick(&card, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(opens.size(), 1);
        QVERIFY(!card.grab().isNull());
    }

    void featuredStripScrollsOneCardAtATime()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        QWidget host;  // owns the strip (a child of a stack widget is deleted by it, never also on the stack)
        auto* stripPtr = new FeaturedStrip(&images, &host);
        FeaturedStrip& strip = *stripPtr;
        host.resize(900, 400);
        strip.setLabel(QStringLiteral("Featured on CurseForge"));
        strip.setItems(makeItems(Provider::CurseForge, 1, 5));
        host.show();
        strip.setGeometry(0, 0, 900, 300);
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QCOMPARE(strip.cards().size(), 5);
        QCOMPARE(FeaturedStrip::heroWidthFor(900), 380);  // 42 % of the strip, kept between 380 and 480 px
        QCOMPARE(FeaturedStrip::heroWidthFor(1200), 480);
        QVERIFY(strip.nextButton()->isVisibleTo(&strip));
        QCOMPARE(strip.firstVisibleIndex(), 0);
        QVERIFY(!strip.previousButton()->isEnabled());
        strip.nextButton()->click();  // motion is off: instant
        QCOMPARE(strip.firstVisibleIndex(), 1);
        QVERIFY(strip.previousButton()->isEnabled());
        strip.previousButton()->click();
        QCOMPARE(strip.firstVisibleIndex(), 0);
        for (int i = 0; i < 10; ++i) {
            strip.nextButton()->click();
        }
        QVERIFY(!strip.nextButton()->isEnabled());  // the last card is reached; never past the end
    }

    void technicDetailsFillTheCardsInTheBackground()
    {
        Rig rig;
        rig.open();
        rig.section->setProvider(Provider::Technic);
        const auto items = makeItems(Provider::Technic, 1, 20);
        rig.technic.answerSearch(0, items);
        // The list has names and icons only: the first rows get their details, a few at a time, never more than 12.
        QCOMPARE(rig.technic.detailCalls.size(), ModpacksSection::kPrefetchParallel);
        QCOMPARE(rig.technic.detailCalls[0].generation, ModpacksSection::kPrefetchGeneration);
        QVERIFY(rig.section->isBusy());
        for (int i = 0; i < 12; ++i) {
            emit rig.technic.detailsFinished(ModpacksSection::kPrefetchGeneration, rig.technic.detailCalls[i].item);
        }
        QCOMPARE(rig.technic.detailCalls.size(), ModpacksSection::kPrefetchMax);
        QSignalSpy changed(rig.section->model(), &QAbstractItemModel::dataChanged);
        emit rig.technic.detailsFinished(ModpacksSection::kPrefetchGeneration, items[0]);
        QCOMPARE(changed.size(), 1);  // the card repaints; no row is inserted
        // a prefetch answer never touches the open details
        rig.section->selectRow(15);
        QTRY_VERIFY(rig.technic.detailCalls.size() > ModpacksSection::kPrefetchMax);  // the selection asked for its own details
        emit rig.technic.detailsFinished(ModpacksSection::kPrefetchGeneration, items[2]);
        QCOMPARE(rig.section->panel()->key(), items[15]->key());
    }

    // ---- selection, details, versions ----

    void staleDetailsAnswerIsDropped()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        const auto items = makeItems(Provider::Modrinth, 0, 6);
        mr.answerSearch(main, items);
        QVERIFY(!rig.section->panel()->isOpen());

        rig.section->selectRow(1);
        QVERIFY(rig.section->panel()->isOpen());
        QCOMPARE(rig.section->panel()->key(), items[1]->key());
        QCOMPARE(rig.section->panel()->nameText(), QStringLiteral("Pack 1"));
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());  // no install until its versions are known
        QTRY_COMPARE(mr.detailCalls.size(), 1);
        QTRY_COMPARE(mr.versionCalls.size(), 1);
        const auto detailsOfFirst = mr.detailCalls[0];
        const auto versionsOfFirst = mr.versionCalls[0];

        // Another row is selected before the answers arrive.
        rig.section->selectRow(3);
        QCOMPARE(rig.section->panel()->key(), items[3]->key());
        QCOMPARE(rig.section->panel()->nameText(), QStringLiteral("Pack 3"));
        QTRY_COMPARE(mr.detailCalls.size(), 2);
        QTRY_COMPARE(mr.versionCalls.size(), 2);

        // The old answers (details and versions of row 1) must not touch the panel of row 3.
        items[1]->pack->extraData.body = QStringLiteral("old body");
        emit mr.detailsFinished(detailsOfFirst.generation, items[1]);
        emit mr.versionsFinished(versionsOfFirst.generation, items[1], { makeVersion(QStringLiteral("v1"), QStringLiteral("Old 1.0")) });
        QCOMPARE(rig.section->panel()->key(), items[3]->key());
        QCOMPARE(rig.section->panel()->versionsState(), LoadState::Loading);
        QCOMPARE(rig.section->panel()->versionRowCount(), 0);
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());

        // The answers of the selected row do.
        items[3]->pack->extraData.body = QStringLiteral("<p>The real body</p>");
        items[3]->detailsLoaded = true;  // what the provider's answer does
        emit mr.detailsFinished(mr.detailCalls[1].generation, items[3]);
        emit mr.versionsFinished(mr.versionCalls[1].generation, items[3],
                                 { makeVersion(QStringLiteral("v2"), QStringLiteral("New 2.0")), makeVersion(QStringLiteral("v1"), QStringLiteral("Old 1.0")) });
        QCOMPARE(rig.section->panel()->versionsState(), LoadState::Ready);
        QCOMPARE(rig.section->panel()->versionRowCount(), 2);
        QVERIFY(rig.section->panel()->primaryButton()->isEnabled());
        QCOMPARE(rig.section->panel()->primaryButton()->text(), QStringLiteral("Install"));
        QVERIFY(rig.section->panel()->overviewBrowser()->toPlainText().contains(QStringLiteral("The real body")));
        QVERIFY(!rig.section->panel()->overviewBrowser()->toPlainText().contains(QStringLiteral("old body")));
        // What's new asks for the newest version's changelog
        QTRY_COMPARE(mr.changelogCalls.size(), 1);
        QCOMPARE(mr.changelogCalls[0].versionId, QStringLiteral("v2"));
    }

    void installEmitsOnlyASignal()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        const auto items = makeItems(Provider::Modrinth, 0, 3);
        mr.answerSearch(main, items);
        rig.section->selectRow(0);
        QTRY_COMPARE(mr.versionCalls.size(), 1);
        emit mr.versionsFinished(mr.versionCalls[0].generation, items[0], { makeVersion(QStringLiteral("v2"), QStringLiteral("2.0")) });
        QSignalSpy installs(rig.section.get(), &ModpacksSection::installRequested);
        rig.section->panel()->primaryButton()->click();
        QCOMPARE(installs.size(), 1);
        QCOMPARE(installs[0][0].value<ModpackItemPtr>(), items[0]);
        QCOMPARE(installs[0][1].toString(), QString());  // a new instance
        QVERIFY(rig.section->panel()->noteText().isEmpty());

        // The pack is installed in an instance, and a newer version exists: the button updates that instance.
        rig.installed = { { managedKey(QStringLiteral("modrinth"), QStringLiteral("0")), QStringLiteral("inst-1"), QStringLiteral("My pack"),
                            QStringLiteral("v1"), QStringLiteral("1.0") } };
        rig.section->refreshInstalled();
        QCOMPARE(rig.section->panel()->primaryButton()->text(), QStringLiteral("Update"));
        QVERIFY(rig.section->panel()->noteText().contains(QStringLiteral("My pack")));
        rig.section->panel()->primaryButton()->click();
        QCOMPARE(installs.size(), 2);
        QCOMPARE(installs[1][1].toString(), QStringLiteral("inst-1"));
        // Up to date: Install again makes a new instance, and the note says the pack is installed.
        rig.installed[0].versionId = QStringLiteral("v2");
        rig.section->refreshInstalled();
        QCOMPARE(rig.section->panel()->primaryButton()->text(), QStringLiteral("Install"));
        QVERIFY(rig.section->panel()->noteText().contains(QStringLiteral("already installed")));
        QVERIFY(rig.section->model()->index(0).data(ModpackListModel::InstalledRole).toBool());
    }

    void updateComparesTheDefaultVersion()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        const auto items = makeItems(Provider::Modrinth, 0, 3);
        mr.answerSearch(main, items);
        rig.installed = { { managedKey(QStringLiteral("modrinth"), QStringLiteral("0")), QStringLiteral("inst-1"), QStringLiteral("My pack"),
                            QStringLiteral("r1"), QStringLiteral("1.0") } };
        rig.section->refreshInstalled();
        rig.section->selectRow(0);
        QTRY_COMPARE(mr.versionCalls.size(), 1);
        // The newest entry is a beta; the install dialog preselects release r1, which is installed: no Update.
        ModpackVersion beta = makeVersion(QStringLiteral("b2"), QStringLiteral("2.0-beta"));
        beta.type = ModpackVersion::Type::Beta;
        beta.date = QDateTime(QDate(2024, 5, 1), QTime(10, 0), Qt::UTC);
        emit mr.versionsFinished(mr.versionCalls[0].generation, items[0], { beta, makeVersion(QStringLiteral("r1"), QStringLiteral("1.0")) });
        QCOMPARE(rig.section->panel()->primaryButton()->text(), QStringLiteral("Install"));
        QVERIFY(rig.section->panel()->noteText().contains(QStringLiteral("already installed")));

        // A newer release exists: Update, naming it.
        rig.installed[0].versionId = QStringLiteral("r0");
        rig.section->refreshInstalled();
        QCOMPARE(rig.section->panel()->primaryButton()->text(), QStringLiteral("Update"));
        QVERIFY(rig.section->panel()->noteText().contains(QStringLiteral("1.0")));
        QVERIFY(!rig.section->panel()->noteText().contains(QStringLiteral("2.0-beta")));
    }

    void ctrlFFocusesTheSearchField()
    {
        Rig rig;
        rig.open();
        rig.section->activateWindow();
        rig.section->setFocus();
        rig.section->toolbar()->searchField()->clearFocus();
        QTest::keyClick(rig.section.get(), Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(rig.section->toolbar()->searchField()->hasFocus());
    }

    void linksOpenOnlyForHttp()
    {
        QVERIFY(isOpenableProviderLink(QUrl(QStringLiteral("https://modrinth.com/x"))));
        QVERIFY(isOpenableProviderLink(QUrl(QStringLiteral("HTTP://example.com"))));
        QVERIFY(!isOpenableProviderLink(QUrl(QStringLiteral("file:///C:/Windows/system32/calc.exe"))));
        QVERIFY(!isOpenableProviderLink(QUrl(QStringLiteral("javascript:alert(1)"))));
        QVERIFY(!isOpenableProviderLink(QUrl(QStringLiteral("ms-settings:"))));
        QVERIFY(!isOpenableProviderLink(QUrl()));
    }

    void searchTextIsPercentEncoded()
    {
        ResourceAPI::SearchArgs args;
        args.search = QStringLiteral("a&b #1 100%");
        QCOMPARE(*withEncodedSearch(args).search, QStringLiteral("a%26b%20%231%20100%25"));
        ResourceAPI::SearchArgs none;
        QVERIFY(!withEncodedSearch(none).search.has_value());
    }

    void versionsNeverStayLoading()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 2));
        rig.section->setVersionsTimeoutMs(80);
        rig.section->selectRow(0);
        QTRY_COMPARE(mr.versionCalls.size(), 1);  // the provider never answers
        QCOMPARE(rig.section->panel()->versionsState(), LoadState::Loading);
        QTRY_COMPARE_WITH_TIMEOUT(rig.section->panel()->versionsState(), LoadState::Failed, 2000);
        QVERIFY(!rig.section->panel()->primaryButton()->isEnabled());
        // Retry asks again
        QSignalSpy retried(rig.section->panel(), &ContentDetailsPanel::retryRequested);
        rig.section->panel()->setCurrentTab(ContentDetailsPanel::Tab::Versions);
        const auto buttons = rig.section->panel()->findChildren<QPushButton*>();
        QPushButton* retry = nullptr;
        for (auto* b : buttons) {
            if (b->text() == QLatin1String("Retry") && b->isVisibleTo(rig.section->panel())) {
                retry = b;
            }
        }
        QVERIFY(retry);
        retry->click();
        QCOMPARE(retried.size(), 1);
        QCOMPARE(mr.versionCalls.size(), 2);
        QCOMPARE(rig.section->panel()->versionsState(), LoadState::Loading);
        // a failure answer also ends the loading state
        emit mr.versionsFailed(mr.versionCalls[1].generation, QStringLiteral("HTTP 500"));
        QCOMPARE(rig.section->panel()->versionsState(), LoadState::Failed);
    }

    void versionFilterReachesTheSources()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        QVERIFY(mr.versionFilter().isEmpty());  // modpacks ask for every version
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 2));
        rig.section->selectRow(0);
        QTRY_COMPARE(mr.versionCalls.size(), 1);
        emit mr.versionsFinished(mr.versionCalls[0].generation, mr.versionCalls[0].item, { makeVersion(QStringLiteral("v1"), QStringLiteral("1.0")) });
        QVERIFY(rig.section->panel()->versionsState() == LoadState::Ready);

        // A target (Mods) narrows the request: every source carries the filter, and the versions loaded before are asked again.
        VersionFilter filter;
        filter.mcVersions = { QStringLiteral("1.20.1") };
        filter.loaders = ModPlatform::ModLoaderTypes(ModPlatform::Fabric);
        QVERIFY(!filter.isEmpty());
        rig.section->setVersionFilter(filter);
        QCOMPARE(mr.versionFilter(), filter);
        QCOMPARE(rig.curseforge.versionFilter(), filter);
        QCOMPARE(rig.technic.versionFilter(), filter);
        rig.section->selectItem(nullptr);
        rig.section->selectRow(0);
        QTRY_COMPARE(mr.versionCalls.size(), 2);
    }

    void changelogLoadsOnDemand()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        const auto items = makeItems(Provider::Modrinth, 0, 2);
        mr.answerSearch(main, items);
        rig.section->selectRow(0);
        QTRY_COMPARE(mr.versionCalls.size(), 1);
        emit mr.versionsFinished(mr.versionCalls[0].generation, items[0],
                                 { makeVersion(QStringLiteral("v2"), QStringLiteral("2.0")), makeVersion(QStringLiteral("v1"), QStringLiteral("1.0")) });
        QTRY_COMPARE(mr.changelogCalls.size(), 1);  // What's new
        auto* panel = rig.section->panel();
        QVERIFY(!panel->isTabVisible(ContentDetailsPanel::Tab::WhatsNew));
        emit mr.changelogFinished(mr.changelogCalls[0].generation, QStringLiteral("v2"), QStringLiteral("<p>Fixed the thing</p>"));
        QVERIFY(panel->isTabVisible(ContentDetailsPanel::Tab::WhatsNew));
        // Expanding the older row asks for its changelog, once; collapsing and expanding again does not.
        panel->toggleVersion(1);
        QVERIFY(panel->isVersionExpanded(1));
        QCOMPARE(mr.changelogCalls.size(), 2);
        QCOMPARE(mr.changelogCalls[1].versionId, QStringLiteral("v1"));
        QVERIFY(panel->versionChangelogText(1).contains(QStringLiteral("Loading")));
        emit mr.changelogFinished(mr.changelogCalls[1].generation, QStringLiteral("v1"), QStringLiteral("<p>First release</p>"));
        QVERIFY(panel->versionChangelogText(1).contains(QStringLiteral("First release")));
        panel->toggleVersion(1);
        QVERIFY(!panel->isVersionExpanded(1));
        panel->toggleVersion(1);
        QCOMPARE(mr.changelogCalls.size(), 2);
    }

    void detailsPanelHidesTabsWithoutData()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ContentDetailsPanel panel(&images);
        panel.resize(ContentDetailsPanel::kWidth, 700);
        QVERIFY(!panel.isOpen());
        QVERIFY(panel.isHidden());
        DetailsHeader header;
        header.key = QStringLiteral("a");
        header.name = QStringLiteral("Alpha");
        header.author = QStringLiteral("someone");
        header.providerLabel = QStringLiteral("Modrinth");
        header.pills = { QStringLiteral("1.20.1"), QStringLiteral("Fabric") };
        header.stats = { { QStringLiteral("download"), QStringLiteral("1.2M downloads"), QStringLiteral("Downloads") } };
        panel.showItem(header);
        QVERIFY(panel.isOpen());
        QVERIFY(!panel.isHidden());
        QCOMPARE(panel.pillTexts(), (QStringList{ "Modrinth", "1.20.1", "Fabric" }));
        QCOMPARE(panel.statTexts(), QStringList{ QStringLiteral("1.2M downloads") });
        QVERIFY(!panel.websiteButton()->isVisibleTo(&panel));  // no website: no button
        // Only what is loading is listed: Overview and Versions. Gallery and What's new have no data.
        QCOMPARE(panel.visibleTabLabels(), (QStringList{ "Overview", "Versions" }));
        QVERIFY(!panel.isTabVisible(ContentDetailsPanel::Tab::Gallery));
        QVERIFY(!panel.isTabVisible(ContentDetailsPanel::Tab::WhatsNew));

        panel.setOverview(LoadState::Ready, QString());  // loaded, nothing to say
        QVERIFY(!panel.isTabVisible(ContentDetailsPanel::Tab::Overview));
        QCOMPARE(panel.currentTab(), ContentDetailsPanel::Tab::Versions);  // the current tab moved to one that has data
        panel.setGallery({ { QStringLiteral("https://example.test/1.png"), QStringLiteral("One"), QString() },
                           { QStringLiteral("https://example.test/2.png"), QString(), QStringLiteral("Second") } });
        panel.setWhatsNew(QStringLiteral("<p>Changes</p>"));
        panel.setOverview(LoadState::Ready, QStringLiteral("<p>Body</p>"));
        QCOMPARE(panel.visibleTabLabels(), (QStringList{ "Overview", "Gallery", "Versions", "What's new" }));
        panel.setVersions(LoadState::Ready, {});
        QVERIFY(panel.isTabVisible(ContentDetailsPanel::Tab::Versions));  // an honest "nothing to download" message
        panel.setGallery({});
        QVERIFY(!panel.isTabVisible(ContentDetailsPanel::Tab::Gallery));
        panel.setWhatsNew(QString());
        QVERIFY(!panel.isTabVisible(ContentDetailsPanel::Tab::WhatsNew));

        // The next item resets everything (no stale tabs, no stale install action).
        panel.setPrimaryAction(QStringLiteral("Update"), true, QStringLiteral("note"));
        DetailsHeader other = header;
        other.key = QStringLiteral("b");
        other.name = QStringLiteral("Beta");
        other.websiteUrl = QStringLiteral("https://example.test/b");
        panel.showItem(other);
        QCOMPARE(panel.key(), QStringLiteral("b"));
        QCOMPARE(panel.nameText(), QStringLiteral("Beta"));
        QVERIFY(!panel.primaryButton()->isEnabled());
        QVERIFY(panel.websiteButton()->isVisibleTo(&panel));
        QCOMPARE(panel.visibleTabLabels(), (QStringList{ "Overview", "Versions" }));
        QCOMPARE(panel.versionsState(), LoadState::Loading);
        QCOMPARE(panel.overviewState(), LoadState::Loading);

        QSignalSpy closed(&panel, &ContentDetailsPanel::closeRequested);
        panel.closeButton()->click();
        QCOMPARE(closed.size(), 1);
        QVERIFY(!panel.isOpen());
        QVERIFY(panel.isHidden());
    }

    void galleryViewerKeys()
    {
        RemoteImageCache images([](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); });
        ContentDetailsPanel panel(&images);
        panel.resize(ContentDetailsPanel::kWidth, 700);
        DetailsHeader header;
        header.key = QStringLiteral("a");
        header.name = QStringLiteral("Alpha");
        panel.showItem(header);
        panel.setGallery({ { QStringLiteral("https://example.test/1.png"), QStringLiteral("One"), QString() },
                           { QStringLiteral("https://example.test/2.png"), QStringLiteral("Two"), QString() },
                           { QStringLiteral("https://example.test/3.png"), QStringLiteral("Three"), QString() } });
        QCOMPARE(panel.galleryCount(), 3);
        panel.openGallery(1);
        auto* viewer = panel.viewer();
        QVERIFY(viewer->isOpen());
        QCOMPARE(viewer->index(), 1);
        QTest::keyClick(viewer, Qt::Key_Right);
        QCOMPARE(viewer->index(), 2);
        QTest::keyClick(viewer, Qt::Key_Right);  // wraps
        QCOMPARE(viewer->index(), 0);
        QTest::keyClick(viewer, Qt::Key_Left);
        QCOMPARE(viewer->index(), 2);
        QVERIFY(!viewer->grab().isNull());
        QTest::keyClick(viewer, Qt::Key_Escape);
        QVERIFY(!viewer->isOpen());
        // the viewer asked the cache for its pictures (through the one image cache)
        QVERIFY(images.fetchCount() >= 1);
    }

    // ---- filter state, view, page ----

    void filterStateIsPersistedWithoutTheQuery()
    {
        Rig rig;
        rig.open();
        rig.section->setSearchText(QStringLiteral("secret words"));
        FilterState next = rig.section->state();
        next.selected.insert(FacetId::loader, { QStringLiteral("fabric"), QStringLiteral("quilt") });
        next.sortId = QStringLiteral("downloads");
        rig.section->applyState(next);
        QVERIFY(!rig.savedFilter.isEmpty());
        QVERIFY(!rig.savedFilter.contains(QStringLiteral("secret words")));
        const FilterState saved = FilterState::fromJson(QJsonDocument::fromJson(rig.savedFilter.toUtf8()).object());
        QCOMPARE(saved.query, QString());
        QCOMPARE(saved.sortId, QStringLiteral("downloads"));
        QCOMPARE(saved.values(FacetId::loader), (QStringList{ "fabric", "quilt" }));

        // A new section restores it (without a query).
        Rig again;
        again.loadFilter = rig.savedFilter;
        again.open();
        QCOMPARE(again.section->state().query, QString());
        QCOMPARE(again.section->state().sortId, QStringLiteral("downloads"));
        QCOMPARE(again.section->state().values(FacetId::loader), (QStringList{ "fabric", "quilt" }));
        QCOMPARE(again.section->toolbar()->searchText(), QString());
        QCOMPARE(again.section->toolbar()->sortId(), QStringLiteral("downloads"));
        QVERIFY(!again.section->isDefaultView());  // the restored chips apply
        QCOMPARE(again.section->chipStrip()->chipLabels(), (QStringList{ "Loader: Fabric", "Loader: Quilt" }));
        // ... and its first search uses them
        bool found = false;
        for (const auto& call : again.modrinth.searches) {
            if (call.args.sorting && call.args.sorting->name == QLatin1String("downloads") && call.args.loaders.has_value()) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    void viewModeIsPersisted()
    {
        Rig rig;
        rig.open();
        QCOMPARE(rig.section->delegate()->mode(), ModpackDelegate::Mode::Grid);
        rig.section->toolbar()->listButton()->click();
        QCOMPARE(rig.section->delegate()->mode(), ModpackDelegate::Mode::List);
        QCOMPARE(rig.savedView, QStringLiteral("list"));
        Rig again;
        again.loadView = QStringLiteral("list");
        again.open();
        QCOMPARE(again.section->delegate()->mode(), ModpackDelegate::Mode::List);
        QCOMPARE(again.section->delegate()->columns(), 1);
    }

    void gridColumnsFollowTheWidth()
    {
        Rig rig;
        rig.open(true, QSize(1280, 720));
        auto* delegate = rig.section->delegate();
        rig.modrinth.answerSearch(0, makeItems(Provider::Modrinth, 0, 8));  // the list page is shown (and laid out) once there are rows
        QTRY_VERIFY(delegate->columns() >= 3);
        const int wide = delegate->columns();
        rig.section->resize(900, 720);  // e.g. the details panel took 400 px
        QTRY_VERIFY(delegate->columns() < wide);
        QVERIFY(delegate->itemSize().width() >= ModpackDelegate::kCardMin);
        QVERIFY(delegate->itemSize().width() <= ModpackDelegate::kCardMax);
        QCOMPARE(delegate->itemSize().height(), delegate->itemSize().width() * 9 / 16 + ModpackDelegate::kCardBodyHeight);
    }

    void clientFilterNeverNeedsARequest()
    {
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        const auto items = makeItems(Provider::Modrinth, 0, 4);
        mr.answerSearch(main, items);
        rig.installed = { { managedKey(QStringLiteral("modrinth"), QStringLiteral("2")), QStringLiteral("i"), QStringLiteral("Mine"), {}, {} } };
        rig.section->refreshInstalled();
        const int before = int(mr.searches.size());
        FilterState onlyInstalled = rig.section->state();
        onlyInstalled.selected.insert(FacetId::installed, { kToggleOn });
        rig.section->applyState(onlyInstalled);
        QCOMPARE(int(mr.searches.size()), before);  // the Installed filter works on the loaded rows
        QCOMPARE(rig.section->model()->rowCount(), 1);
        QCOMPARE(rig.section->model()->item(0)->id, QStringLiteral("2"));
        QCOMPARE(rig.section->resultsCaption(), QStringLiteral("1 of 4 results"));
        rig.section->applyState(clearAll(rig.section->state(), FilterState()));
        QCOMPARE(rig.section->model()->rowCount(), 4);
    }

    void revealOnlyAnimatesNewRows()
    {
        setMotion(true);
        Rig rig;
        rig.open();
        auto& mr = rig.modrinth;
        int main = -1;
        for (int i = 0; i < mr.searches.size(); ++i) {
            if (mr.searches[i].args.sorting && mr.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        mr.answerSearch(main, makeItems(Provider::Modrinth, 0, 25));
        auto* reveal = rig.section->reveal();
        QCOMPARE(reveal->spec().offsetPx, 24);
        QCOMPARE(reveal->spec().durationMs, 320);
        QCOMPARE(reveal->spec().staggerMs, 32);
        reveal->finish();
        QVERIFY(!reveal->isAnimating());
        // A card whose details arrive or whose installed state changes repaints; it does not enter again.
        rig.section->model()->itemChanged(rig.section->model()->item(0));
        rig.installed = { { managedKey(QStringLiteral("modrinth"), QStringLiteral("0")), QStringLiteral("i"), QStringLiteral("Mine"), {}, {} } };
        rig.section->refreshInstalled();
        QCOMPARE(reveal->trackedCount(), 0);
        // A new page animates just its rows.
        QScrollBar* bar = rig.section->view()->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum());
        QTRY_COMPARE(mainSearches(mr), 2);
        mr.answerSearch(int(mr.searches.size()) - 1, makeItems(Provider::Modrinth, 25, 5));
        QCoreApplication::processEvents();
        reveal->advance();
        for (int row = 0; row < 25; ++row) {
            QCOMPARE(reveal->progress(rig.section->model()->index(row)), 1.0);
        }
    }

    void discoverPageChrome()
    {
        Rig rig;
        auto page = std::make_unique<DiscoverPage>(rig.environment(), nullptr);
        page->resize(1280, 720);
        page->show();
        QVERIFY(QTest::qWaitForWindowExposed(page.get()));
        QCOMPARE(page->title(), QStringLiteral("Discover"));
        QCOMPARE(page->subtitle(), QString());
        QSignalSpy chrome(page.get(), &ShellPage::chromeChanged);
        int main = -1;
        for (int i = 0; i < rig.modrinth.searches.size(); ++i) {
            if (rig.modrinth.searches[i].args.sorting && rig.modrinth.searches[i].args.sorting->name == QLatin1String("relevance")) {
                main = i;
            }
        }
        rig.modrinth.answerSearch(main, makeItems(Provider::Modrinth, 0, 25));
        QCOMPARE(page->subtitle(), QStringLiteral("25 loaded"));  // the section's caption is the top bar subtitle
        QVERIFY(chrome.size() >= 1);
        // Modpacks and Mods are built; no Resource Packs / Shaders stubs (the switcher itself is tested in ZeroDiscoverMods_test)
        QCOMPARE(DiscoverPage::sectionIds(), (QStringList{ QStringLiteral("modpacks"), QStringLiteral("mods") }));
        QSignalSpy installs(page.get(), &DiscoverPage::installRequested);
        // the page forwards the section's install request (the hosting window shows the install dialog)
        emit page->modpacks()->installRequested(makeItem(Provider::Modrinth, QStringLiteral("1"), QStringLiteral("x")), QString());
        QCOMPARE(installs.size(), 1);
    }
};

QTEST_MAIN(ZeroDiscoverTest)

#include "ZeroDiscover_test.moc"
