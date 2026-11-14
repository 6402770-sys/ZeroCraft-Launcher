// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Discover widget kit (launcher/ui/zero/content): RemoteImageCache (injected fetcher),
// ContentToolbar, FilterChipStrip, FilterPanel, ContentReveal (injected clock), the ZeroSkeleton shapes and the empty states.
#include <QAbstractButton>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <memory>

#include "ui/zero/ZeroSkeleton.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/ContentReveal.h"
#include "ui/zero/content/ContentStates.h"
#include "ui/zero/content/ContentToolbar.h"
#include "ui/zero/content/FilterChipStrip.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "zero/content/ContentFilter.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

void setMotion(bool on)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    spec.reduceMotion = !on;
    Theme::setCurrent(spec);
}

QByteArray pngBytes(int w, int h, const QColor& c = Qt::red)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return bytes;
}

QString url(int i)
{
    return QStringLiteral("https://cdn.example.org/img/%1.png").arg(i);
}

/// A fetcher that records the requests and answers when the test says so.
struct FakeNet {
    QStringList requested;
    QHash<QString, std::function<void(const RemoteImageCache::FetchResult&)>> pending;

    RemoteImageCache::Fetcher fetcher()
    {
        return [this](const QString& u, std::function<void(const RemoteImageCache::FetchResult&)> done) {
            requested << u;
            pending.insert(u, std::move(done));
        };
    }
    void answer(const QString& u, const QByteArray& data)
    {
        auto done = pending.take(u);
        if (!done)
            QFAIL(qPrintable("no pending fetch of " + u));
        done({ true, {}, data });
    }
    void fail(const QString& u)
    {
        auto done = pending.take(u);
        if (!done)
            QFAIL(qPrintable("no pending fetch of " + u));
        done({});
    }
};

FacetContext facetCtx(Provider p, bool hasTarget = true, QStringList versions = {})
{
    FacetContext c;
    c.provider = p;
    c.hasTarget = hasTarget;
    c.mcVersions = std::move(versions);
    c.categories = { { .name = "Adventure", .id = "adventure", .header = "categories" },
                     { .name = "Magic", .id = "magic", .header = "categories" } };
    return c;
}

FilterState modrinthDefaults()
{
    FilterState d;
    d.selected[FacetId::provider] = { providerId(Provider::Modrinth) };
    return d;
}

QAbstractButton* pill(FilterPanel& panel, const QString& facetId, const QString& value)
{
    for (auto* p : panel.pillsOf(facetId))
        if (p->property("zeroOptionValue").toString() == value)
            return p;
    return nullptr;
}

QStringList visibleItems(QListWidget* list)
{
    QStringList out;
    for (int i = 0; i < list->count(); ++i)
        if (!list->item(i)->isHidden())
            out << list->item(i)->text();
    return out;
}

/// Counts what the view's delegate is asked to paint.
class CountingDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override
    {
        ++painted;
        QStyledItemDelegate::paint(p, o, i);
    }
    mutable int painted = 0;
};

}  // namespace

class ZeroContentKitTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase() { setMotion(false); }

    // ---- RemoteImageCache --------------------------------------------------------------------------------------------

    void buckets()
    {
        QCOMPARE(RemoteImageCache::bucketFor(-5), 64);
        QCOMPARE(RemoteImageCache::bucketFor(64), 64);
        QCOMPARE(RemoteImageCache::bucketFor(65), 128);
        QCOMPARE(RemoteImageCache::bucketFor(129), 400);
        QCOMPARE(RemoteImageCache::bucketFor(401), 1280);
        QCOMPARE(RemoteImageCache::bucketFor(9000), 1280);
        QCOMPARE(RemoteImageCache::bucketFor(100.0, 2.0), 400);  // 200 device px
    }

    void imageCacheDedupesFetchesPerUrl()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        QSignalSpy ready(&cache, &RemoteImageCache::imageReady);
        QObject a, b;
        QVERIFY(cache.request(url(1), 128, &a).isNull());
        QVERIFY(cache.request(url(1), 128, &b).isNull());
        QVERIFY(cache.request(url(1), 400, &a).isNull());  // another size of the same image: still one fetch
        QCOMPARE(net.requested.size(), 1);
        QCOMPARE(cache.fetchCount(), 1);
        net.answer(url(1), pngBytes(900, 450));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);  // one per requested size
        QCOMPARE(cache.fetchCount(), 1);
        QCOMPARE(cache.decodeCount(), 2);
        QVERIFY(cache.isCached(url(1), 128));
        QVERIFY(cache.isCached(url(1), 400));
        QVERIFY(!cache.request(url(1), 128, &b).isNull());  // from memory now
        QCOMPARE(cache.fetchCount(), 1);
    }

    void imageCacheLimitsFetchesInFlight()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        for (int i = 0; i < 10; ++i)
            cache.request(url(i), 64);
        QCOMPARE(cache.inFlightCount(), RemoteImageCache::kMaxInFlight);
        QCOMPARE(cache.queuedCount(), 4);
        QCOMPARE(net.requested.size(), 6);
        net.answer(url(0), pngBytes(10, 10));
        QTRY_COMPARE_WITH_TIMEOUT(net.requested.size(), 7, 5000);
        QCOMPARE(cache.inFlightCount(), RemoteImageCache::kMaxInFlight);  // never more than 6
        QCOMPARE(net.requested.at(6), url(9));                            // the newest request first
    }

    void imageCacheServesVisibleBeforeBackground()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        for (int i = 0; i < 6; ++i)
            cache.request(url(i), 64);
        cache.request(url(20), 64, nullptr, RemoteImageCache::Priority::Visible);
        cache.request(url(30), 64, nullptr, RemoteImageCache::Priority::Background);
        cache.request(url(31), 64, nullptr, RemoteImageCache::Priority::Background);  // newer, but only a prefetch
        net.answer(url(0), pngBytes(10, 10));
        QTRY_COMPARE_WITH_TIMEOUT(net.requested.size(), 7, 5000);
        QCOMPARE(net.requested.at(6), url(20));
        net.answer(url(1), pngBytes(10, 10));
        QTRY_COMPARE_WITH_TIMEOUT(net.requested.size(), 8, 5000);
        QCOMPARE(net.requested.at(7), url(31));
    }

    void imageCacheCancelsByRequester()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        for (int i = 0; i < 6; ++i)
            cache.request(url(i), 64);  // all slots busy
        auto* gone = new QObject;
        QObject kept;
        QObject other;
        cache.request(url(10), 64, &other);  // only `other` wants it
        cache.request(url(11), 64, &other);  // `other` and `kept` want it
        cache.request(url(11), 64, &kept);
        cache.request(url(12), 64, gone);  // its requester is deleted before the slot frees
        QCOMPARE(cache.queuedCount(), 3);
        cache.cancel(&other);
        QCOMPARE(cache.queuedCount(), 2);  // url(10) left the queue, url(11) stays for `kept`
        delete gone;                       // never touched, the QPointer just reads null
        cache.cancel(&other);              // cancelling twice is harmless
        net.answer(url(0), pngBytes(10, 10));
        net.answer(url(1), pngBytes(10, 10));
        QTRY_VERIFY_WITH_TIMEOUT(net.requested.contains(url(11)), 5000);
        QCoreApplication::processEvents();
        QVERIFY(!net.requested.contains(url(10)));
        QVERIFY(!net.requested.contains(url(12)));  // a deleted requester never causes a fetch
    }

    void imageCacheRemembersFailuresUntilRetry()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        QSignalSpy failed(&cache, &RemoteImageCache::imageFailed);
        QSignalSpy ready(&cache, &RemoteImageCache::imageReady);
        QObject requester;
        cache.request(url(1), 64, &requester);
        net.fail(url(1));
        QCOMPARE(failed.size(), 1);
        QVERIFY(cache.hasFailed(url(1)));
        QVERIFY(cache.request(url(1), 64, &requester).isNull());  // remembered: no new fetch
        QCOMPARE(cache.fetchCount(), 1);

        cache.retryFailed();  // "Refresh": the requester that was waiting is asked again
        QVERIFY(!cache.hasFailed(url(1)));
        QCOMPARE(cache.fetchCount(), 2);
        net.answer(url(1), pngBytes(40, 40));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
    }

    void imageCacheRejectsNonHttpUrls()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        QVERIFY(cache.request(QStringLiteral("file:///etc/passwd"), 64).isNull());
        QVERIFY(cache.request(QStringLiteral("not a url"), 64).isNull());
        QVERIFY(cache.request(QString(), 64).isNull());
        QCOMPARE(net.requested.size(), 0);
    }

    void imageCacheScalesToTheBucketOffTheGuiThread()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        QSignalSpy ready(&cache, &RemoteImageCache::imageReady);
        cache.request(url(1), 128);
        cache.request(url(2), 400);
        net.answer(url(1), pngBytes(1000, 500));
        net.answer(url(2), pngBytes(50, 50));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
        const QPixmap big = cache.peek(url(1), 128);
        QCOMPARE(big.size(), QSize(128, 64));  // scaled to the bucket, aspect kept
        QCOMPARE(cache.peek(url(2), 400).size(), QSize(50, 50));  // never enlarged
        QVERIFY(cache.lastDecodeThread());
        QVERIFY(cache.lastDecodeThread() != QThread::currentThread());
        // Until the requested size exists the nearest decoded one is returned.
        QCOMPARE(cache.peek(url(1), 400).width(), 128);
    }

    void imageCacheLoadsLocalFilesThroughTheSamePool()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("icon.png"));
        QVERIFY(QImage(300, 300, QImage::Format_RGB32).save(path));

        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        QSignalSpy ready(&cache, &RemoteImageCache::imageReady);
        QSignalSpy failed(&cache, &RemoteImageCache::imageFailed);
        QVERIFY(cache.requestLocal(path, 64).isNull());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(ready.first().at(0).toString(), path);
        QCOMPARE(cache.peek(path, 64).width(), 64);
        QCOMPARE(net.requested.size(), 0);  // no network involved
        QCOMPARE(cache.fetchCount(), 0);

        cache.requestLocal(dir.filePath(QStringLiteral("missing.png")), 64);
        QCOMPARE(failed.size(), 1);
    }

    void imageCacheEvictsLeastRecentlyUsed()
    {
        FakeNet net;
        RemoteImageCache cache(net.fetcher());
        cache.setMaxCostBytes(2 * 100 * 100 * 4 + 16);  // room for two 100 x 100 pictures
        QSignalSpy ready(&cache, &RemoteImageCache::imageReady);
        for (int i = 0; i < 3; ++i) {
            cache.request(url(i), 128);
            net.answer(url(i), pngBytes(100, 100, QColor::fromHsv(i * 80, 255, 255)));
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(), i + 1, 5000);
        }
        QVERIFY(cache.cachedCost() <= 2 * 100 * 100 * 4 + 16);
        QVERIFY(!cache.isCached(url(0), 128));  // the oldest went
        QVERIFY(cache.isCached(url(2), 128));
    }

    // ---- ContentToolbar ---------------------------------------------------------------------------------------------

    void toolbarDebouncesSearchAndAppliesEnterAndEscAtOnce()
    {
        ContentToolbar bar;
        bar.resize(900, 40);
        bar.show();
        QSignalSpy search(&bar, &ContentToolbar::searchChanged);
        bar.searchField()->setText(QStringLiteral("  sodium "));
        QCOMPARE(search.size(), 0);  // debounced
        QTest::qWait(150);
        QCOMPARE(search.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(search.size(), 1, 2000);  // ~300 ms
        QCOMPARE(search.last().at(0).toString(), QStringLiteral("sodium"));
        QCOMPARE(bar.searchText(), QStringLiteral("sodium"));

        bar.searchField()->setText(QStringLiteral("iris"));
        QTest::keyClick(bar.searchField(), Qt::Key_Return);  // Enter applies now
        QCOMPARE(search.size(), 2);
        QCOMPARE(search.last().at(0).toString(), QStringLiteral("iris"));

        QTest::keyClick(bar.searchField(), Qt::Key_Escape);  // Esc clears and applies now
        QVERIFY(bar.searchField()->text().isEmpty());
        QCOMPARE(search.size(), 3);
        QCOMPARE(search.last().at(0).toString(), QString());

        bar.setSearchText(QStringLiteral("quiet"));  // programmatic: no signal, no pending one
        QTest::qWait(400);
        QCOMPARE(search.size(), 3);
    }

    void toolbarShowsCountPillCaptionSortAndViewToggle()
    {
        ContentToolbar bar;
        bar.resize(900, 40);
        bar.show();
        QCOMPARE(bar.height(), 40);
        QVERIFY(bar.searchField()->minimumWidth() == 240 && bar.searchField()->maximumWidth() == 420);
        QCOMPARE(bar.filtersButton()->text(), QStringLiteral("Filters"));
        QVERIFY(!bar.filtersButton()->property("zeroHasBadge").toBool());
        bar.setFilterCount(3);
        QVERIFY(bar.filtersButton()->property("zeroHasBadge").toBool());
        QVERIFY(bar.filtersButton()->accessibleName().contains(QLatin1Char('3')));
        QVERIFY(!bar.filtersButton()->grab().isNull());  // paints the pill
        bar.setFilterCount(0);
        QVERIFY(!bar.filtersButton()->property("zeroHasBadge").toBool());

        QVERIFY(bar.captionLabel()->isHidden());
        bar.setResultsCaption(QStringLiteral("48 of 312 loaded"));
        QVERIFY(!bar.captionLabel()->isHidden());
        QCOMPARE(bar.resultsCaption(), QStringLiteral("48 of 312 loaded"));

        QVERIFY(bar.sortCombo()->isHidden());  // no options (Technic): no combo
        bar.setSortOptions({ { QStringLiteral("relevance"), QStringLiteral("Relevance") }, { QStringLiteral("downloads"), QStringLiteral("Downloads") } },
                           QStringLiteral("downloads"));
        QVERIFY(!bar.sortCombo()->isHidden());
        QCOMPARE(bar.sortId(), QStringLiteral("downloads"));
        QSignalSpy sort(&bar, &ContentToolbar::sortChanged);
        bar.sortCombo()->setCurrentIndex(0);
        emit bar.sortCombo()->activated(0);
        QCOMPARE(sort.size(), 1);
        QCOMPARE(sort.first().at(0).toString(), QStringLiteral("relevance"));

        QVERIFY(bar.gridButton()->parentWidget()->isHidden());  // only where both views exist
        bar.setViewModesAvailable(true);
        QVERIFY(!bar.gridButton()->parentWidget()->isHidden());
        QSignalSpy mode(&bar, &ContentToolbar::viewModeChanged);
        bar.listButton()->click();
        QCOMPARE(mode.size(), 1);
        QCOMPARE(bar.viewMode(), ContentToolbar::ViewMode::List);

        QSignalSpy filters(&bar, &ContentToolbar::filtersRequested);
        bar.filtersButton()->click();
        QCOMPARE(filters.size(), 1);

        bar.focusSearch();  // selects the field's text; focus needs an active window, which offscreen does not promise
    }

    // ---- FilterChipStrip --------------------------------------------------------------------------------------------

    void chipFlowWrapsToTwoLinesThenScrolls()
    {
        using S = FilterChipStrip;
        auto f = S::flow({ 100, 100, 100 }, 400);
        QCOMPARE(f.lines, 1);
        QCOMPARE(f.contentWidth, 400);
        f = S::flow(QList<int>(8, 100), 300);  // would need 4 lines at 300 px
        QCOMPARE(f.lines, 2);
        QVERIFY(f.contentWidth > 300);  // so the canvas grows: the strip scrolls horizontally
        QVERIFY(f.contentWidth < 8 * 100);
        QCOMPARE(f.positions.size(), 8);
        QVERIFY(S::flow(QList<int>(5, 100), 320).lines == 2 && S::flow(QList<int>(5, 100), 320).contentWidth == 320);
        QCOMPARE(S::flow({}, 300).lines, 0);
    }

    void chipStripShowsChipsAndRemovesExactlyOne()
    {
        const auto facets = facetsFor(Kind::Mod, facetCtx(Provider::Modrinth));
        FilterChipStrip strip;
        strip.resize(700, 40);
        strip.show();
        strip.setFacets(facets);
        strip.setDefaults(modrinthDefaults());
        QVERIFY(strip.isHidden());  // nothing selected: hidden

        FilterState state = modrinthDefaults();
        state.selected[FacetId::loader] = { QStringLiteral("fabric"), QStringLiteral("quilt") };
        state.selected[FacetId::mcVersion] = { QStringLiteral("1.20.1") };
        state.query = QStringLiteral("sodium");
        strip.setState(state);
        QVERIFY(!strip.isHidden());
        QCOMPARE(strip.lineCount(), 1);
        const QList<Chip> expected = chipsFor(state, facets, modrinthDefaults());
        QCOMPARE(strip.chipLabels().size(), expected.size());
        QVERIFY(strip.chipLabels().contains(QStringLiteral("Loader: Fabric")));
        QVERIFY(strip.chipLabels().contains(QStringLiteral("Loader: Quilt")));
        QVERIFY(strip.chipLabels().contains(QStringLiteral("\"sodium\"")));
        QCOMPARE(strip.height(), FilterChipStrip::kChipHeight);
        QVERIFY(!strip.clearAllButton()->isHidden());

        QSignalSpy changed(&strip, &FilterChipStrip::stateChanged);
        QAbstractButton* quilt = nullptr;
        for (auto* b : strip.chipButtons())
            if (b->text() == QStringLiteral("Loader: Quilt"))
                quilt = b;
        QVERIFY(quilt);
        quilt->click();
        QCOMPARE(changed.size(), 1);
        const auto next = qvariant_cast<FilterState>(changed.last().at(0));
        QCOMPARE(next.values(FacetId::loader), QStringList{ QStringLiteral("fabric") });  // exactly that value went
        QCOMPARE(next.values(FacetId::mcVersion), QStringList{ QStringLiteral("1.20.1") });
        QCOMPARE(next.query, QStringLiteral("sodium"));
        QCOMPARE(strip.chipLabels().size(), expected.size() - 1);
        QVERIFY(!strip.chipLabels().contains(QStringLiteral("Loader: Quilt")));

        strip.clearAllButton()->click();  // back to the defaults
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)), clearAll(next, modrinthDefaults()));
        QVERIFY(strip.isHidden());  // the defaults (just the provider) draw no chip
    }

    void chipStripOffersShowEverythingWhenDefaultsExist()
    {
        const auto facets = facetsFor(Kind::Mod, facetCtx(Provider::Modrinth));
        FilterState defaults = modrinthDefaults();
        defaults.selected[FacetId::mcVersion] = { QStringLiteral("1.20.1") };
        defaults.selected[FacetId::loader] = { QStringLiteral("fabric") };

        FilterChipStrip strip;
        strip.resize(700, 40);
        strip.show();
        strip.setFacets(facets);
        strip.setDefaults(defaults);
        strip.setState(defaults);
        QCOMPARE(strip.chipLabels().size(), 2);
        for (const Chip& c : strip.chips())
            QVERIFY(c.isDefault);  // drawn as defaults
        QVERIFY(!strip.showEverythingButton()->isHidden());

        QSignalSpy changed(&strip, &FilterChipStrip::stateChanged);
        strip.showEverythingButton()->click();
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)), showEverything(defaults));
        QVERIFY(strip.isHidden());

        // Without defaults beyond the provider there is nothing to "show everything" of.
        FilterChipStrip plain;
        plain.resize(700, 40);
        plain.show();
        plain.setFacets(facets);
        plain.setDefaults(modrinthDefaults());
        FilterState s = modrinthDefaults();
        s.selected[FacetId::loader] = { QStringLiteral("forge") };
        plain.setState(s);
        QVERIFY(plain.showEverythingButton()->isHidden());
        QVERIFY(!plain.clearAllButton()->isHidden());
    }

    void chipStripWrapsAndScrollsInANarrowStrip()
    {
        const auto facets = facetsFor(Kind::Mod, facetCtx(Provider::Modrinth, true, { QStringLiteral("1.21.1"), QStringLiteral("1.20.1") }));
        FilterChipStrip strip;
        strip.resize(260, 40);
        strip.show();
        strip.setFacets(facets);
        strip.setDefaults(modrinthDefaults());
        FilterState s = modrinthDefaults();
        s.selected[FacetId::loader] = { QStringLiteral("fabric"), QStringLiteral("forge"), QStringLiteral("neoforge"), QStringLiteral("quilt") };
        s.selected[FacetId::mcVersion] = { QStringLiteral("1.21.1"), QStringLiteral("1.20.1") };
        strip.setState(s);
        QCOMPARE(strip.lineCount(), 2);  // never more than two lines
        QVERIFY(strip.scrolls());        // the rest scrolls sideways
        QVERIFY(strip.height() >= 2 * FilterChipStrip::kChipHeight);
    }

    void chipStripAnimatesWithMotionOn()
    {
        setMotion(true);
        const auto facets = facetsFor(Kind::Mod, facetCtx(Provider::Modrinth));
        FilterChipStrip strip;
        strip.resize(700, 40);
        strip.show();
        strip.setFacets(facets);
        strip.setDefaults(modrinthDefaults());
        FilterState s = modrinthDefaults();
        s.selected[FacetId::loader] = { QStringLiteral("forge") };
        strip.setState(s);
        QCOMPARE(strip.chipLabels().size(), 1);
        strip.setState(modrinthDefaults());
        QVERIFY(strip.chipLabels().isEmpty());     // the chip is leaving ...
        QVERIFY(!strip.isHidden());                // ... so the strip stays for the 140 ms exit
        QTRY_VERIFY_WITH_TIMEOUT(strip.isHidden(), 2000);
        setMotion(false);
    }

    // ---- FilterPanel ------------------------------------------------------------------------------------------------

    void panelBuildsGroupsAndAppliesLiveDebounced()
    {
        QPushButton anchor(QStringLiteral("Filters"));
        anchor.resize(100, 40);
        anchor.show();
        FilterPanel panel;
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::Modrinth)));
        panel.setDefaults(modrinthDefaults());
        panel.setState(modrinthDefaults());
        QSignalSpy changed(&panel, &FilterPanel::stateChanged);

        QVERIFY(panel.segmentedOf(FacetId::provider));
        QVERIFY(panel.segmentedOf(FacetId::environment));
        QCOMPARE(panel.pillsOf(FacetId::loader).size(), 4);
        QVERIFY(panel.switchOf(FacetId::installed));
        QVERIFY(panel.listOf(FacetId::mcVersion));

        QVERIFY(pill(panel, FacetId::loader, QStringLiteral("forge"))->isCheckable());
        pill(panel, FacetId::loader, QStringLiteral("forge"))->click();
        pill(panel, FacetId::loader, QStringLiteral("fabric"))->click();
        QCOMPARE(changed.size(), 0);  // live, but debounced: a few clicks make one change
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 2000);
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)).values(FacetId::loader),
                 (QStringList{ QStringLiteral("forge"), QStringLiteral("fabric") }));  // Multi: OR-ed values, in click order
        QVERIFY(pill(panel, FacetId::loader, QStringLiteral("forge"))->isChecked());

        pill(panel, FacetId::loader, QStringLiteral("forge"))->click();  // click again removes it
        panel.flush();
        QCOMPARE(changed.size(), 2);
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)).values(FacetId::loader), QStringList{ QStringLiteral("fabric") });

        panel.switchOf(FacetId::installed)->click();  // Toggle facet
        panel.flush();
        QVERIFY(qvariant_cast<FilterState>(changed.last().at(0)).isSelected(FacetId::installed, kToggleOn));
        QVERIFY(panel.switchOf(FacetId::installed)->isChecked());
        panel.switchOf(FacetId::installed)->click();
        panel.flush();
        QVERIFY(qvariant_cast<FilterState>(changed.last().at(0)).values(FacetId::installed).isEmpty());

        // setState() shows without emitting.
        const int before = int(changed.size());
        FilterState external = modrinthDefaults();
        external.selected[FacetId::loader] = { QStringLiteral("quilt") };
        panel.setState(external);
        QVERIFY(pill(panel, FacetId::loader, QStringLiteral("quilt"))->isChecked());
        QVERIFY(!pill(panel, FacetId::loader, QStringLiteral("fabric"))->isChecked());
        QTest::qWait(400);
        QCOMPARE(changed.size(), before);
    }

    void panelSegmentedRowsFollowClicksAndDefaults()
    {
        QPushButton anchor(QStringLiteral("Filters"));
        anchor.resize(100, 40);
        anchor.show();
        FilterPanel panel;
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::Modrinth)));
        panel.setDefaults(modrinthDefaults());
        panel.setState(modrinthDefaults());
        QSignalSpy changed(&panel, &FilterPanel::stateChanged);
        panel.popup(&anchor);
        QVERIFY(panel.isOpen());
        QCOMPARE(panel.panel()->width(), FilterPanel::kWidth);

        auto* provider = panel.segmentedOf(FacetId::provider);
        QCOMPARE(provider->selected(), providerId(Provider::Modrinth));  // the default shows as chosen
        QCOMPARE(provider->count(), 2);                                  // Modrinth, CurseForge (no Technic for mods)
        QTest::mouseClick(provider, Qt::LeftButton, {}, provider->segmentRect(1).center());
        panel.flush();
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)).values(FacetId::provider), QStringList{ providerId(Provider::CurseForge) });
        QTest::mouseClick(provider, Qt::LeftButton, {}, provider->segmentRect(1).center());  // the facet has a default: it cannot be emptied
        panel.flush();
        QCOMPARE(panel.state().values(FacetId::provider), QStringList{ providerId(Provider::CurseForge) });
        QCOMPARE(provider->selected(), providerId(Provider::CurseForge));

        // A Single facet without a default can be emptied by clicking its chosen segment again.
        panel.setState(modrinthDefaults());
        auto* env = panel.segmentedOf(FacetId::environment);
        QVERIFY(env->selected().isEmpty());
        QTest::mouseClick(env, Qt::LeftButton, {}, env->segmentRect(0).center());
        QCOMPARE(panel.state().values(FacetId::environment).size(), 1);
        QTest::mouseClick(env, Qt::LeftButton, {}, env->segmentRect(0).center());
        QVERIFY(panel.state().values(FacetId::environment).isEmpty());
        panel.close();
    }

    void panelResetGoesBackToTheDefaultsAtOnce()
    {
        FilterPanel panel;
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::Modrinth)));
        FilterState defaults = modrinthDefaults();
        defaults.selected[FacetId::loader] = { QStringLiteral("fabric") };
        panel.setDefaults(defaults);
        FilterState state = defaults;
        state.selected[FacetId::loader] = { QStringLiteral("forge") };
        state.selected[FacetId::installed] = { kToggleOn };
        state.sortId = QStringLiteral("downloads");
        state.query = QStringLiteral("keep me out");
        panel.setState(state);
        QSignalSpy changed(&panel, &FilterPanel::stateChanged);
        panel.resetButton()->click();
        QCOMPARE(changed.size(), 1);  // no debounce for an explicit Reset
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)), clearAll(state, defaults));
        QCOMPARE(panel.state().sortId, QStringLiteral("downloads"));  // Reset keeps the sort
        QVERIFY(pill(panel, FacetId::loader, QStringLiteral("fabric"))->isChecked());
        QVERIFY(!pill(panel, FacetId::loader, QStringLiteral("forge"))->isChecked());
    }

    void panelShowsDisabledFacetsGreyedWithTheReason()
    {
        FilterPanel panel;
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::CurseForge, false)));
        QWidget* env = panel.groupOf(FacetId::environment);
        QVERIFY(env);
        QVERIFY(!env->isEnabled());
        QCOMPARE(env->toolTip(), QStringLiteral("CurseForge has no environment data"));
        QVERIFY(panel.reasonLabelOf(FacetId::environment));
        QCOMPARE(panel.reasonLabelOf(FacetId::environment)->text(), QStringLiteral("CurseForge has no environment data"));
        QVERIFY(panel.groupOf(FacetId::loader)->isEnabled());  // others stay usable
        QVERIFY(!panel.reasonLabelOf(FacetId::loader));
        QVERIFY(!panel.switchOf(FacetId::installed)->isEnabled());  // no target instance
        QCOMPARE(panel.switchOf(FacetId::installed)->toolTip(), QStringLiteral("Choose a target instance first"));
    }

    void panelVersionListSearchesAndHidesSnapshots()
    {
        FilterPanel panel;
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::Modrinth, true,
                                                       { QStringLiteral("1.21.1"), QStringLiteral("24w40a"), QStringLiteral("1.20.1"),
                                                         QStringLiteral("1.20.1-pre1"), QStringLiteral("1.20") })));
        panel.setDefaults(modrinthDefaults());
        panel.setState(modrinthDefaults());
        QSignalSpy releases(&panel, &FilterPanel::releasesOnlyChanged);
        QListWidget* list = panel.listOf(FacetId::mcVersion);
        QVERIFY(list);
        QVERIFY(panel.releasesOnlySwitch()->isChecked());
        QCOMPARE(visibleItems(list), (QStringList{ QStringLiteral("1.21.1"), QStringLiteral("1.20.1"), QStringLiteral("1.20") }));

        panel.listFilterOf(FacetId::mcVersion)->setText(QStringLiteral("1.20"));
        QCOMPARE(visibleItems(list), (QStringList{ QStringLiteral("1.20.1"), QStringLiteral("1.20") }));
        panel.listFilterOf(FacetId::mcVersion)->clear();

        panel.releasesOnlySwitch()->click();  // off: snapshots appear
        QCOMPARE(releases.size(), 1);
        QVERIFY(!panel.releasesOnly());
        QCOMPARE(visibleItems(list).size(), 5);
        panel.releasesOnlySwitch()->click();
        QCOMPARE(visibleItems(list).size(), 3);

        // A selected snapshot stays listed even while releases only is on.
        FilterState s = modrinthDefaults();
        s.selected[FacetId::mcVersion] = { QStringLiteral("24w40a") };
        panel.setState(s);
        QVERIFY(visibleItems(list).contains(QStringLiteral("24w40a")));
        QCOMPARE(list->item(1)->checkState(), Qt::Checked);

        // Clicking a row toggles its version.
        QSignalSpy changed(&panel, &FilterPanel::stateChanged);
        emit list->itemClicked(list->item(0));
        panel.flush();
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)).values(FacetId::mcVersion),
                 (QStringList{ QStringLiteral("24w40a"), QStringLiteral("1.21.1") }));
        QVERIFY(FilterPanel::isReleaseVersion(QStringLiteral("1.7.10")));
        QVERIFY(!FilterPanel::isReleaseVersion(QStringLiteral("b1.7.3")));
    }

    void panelPopsUpWithinSeventyPercentAndClosesOnEscAndDone()
    {
        QWidget window;
        window.resize(900, 500);
        auto* anchor = new QPushButton(QStringLiteral("Filters"), &window);
        anchor->move(20, 20);
        window.show();
        FilterPanel panel;
        QStringList versions;
        for (int i = 0; i < 40; ++i)
            versions << QStringLiteral("1.%1.1").arg(i);
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::Modrinth, true, versions)));
        panel.setDefaults(modrinthDefaults());
        panel.setState(modrinthDefaults());
        panel.setResultText(QStringLiteral("312 results"));
        QCOMPARE(panel.resultLabel()->text(), QStringLiteral("312 results"));

        QSignalSpy closed(&panel, &FilterPanel::closed);
        panel.toggle(anchor);
        QVERIFY(panel.isOpen());
        QCOMPARE(panel.panel()->width(), 360);
        QVERIFY(panel.panel()->height() <= int(500 * 0.7));
        QVERIFY(panel.panel()->height() > 200);

        QTest::keyClick(&panel, Qt::Key_Escape);
        QVERIFY(!panel.isOpen());
        QCOMPARE(closed.size(), 1);

        panel.toggle(anchor);
        QVERIFY(panel.isOpen());
        panel.toggle(anchor);  // the button closes it again
        QVERIFY(!panel.isOpen());
        panel.popup(anchor);
        panel.doneButton()->click();
        QVERIFY(!panel.isOpen());
    }

    void panelFlushesAPendingChangeWhenItCloses()
    {
        QPushButton anchor(QStringLiteral("Filters"));
        anchor.show();
        FilterPanel panel;
        panel.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::Modrinth)));
        panel.setDefaults(modrinthDefaults());
        panel.setState(modrinthDefaults());
        QSignalSpy changed(&panel, &FilterPanel::stateChanged);
        panel.popup(&anchor);
        pill(panel, FacetId::loader, QStringLiteral("quilt"))->click();
        QCOMPARE(changed.size(), 0);
        panel.close();  // Esc / Done / click outside: nothing is lost
        QCOMPARE(changed.size(), 1);
        QCOMPARE(qvariant_cast<FilterState>(changed.last().at(0)).values(FacetId::loader), QStringList{ QStringLiteral("quilt") });
    }

    // ---- ContentReveal ----------------------------------------------------------------------------------------------

    void revealAnimatesOnlyInsertedRowsInRowOrder()
    {
        setMotion(true);
        QStandardItemModel model;
        QListView view;
        view.setModel(&model);
        view.resize(400, 800);
        view.show();
        auto* reveal = installContentReveal(&view, RevealSpec::modpacks());
        qint64 clock = 1000;
        reveal->setClock([&clock]() { return clock; });

        for (int i = 0; i < 5; ++i)
            model.appendRow(new QStandardItem(QString::number(i)));  // one frame: one batch
        QCOMPARE(reveal->trackedCount(), 5);
        QVERIFY(reveal->timerActive());
        for (int i = 0; i < 5; ++i)
            QCOMPARE(reveal->startDelay(model.index(i, 0)), i * 32);  // stagger by row order
        QCOMPARE(reveal->progress(model.index(0, 0)), 0.0);            // not started: hidden
        clock = 1000 + 160;
        const qreal mid = reveal->progress(model.index(0, 0));
        QVERIFY(mid > 0.0 && mid < 1.0);
        QCOMPARE(reveal->progress(model.index(4, 0)), reveal->progress(model.index(4, 0)));  // a pure function of the clock

        // dataChanged (a picture arrived, installed state) never starts anything.
        reveal->advance();
        clock = 1000 + 2000;
        reveal->advance();
        QCOMPARE(reveal->trackedCount(), 0);
        QVERIFY(!reveal->isAnimating());
        QVERIFY(!reveal->timerActive());  // the timer stops when idle
        model.item(2)->setText(QStringLiteral("changed"));
        model.item(3)->setData(QVariant(7), Qt::UserRole + 5);
        QCOMPARE(reveal->trackedCount(), 0);
        QCOMPARE(reveal->progress(model.index(2, 0)), 1.0);

        // Rows appended later (next page) enter, the old ones stay put.
        model.appendRow(new QStandardItem(QStringLiteral("5")));
        QCOMPARE(reveal->trackedCount(), 1);
        QCOMPARE(reveal->progress(model.index(0, 0)), 1.0);
        reveal->finish();
        setMotion(false);
    }

    void revealCapsTheStaggerAtFourteenSteps()
    {
        setMotion(true);
        QStandardItemModel model;
        QListView view;
        view.setModel(&model);
        view.resize(400, 4000);
        view.show();
        auto* reveal = installContentReveal(&view, RevealSpec::modpacks());
        qint64 clock = 0;
        reveal->setClock([&clock]() { return clock; });
        for (int i = 0; i < 30; ++i)
            model.appendRow(new QStandardItem(QString::number(i)));
        QCOMPARE(reveal->startDelay(model.index(0, 0)), 0);
        QCOMPARE(reveal->startDelay(model.index(13, 0)), 13 * 32);  // the 14th row
        QCOMPARE(reveal->startDelay(model.index(14, 0)), 13 * 32);  // later rows start with it
        QCOMPARE(reveal->startDelay(model.index(29, 0)), 13 * 32);
        reveal->finish();
        setMotion(false);
    }

    void revealBatchesPerFrame()
    {
        setMotion(true);
        QStandardItemModel model;
        QListView view;
        view.setModel(&model);
        view.resize(400, 800);
        view.show();
        auto* reveal = installContentReveal(&view, RevealSpec::mods());
        QCOMPARE(reveal->spec().offsetPx, 16);
        QCOMPARE(reveal->spec().durationMs, 260);
        QCOMPARE(reveal->spec().staggerMs, 24);
        qint64 clock = 500;
        reveal->setClock([&clock]() { return clock; });
        model.appendRow(new QStandardItem(QStringLiteral("a")));
        model.appendRow(new QStandardItem(QStringLiteral("b")));
        QCOMPARE(reveal->startDelay(model.index(1, 0)), 24);  // same frame: continues the stagger
        clock = 516;
        reveal->advance();  // the frame passes
        model.appendRow(new QStandardItem(QStringLiteral("c")));
        QCOMPARE(reveal->startDelay(model.index(2, 0)), 0);  // a new batch starts its own stagger
        reveal->finish();
        setMotion(false);
    }

    void revealRestartsOnModelReset()
    {
        setMotion(true);
        QStringListModel model;
        model.setStringList({ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
        QListView view;
        view.setModel(&model);
        view.resize(400, 800);
        view.show();
        auto* reveal = installContentReveal(&view, RevealSpec::gallery());
        QCOMPARE(reveal->spec().offsetPx, 20);
        qint64 clock = 0;
        reveal->setClock([&clock]() { return clock; });

        QStringList first;
        for (int i = 0; i < 6; ++i)
            first << QString::number(i);
        const quint64 gen0 = reveal->generation();
        model.setStringList(first);  // a reset with rows
        QVERIFY(reveal->generation() > gen0);
        QVERIFY(reveal->isAnimating());
        QCOMPARE(reveal->progress(model.index(0, 0)), 0.0);  // hidden until the first frame decides
        reveal->advance();
        QCOMPARE(reveal->trackedCount(), 6);
        QCOMPARE(reveal->startDelay(model.index(3, 0)), 3 * 28);

        // A new search while it runs replaces the entrance: the old rows stop, the new set enters.
        clock = 100;
        const quint64 gen1 = reveal->generation();
        model.setStringList({ QStringLiteral("x"), QStringLiteral("y") });
        QVERIFY(reveal->generation() > gen1);
        QCOMPARE(reveal->trackedCount(), 0);
        reveal->advance();
        QCOMPARE(reveal->trackedCount(), 2);
        QCOMPARE(reveal->startDelay(model.index(1, 0)), 28);
        QCOMPARE(reveal->startDelay(model.index(1, 0)), reveal->startDelay(model.index(1, 0)));

        // beginGeneration() does the same without a reset (the proxy filtered in place).
        reveal->beginGeneration();
        QCOMPARE(reveal->trackedCount(), 0);
        QCOMPARE(reveal->progress(model.index(0, 0)), 1.0);

        // An empty result set has nothing to animate.
        model.setStringList({});
        QVERIFY(!reveal->isAnimating());
        reveal->finish();
        setMotion(false);
    }

    void revealIsInstantWhenMotionIsOffOrTheViewIsHidden()
    {
        setMotion(false);
        QStandardItemModel model;
        QListView view;
        view.setModel(&model);
        view.resize(400, 400);
        view.show();
        auto* reveal = installContentReveal(&view, RevealSpec::modpacks());
        for (int i = 0; i < 4; ++i)
            model.appendRow(new QStandardItem(QString::number(i)));
        QCOMPARE(reveal->trackedCount(), 0);
        QVERIFY(!reveal->isAnimating());
        QCOMPARE(reveal->progress(model.index(0, 0)), 1.0);

        setMotion(true);
        QStandardItemModel hiddenModel;
        QListView hidden;
        hidden.setModel(&hiddenModel);
        auto* r2 = installContentReveal(&hidden, RevealSpec::modpacks());
        hiddenModel.appendRow(new QStandardItem(QStringLiteral("a")));
        QCOMPARE(r2->trackedCount(), 0);  // nobody sees it: no animation to run
        setMotion(false);
    }

    void revealPaintsThroughTheDelegateWrapper()
    {
        setMotion(true);
        QStandardItemModel model;
        auto* inner = new CountingDelegate;
        QListView view;
        view.setModel(&model);
        view.setItemDelegate(inner);
        view.resize(300, 300);
        view.show();
        auto* reveal = installContentReveal(&view, RevealSpec::modpacks());
        qint64 clock = 0;
        reveal->setClock([&clock]() { return clock; });
        for (int i = 0; i < 3; ++i)
            model.appendRow(new QStandardItem(QString::number(i)));
        clock = 40;  // rows 0 and 1 have started (0 and 32 ms), row 2 (64 ms) has not
        inner->painted = 0;
        view.viewport()->grab();
        QCOMPARE(inner->painted, 2);  // a row that has not started is not painted at all

        // The applied transform: from the right, translucent, a little smaller.
        QImage img(100, 60, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&img);
        reveal->apply(&p, QRect(0, 0, 100, 50), model.index(0, 0));
        QVERIFY(p.opacity() > 0.15 && p.opacity() < 1.0);
        QVERIFY(p.transform().dx() > 0.0);
        QVERIFY(p.transform().m11() < 1.0 && p.transform().m11() > 0.98);
        p.end();

        reveal->finish();
        inner->painted = 0;
        view.viewport()->grab();
        QCOMPARE(inner->painted, 3);
        QVERIFY(view.itemDelegate() != inner);  // wrapped, the original is still the wrapper's inner delegate
        setMotion(false);
    }

    // ---- ZeroSkeleton -----------------------------------------------------------------------------------------------

    void skeletonShapesPaint()
    {
        const QList<ZeroSkeleton::Shape> shapes = { ZeroSkeleton::Shape::Cards, ZeroSkeleton::Shape::Gallery, ZeroSkeleton::Shape::Cinematic };
        QList<QImage> images;
        for (auto shape : shapes) {
            for (int w : { 900, 300 }) {
                ZeroSkeleton sk(shape, QStringLiteral("Loading"));
                QCOMPARE(sk.shape(), shape);
                sk.resize(w, 520);
                sk.show();
                const QImage img = sk.grab().toImage().convertToFormat(QImage::Format_ARGB32);
                int painted = 0;
                for (int y = 0; y < img.height(); y += 2)
                    for (int x = 0; x < img.width(); x += 2)
                        if (qAlpha(img.pixel(x, y)) > 0)
                            ++painted;
                QVERIFY2(painted > 300, "a skeleton shape draws blocks");
                if (w == 900)
                    images << img;
            }
        }
        QVERIFY(images[0] != images[1]);  // the shapes differ
        QVERIFY(images[1] != images[2]);
        QVERIFY(images[0] != images[2]);
        // Motion on: the sweep runs while visible and stops when hidden.
        setMotion(true);
        ZeroSkeleton sweeping(ZeroSkeleton::Shape::Cards, QStringLiteral("Loading"));
        sweeping.resize(600, 400);
        sweeping.show();
        QVERIFY(sweeping.isSweeping());
        sweeping.hide();
        QVERIFY(!sweeping.isSweeping());
        setMotion(false);
    }

    // ---- Screenshots (only with ZERO_KIT_SHOTS=<dir>) -----------------------------------------------------------------

    void screenshots()
    {
        const QString dir = qEnvironmentVariable("ZERO_KIT_SHOTS");
        if (dir.isEmpty())
            return;
        QDir().mkpath(dir);
        for (const QString& themeId : { QStringLiteral("obsidian"), QStringLiteral("minecraft") }) {
            ThemeSpec spec = themeId == QLatin1String("obsidian") ? ThemeSpec::obsidian() : ThemeSpec::minecraft();
            spec.reduceMotion = true;
            Theme::setCurrent(spec);
            qApp->setPalette(Style::palette(spec));
            qApp->setStyleSheet(Style::styleSheet(spec));
            auto grab = [&](QWidget* w, const QString& name) { w->grab().save(dir + QLatin1Char('/') + themeId + QLatin1Char('-') + name + QStringLiteral(".png")); };

            const auto facets = facetsFor(Kind::Mod, facetCtx(Provider::Modrinth, true, { QStringLiteral("1.21.1"), QStringLiteral("1.20.1"), QStringLiteral("24w40a"), QStringLiteral("1.19.4") }));
            FilterState state = modrinthDefaults();
            state.selected[FacetId::loader] = { QStringLiteral("fabric"), QStringLiteral("quilt") };
            state.selected[FacetId::mcVersion] = { QStringLiteral("1.20.1") };
            state.selected[FacetId::installed] = { kToggleOn };
            state.query = QStringLiteral("sodium");

            QWidget page;
            auto* lay = new QVBoxLayout(&page);
            auto* bar = new ContentToolbar;
            auto* strip = new FilterChipStrip;
            lay->addWidget(bar);
            lay->addWidget(strip);
            lay->addStretch(1);
            page.resize(900, 140);
            bar->setSearchPlaceholder(QStringLiteral("Search mods"));
            bar->setSearchText(QStringLiteral("sodium"));
            bar->setFilterCount(4);
            bar->setResultsCaption(QStringLiteral("12,431 results"));
            bar->setSortOptions({ { QStringLiteral("relevance"), QStringLiteral("Relevance") } }, QStringLiteral("relevance"));
            bar->setViewModesAvailable(true);
            strip->setFacets(facets);
            strip->setDefaults(modrinthDefaults());
            strip->setState(state);
            page.show();
            grab(&page, QStringLiteral("toolbar-strip"));

            QWidget window;
            window.resize(900, 700);
            QPushButton anchor(QStringLiteral("Filters"), &window);
            window.show();
            FilterPanel panel;
            panel.setFacets(facets);
            panel.setDefaults(modrinthDefaults());
            panel.setState(state);
            panel.setResultText(QStringLiteral("48 of 312 loaded"));
            panel.popup(&anchor);
            QTest::qWait(50);
            grab(panel.panel(), QStringLiteral("panel"));
            panel.close();

            FilterPanel disabled;
            disabled.setFacets(facetsFor(Kind::Mod, facetCtx(Provider::CurseForge, false)));
            disabled.setDefaults(modrinthDefaults());
            disabled.popup(&anchor);
            QTest::qWait(50);
            grab(disabled.panel(), QStringLiteral("panel-curseforge"));
            disabled.close();

            for (auto shape : { ZeroSkeleton::Shape::Cards, ZeroSkeleton::Shape::Gallery, ZeroSkeleton::Shape::Cinematic }) {
                ZeroSkeleton sk(shape, QStringLiteral("Loading"));
                sk.resize(900, 520);
                sk.show();
                grab(&sk, QStringLiteral("skeleton-%1").arg(int(shape)));
            }
            NoMatchState empty;
            empty.resize(520, 320);
            empty.setContext(QStringLiteral("Loader: Quilt"), true);
            empty.show();
            grab(&empty, QStringLiteral("empty"));
            NetworkErrorState error;
            error.resize(520, 320);
            error.setError(QStringLiteral("Modrinth"), QStringLiteral("Connection timed out"));
            error.show();
            grab(&error, QStringLiteral("error"));
        }
        qApp->setStyleSheet({});
        setMotion(false);
    }

    // ---- Empty and error states --------------------------------------------------------------------------------------

    void noMatchStateNamesTheMostRestrictiveFilter()
    {
        NoMatchState state;
        state.resize(500, 400);
        state.show();
        QVERIFY(state.showEverythingButton()->isHidden());
        QVERIFY(state.bodyText().contains(QStringLiteral("removing a filter")));  // unknown chip: generic body
        state.setContext(QStringLiteral("Loader: Quilt"), false);
        QVERIFY(state.bodyText().contains(QStringLiteral("Loader: Quilt")));
        QVERIFY(state.showEverythingButton()->isHidden());
        state.setContext(QStringLiteral("Loader: Quilt"), true);
        QVERIFY(!state.showEverythingButton()->isHidden());

        QSignalSpy clear(&state, &NoMatchState::clearAllRequested);
        QSignalSpy everything(&state, &NoMatchState::showEverythingRequested);
        state.findChild<QPushButton*>()->click();  // the primary action
        QCOMPARE(clear.size(), 1);
        state.showEverythingButton()->click();
        QCOMPARE(everything.size(), 1);
        QVERIFY(!state.grab().isNull());
    }

    void networkErrorStateRetries()
    {
        NetworkErrorState state;
        state.resize(500, 400);
        state.show();
        state.setError(QStringLiteral("Modrinth"), QStringLiteral("Connection timed out"));
        QCOMPARE(state.titleText(), QStringLiteral("Couldn't reach Modrinth"));
        QCOMPARE(state.bodyText(), QStringLiteral("Connection timed out"));
        QSignalSpy retry(&state, &NetworkErrorState::retryRequested);
        state.findChild<QPushButton*>()->click();
        QCOMPARE(retry.size(), 1);
    }
};

QTEST_MAIN(ZeroContentKitTest)

#include "ZeroContentKit_test.moc"
