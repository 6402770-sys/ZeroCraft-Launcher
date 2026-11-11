// SPDX-License-Identifier: GPL-3.0-only
// TechnicMeta / TechnicDetailsStore: Technic API parsing (lists, Solder and single-zip details, nulls, errors), the preview
// title rule and text helpers, and the details cache (cache hits, coalescing, out-of-order answers, failure and retry).
#include <QFile>
#include <QList>
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "zero/content/TechnicDetailsStore.h"
#include "zero/content/TechnicMeta.h"

using namespace Zero::Content;

namespace {

QByteArray fixture(const QString& name)
{
    QFile f(QFINDTESTDATA("testdata/TechnicMeta/" + name));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "missing fixture" << name;
        return {};
    }
    return f.readAll();
}

/// JSON written with single quotes (no raw string literals: moc can't parse them in this file).
QByteArray j(const char* json)
{
    return QByteArray(json).replace('\'', '"');
}

QByteArray detailsJson(const QString& slug, const QString& displayName)
{
    const QString json = QString::fromUtf8(j("{'id':1,'name':'%1','displayName':'%2','user':'someone','url':'https://example.org/%1.zip',"
                                             "'solder':null,'minecraft':'1.12.2','version':'1.0'}"));
    return json.arg(slug, displayName).toUtf8();
}

QByteArray solderJson(const QString& recommended, const QStringList& builds)
{
    const QString json = QString::fromUtf8(j("{'recommended':'%1','latest':'%2','builds':['%3']}"));
    return json.arg(recommended, builds.last(), builds.join(QStringLiteral("\",\""))).toUtf8();
}

const QByteArray kUnknownPack = j("{'error':'Modpack does not exist'}");

/// A network stand-in: remembers every request and answers only when the test says so, in any order.
struct ManualNetwork {
    struct Pending {
        QString slug;
        QString solderBase;
        TechnicDetailsStore::Reply reply;
    };
    QList<Pending> details;
    QList<Pending> versions;
    int detailFetches = 0;
    int versionFetches = 0;

    TechnicDetailsStore::DetailsFetcher detailsFetcher()
    {
        return [this](const QString& slug, TechnicDetailsStore::Reply reply) {
            detailFetches++;
            details.append({ slug, {}, std::move(reply) });
        };
    }
    TechnicDetailsStore::VersionsFetcher versionsFetcher()
    {
        return [this](const QString& slug, const QString& base, TechnicDetailsStore::Reply reply) {
            versionFetches++;
            versions.append({ slug, base, std::move(reply) });
        };
    }
    /// Answers (and forgets) the oldest pending request for `slug`.
    static bool answer(QList<Pending>& list, const QString& slug, const Result<QByteArray>& result)
    {
        for (int i = 0; i < list.size(); i++) {
            if (list[i].slug == slug) {
                auto reply = list.takeAt(i).reply;
                reply(result);
                return true;
            }
        }
        return false;
    }
    bool answerDetails(const QString& slug, const Result<QByteArray>& result) { return answer(details, slug, result); }
    bool answerVersions(const QString& slug, const Result<QByteArray>& result) { return answer(versions, slug, result); }
};

/// What the Technic page does with store answers: show them only while their slug is the selection.
struct PreviewProbe {
    TechnicSelection selection;
    QString shownSlug;
    QString shownTitle;
    QString shownError;
    QStringList shownVersions;

    void attach(TechnicDetailsStore& store)
    {
        QObject::connect(&store, &TechnicDetailsStore::detailsReady, &store, [this, &store](const QString& slug) {
            if (!selection.isCurrent(slug)) {
                return;
            }
            const auto d = store.details(slug);
            shownSlug = slug;
            shownTitle = previewTitle(d ? &*d : nullptr, {}, slug);
            shownError.clear();
        });
        QObject::connect(&store, &TechnicDetailsStore::detailsFailed, &store, [this](const QString& slug, const QString& reason) {
            if (selection.isCurrent(slug)) {
                shownSlug = slug;
                shownError = reason;
            }
        });
        QObject::connect(&store, &TechnicDetailsStore::versionsReady, &store, [this, &store](const QString& slug) {
            if (selection.isCurrent(slug)) {
                shownVersions = store.versions(slug)->builds;
            }
        });
    }
};

}  // namespace

class TechnicMetaTest : public QObject {
    Q_OBJECT
   private slots:
    // ---- search / trending ----

    void searchResponseFromApi()
    {
        const auto packs = parseSearchResponse(fixture("search.json"));
        QVERIFY2(packs.has_value(), qPrintable(packs ? QString() : packs.error()));
        QCOMPARE(packs->size(), 4);
        const TechnicPackSummary& first = packs->first();
        QCOMPARE(first.id, QStringLiteral("735902"));
        QCOMPARE(first.slug, QStringLiteral("tekkit-legends"));
        QCOMPARE(first.name, QStringLiteral("Tekkit Legends"));
        QCOMPARE(first.platformUrl, QStringLiteral("https://www.technicpack.net/modpack/tekkit-legends.735902"));
        QVERIFY(first.iconUrl.startsWith("https://cdn.technicpack.net/platform2/pack-icons/735902.png"));
        QCOMPARE(packs->at(2).slug, QStringLiteral("tekkit"));
        QCOMPARE(packs->at(2).name, QStringLiteral("Tekkit Classic"));
    }

    void searchSkipsVanillaAndToleratesMissingIcons()
    {
        const QByteArray json = j("{'modpacks':["
            "{'id':'1','name':'Vanilla','slug':'vanilla','iconUrl':'https://x/1.png'},"
            "{'id':2,'name':'No Icon','slug':'no-icon','iconUrl':null},"
            "{'name':'Bare','slug':'bare'}"
        "]}");
        const auto packs = parseSearchResponse(json);
        QVERIFY(packs.has_value());
        QCOMPARE(packs->size(), 2);
        QCOMPARE(packs->at(0).slug, QStringLiteral("no-icon"));
        QCOMPARE(packs->at(0).id, QStringLiteral("2"));
        QVERIFY(packs->at(0).iconUrl.isEmpty());
        QCOMPARE(packs->at(1).slug, QStringLiteral("bare"));
        QVERIFY(packs->at(1).id.isEmpty());
        QVERIFY(packs->at(1).platformUrl.isEmpty());

        const auto empty = parseSearchResponse(j("{'modpacks':[]}"));
        QVERIFY(empty.has_value());
        QVERIFY(empty->isEmpty());
    }

    void searchErrors_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("malformed") << j("{'modpacks':[{'name':'A',");
        QTest::newRow("empty body") << QByteArray();
        QTest::newRow("root array") << j("[{'name':'A','slug':'a'}]");
        QTest::newRow("no modpacks") << j("{'error':'nope'}");
        QTest::newRow("modpacks not array") << j("{'modpacks':{}}");
        QTest::newRow("entry not object") << j("{'modpacks':['a']}");
        QTest::newRow("entry without slug") << j("{'modpacks':[{'name':'A'}]}");
        QTest::newRow("entry with null name") << j("{'modpacks':[{'name':null,'slug':'a'}]}");
    }
    void searchErrors()
    {
        QFETCH(QByteArray, json);
        const auto packs = parseSearchResponse(json);
        QVERIFY(!packs.has_value());
        QVERIFY(!packs.error().isEmpty());
    }

    // ---- pack details ----

    void detailsOfSolderPack()
    {
        const auto d = parsePackDetails(fixture("details-solder.json"));
        QVERIFY2(d.has_value(), qPrintable(d ? QString() : d.error()));
        QCOMPARE(d->id, QStringLiteral("735902"));
        QCOMPARE(d->slug, QStringLiteral("tekkit-legends"));
        QCOMPARE(d->displayName, QStringLiteral("Tekkit Legends"));
        QCOMPARE(d->author, QStringLiteral("CanVox"));
        QCOMPARE(d->description, QStringLiteral("Tekkit returns in this legendary pack!"));
        QCOMPARE(d->minecraftVersion, QStringLiteral("1.4.7"));
        QCOMPARE(d->version, QStringLiteral("1.1.1"));
        QCOMPARE(d->platformUrl, QStringLiteral("https://www.technicpack.net/modpack/tekkit-legends.735902"));
        QVERIFY(d->iconUrl.contains("pack-icons/735902.png"));
        QVERIFY(d->logoUrl.contains("pack-logos/735902.png"));
        QVERIFY(d->backgroundUrl.contains("pack-backgrounds/735902.jpg"));
        QCOMPARE(d->installs, qint64(4862862));
        QCOMPARE(d->runs, qint64(22738777));
        QCOMPARE(d->ratings, qint64(0));
        QVERIFY(d->tags.isEmpty());  // "tags": null
        QVERIFY(d->isOfficial);
        QVERIFY(!d->isServer);
        QVERIFY(d->downloadUrl.isEmpty());  // "url": null
        QCOMPARE(d->solderUrl, QStringLiteral("https://solder.technicpack.net/api/"));
        QVERIFY(d->isSolder());
        QVERIFY(d->isInstallable());
        QCOMPARE(d->installUrl(), QStringLiteral("https://solder.technicpack.net/api"));
        QCOMPARE(d->feed.size(), kMaxFeedEntries);
        QCOMPARE(d->feed.first().content, QStringLiteral("Updated to version 1.1.1"));
        QCOMPARE(d->feed.first().date, qint64(1450491810));
        QCOMPARE(d->feed.first().user, QStringLiteral("CanVox"));
        QVERIFY(d->feed.first().url.contains("/changelog/"));
    }

    void detailsOfSingleZipPack()
    {
        const auto d = parsePackDetails(fixture("details-zip.json"));
        QVERIFY2(d.has_value(), qPrintable(d ? QString() : d.error()));
        QCOMPARE(d->slug, QStringLiteral("shivaxi-rlcraft"));
        QCOMPARE(d->displayName, QStringLiteral("RLCraft"));
        QCOMPARE(d->author, QStringLiteral("Shivaxi"));
        QCOMPARE(d->minecraftVersion, QStringLiteral("1.12.2"));
        QCOMPARE(d->version, QStringLiteral("2.9.3"));
        QCOMPARE(d->downloadUrl, QStringLiteral("https://bhrepo.com/packs/rlcraft/modpack.zip"));
        QVERIFY(d->solderUrl.isEmpty());  // "solder": null
        QVERIFY(!d->isSolder());
        QCOMPARE(d->installUrl(), d->downloadUrl);
        QCOMPARE(d->ratings, qint64(1989));
        QVERIFY(!d->isOfficial);
        QCOMPARE(d->tags.size(), 10);
        QCOMPARE(d->tags.first(), QStringLiteral("rlcraft"));
        QCOMPARE(d->tags.last(), QStringLiteral("shivaxi"));
    }

    void detailsWithMissingAndNullFields()
    {
        const auto bare = parsePackDetails(j("{'name':'some-pack'}"));
        QVERIFY(bare.has_value());
        QCOMPARE(bare->slug, QStringLiteral("some-pack"));
        QVERIFY(bare->displayName.isEmpty());
        QVERIFY(bare->author.isEmpty());
        QVERIFY(bare->description.isEmpty());
        QVERIFY(bare->iconUrl.isEmpty());
        QCOMPARE(bare->installs, qint64(0));
        QVERIFY(bare->feed.isEmpty());
        QVERIFY(!bare->isInstallable());
        QVERIFY(bare->installUrl().isEmpty());

        const auto nulls = parsePackDetails(j("{'id':null,'name':'p','displayName':null,'user':null,'url':null,'solder':null,"
            "'minecraft':null,'version':null,'description':null,'platformUrl':null,'icon':null,'logo':{'url':null},"
            "'background':{},'installs':null,'runs':'12','ratings':-3,'tags':' a, ,b,a ','isServer':null,'isOfficial':null,"
            "'feed':null}"));
        QVERIFY(nulls.has_value());
        QVERIFY(nulls->id.isEmpty());
        QVERIFY(nulls->displayName.isEmpty());
        QVERIFY(nulls->logoUrl.isEmpty());
        QVERIFY(nulls->backgroundUrl.isEmpty());
        QCOMPARE(nulls->runs, qint64(12));
        QCOMPARE(nulls->ratings, qint64(0));
        QCOMPARE(nulls->tags, (QStringList{ "a", "b" }));
        QVERIFY(!nulls->isServer);
        QVERIFY(!nulls->isInstallable());
    }

    void detailsInstallSource()
    {
        // A zip URL wins over Solder; an empty zip URL falls back to Solder; Solder trailing slashes are stripped.
        auto both = parsePackDetails(j("{'name':'p','url':'https://e.org/p.zip','solder':'https://s.org/api/'}"));
        QVERIFY(both.has_value());
        QVERIFY(!both->isSolder());
        QCOMPARE(both->installUrl(), QStringLiteral("https://e.org/p.zip"));

        auto emptyZip = parsePackDetails(j("{'name':'p','url':'','solder':'https://s.org/api///'}"));
        QVERIFY(emptyZip.has_value());
        QVERIFY(emptyZip->isSolder());
        QCOMPARE(emptyZip->installUrl(), QStringLiteral("https://s.org/api"));
    }

    void detailsFeedIsNewestFirstAndCapped()
    {
        const auto d = parsePackDetails(j("{'name':'p','feed':["
            "{'date':3,'content':'three'},{'date':7,'content':'seven'},'junk',{'date':1,'content':'one'},"
            "{'date':5,'content':'five'},{'date':6,'content':'six'},{'date':2,'content':'two'},{'date':4,'content':'four'},"
            "{'date':9}]}"));
        QVERIFY(d.has_value());
        QStringList contents;
        for (const auto& e : d->feed) {
            contents << e.content;
        }
        QCOMPARE(contents, (QStringList{ "seven", "six", "five", "four", "three" }));
    }

    void detailsErrors_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expected");
        QTest::newRow("unknown slug") << j("{'error':'Modpack does not exist'}") << QString("Modpack does not exist");
        QTest::newRow("empty error") << j("{'error':''}") << QString("doesn't know this pack");
        QTest::newRow("malformed") << j("{'name':'p',") << QString("JSON");
        QTest::newRow("not an object") << j("['p']") << QString();
        QTest::newRow("missing name") << j("{'displayName':'P'}") << QString("name");
        QTest::newRow("null name") << j("{'name':null,'displayName':'P'}") << QString("name");
        QTest::newRow("blank name") << j("{'name':'  ','displayName':'P'}") << QString("name");
    }
    void detailsErrors()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, expected);
        const auto d = parsePackDetails(json);
        QVERIFY(!d.has_value());
        QVERIFY2(d.error().contains(expected), qPrintable(d.error()));
    }

    void solderVersions()
    {
        const auto v = parseSolderVersions(j("{'name':'p','recommended':'1.0.2','latest':'1.1','builds':['1.0.1','1.0.2','1.1']}"));
        QVERIFY(v.has_value());
        QCOMPARE(v->recommended, QStringLiteral("1.0.2"));
        QCOMPARE(v->latest, QStringLiteral("1.1"));
        QCOMPARE(v->builds, (QStringList{ "1.0.1", "1.0.2", "1.1" }));

        const QByteArray noBuilds = j("{'recommended':'1','latest':'1'}");
        QVERIFY(!parseSolderVersions(noBuilds).has_value());
        QVERIFY(!parseSolderVersions(kUnknownPack).has_value());
        QVERIFY(!parseSolderVersions("<html>502</html>").has_value());
    }

    void solderUrl()
    {
        QCOMPARE(solderPackUrl("https://solder.technicpack.net/api/", "tekkit-legends"),
                 QStringLiteral("https://solder.technicpack.net/api/modpack/tekkit-legends"));
        QCOMPARE(solderPackUrl("https://s.org/api", "p"), QStringLiteral("https://s.org/api/modpack/p"));
        // a slug can't escape the path or add a query
        QCOMPARE(solderPackUrl("https://s.org/api", "../x?y"), QStringLiteral("https://s.org/api/modpack/..%2Fx%3Fy"));
    }

    // ---- preview text ----

    void titleRule()
    {
        TechnicPackDetails d;
        d.slug = "tekkit-legends";
        d.displayName = "Tekkit Legends";
        QCOMPARE(previewTitle(&d, "Listed Name", d.slug), QStringLiteral("Tekkit Legends"));  // displayName first
        d.displayName = "   ";
        QCOMPARE(previewTitle(&d, "Listed Name", d.slug), QStringLiteral("Listed Name"));  // then the list name
        QCOMPARE(previewTitle(nullptr, "Listed Name", d.slug), QStringLiteral("Listed Name"));
        QCOMPARE(previewTitle(&d, "", d.slug), QStringLiteral("tekkit-legends"));  // the slug only when nothing else exists
        QCOMPARE(previewTitle(nullptr, " ", "slug"), QStringLiteral("slug"));
        QCOMPARE(previewTitle(nullptr, {}, {}), QString());
    }

    void plainTextOfDescriptions()
    {
        QCOMPARE(plainText("Line one<br>line <b>two</b><br/><p>Para&nbsp;&amp;&nbsp;more</p>"),
                 QStringLiteral("Line one line two Para & more"));
        QCOMPARE(plainText("&lt;b&gt; is literal, &amp;lt; too, &#233;&#x1F600; &#0; &quot;q&quot; &#39;s"),
                 QString::fromUtf8("<b> is literal, &lt; too, \xC3\xA9\xF0\x9F\x98\x80 &#0; \"q\" 's"));
        QCOMPARE(plainText("<ul><li>a</li><li>b</li></ul>"), QStringLiteral("a b"));
        QCOMPARE(plainText(""), QString());

        const QString longText = "word " + QString("abcdefghij ").repeated(20);
        const QString summary = plainTextSummary(longText, 40);
        QVERIFY(summary.size() <= 40);
        QVERIFY(summary.endsWith(QChar(0x2026)));
        QVERIFY(!summary.contains("  "));
        QCOMPARE(plainTextSummary("short <i>text</i>", 40), QStringLiteral("short text"));
        QCOMPARE(plainTextSummary("anything", 0), QString());
    }

    void previewHtmlShowsDetails()
    {
        auto d = parsePackDetails(fixture("details-zip.json"));
        QVERIFY(d.has_value());
        d->description = "<b>Hard</b> mode";
        const QString html = previewHtml(*d, previewTitle(&*d, "RLCraft", d->slug));
        const QString link = QStringLiteral("<a href=\"https://www.technicpack.net/modpack/shivaxi-rlcraft.1530385\">RLCraft</a>");
        QVERIFY(html.contains(link));
        QVERIFY(html.contains("by Shivaxi"));
        QVERIFY(html.contains("Minecraft 1.12.2"));
        QVERIFY(html.contains("Version 2.9.3"));
        QVERIFY(html.endsWith("<b>Hard</b> mode"));  // the API's HTML is kept as provided
        QVERIFY(!html.contains("can't be installed"));

        TechnicPackDetails odd;
        odd.slug = "odd";
        odd.displayName = "<i>Odd</i> & co";
        odd.author = "<script>";
        odd.platformUrl = "javascript:alert(1)";
        const QString oddHtml = previewHtml(odd, previewTitle(&odd, {}, odd.slug));
        QVERIFY(oddHtml.startsWith("&lt;i&gt;Odd&lt;/i&gt; &amp; co"));  // escaped, and not linked
        QVERIFY(!oddHtml.contains("href"));
        QVERIFY(oddHtml.contains("by &lt;script&gt;"));
        QVERIFY(oddHtml.contains("can't be installed"));
    }

    void previewLoadingAndFailure()
    {
        const QString loading = previewLoadingHtml("A & B");
        QVERIFY(loading.startsWith("A &amp; B"));
        QVERIFY(loading.contains("Loading details"));

        const QString failed = previewFailedHtml("Pack", "Host <unknown>");
        QVERIFY(failed.startsWith("Pack"));
        QVERIFY(failed.contains("Couldn't load the details"));
        QVERIFY(failed.contains("Host &lt;unknown&gt;"));
        QCOMPARE(previewFailedHtml("Pack", "  ").count("<br>"), 2);  // no empty reason line
    }

    void selectionGuard()
    {
        TechnicSelection s;
        QVERIFY(s.isEmpty());
        QVERIFY(!s.isCurrent({}));  // nothing selected: an empty slug is never current
        s.select("a");
        QVERIFY(s.isCurrent("a"));
        QVERIFY(!s.isCurrent("b"));
        s.select("b");
        QVERIFY(!s.isCurrent("a"));
        s.clear();
        QVERIFY(!s.isCurrent("b"));
    }

    // ---- details store ----

    void storeCacheHitDoesNotFetchAgain()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        QSignalSpy ready(&store, &TechnicDetailsStore::detailsReady);

        QVERIFY(!store.requestDetails("a"));
        QVERIFY(store.isLoadingDetails("a"));
        QVERIFY(!store.details("a").has_value());
        QVERIFY(net.answerDetails("a", detailsJson("a", "Pack A")));
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("a"));
        QVERIFY(!store.isLoadingDetails("a"));
        QCOMPARE(store.details("a")->displayName, QStringLiteral("Pack A"));

        QVERIFY(store.requestDetails("a"));  // cached: true, no fetch, no signal
        QCOMPARE(net.detailFetches, 1);
        QCOMPARE(ready.count(), 1);
    }

    void storeCoalescesConcurrentRequests()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        QSignalSpy ready(&store, &TechnicDetailsStore::detailsReady);

        QVERIFY(!store.requestDetails("a"));
        QVERIFY(!store.requestDetails("a"));
        QVERIFY(!store.requestDetails("b"));
        QVERIFY(!store.requestDetails("a"));
        QCOMPARE(net.detailFetches, 2);  // one per slug
        QVERIFY(net.answerDetails("a", detailsJson("a", "Pack A")));
        QVERIFY(!net.answerDetails("a", detailsJson("a", "Pack A")));  // nothing else pending for "a"
        QCOMPARE(ready.count(), 1);
    }

    void storeOutOfOrderAnswersNeverShowTheOldPack()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        PreviewProbe page;
        page.attach(store);

        page.selection.select("a");
        store.requestDetails("a");
        page.selection.select("b");  // the user moves on before A answers
        store.requestDetails("b");

        QVERIFY(net.answerDetails("b", detailsJson("b", "Pack B")));
        QCOMPARE(page.shownSlug, QStringLiteral("b"));
        QCOMPARE(page.shownTitle, QStringLiteral("Pack B"));

        QVERIFY(net.answerDetails("a", detailsJson("a", "Pack A")));  // late answer for A
        QCOMPARE(page.shownSlug, QStringLiteral("b"));
        QCOMPARE(page.shownTitle, QStringLiteral("Pack B"));
        QVERIFY(store.details("a").has_value());  // still cached: selecting A again is instant
        QVERIFY(store.requestDetails("a"));
        QCOMPARE(net.detailFetches, 2);

        // A late failure of a deselected pack does not show either
        page.selection.select("c");
        store.requestDetails("c");
        page.selection.select("b");
        QVERIFY(net.answerDetails("c", std::unexpected(QStringLiteral("timeout"))));
        QCOMPARE(page.shownSlug, QStringLiteral("b"));
        QVERIFY(page.shownError.isEmpty());
    }

    void storeReselectingWhileLoadingShowsTheAnswer()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        PreviewProbe page;
        page.attach(store);

        page.selection.select("a");
        store.requestDetails("a");
        page.selection.select("b");
        store.requestDetails("b");
        page.selection.select("a");  // back to A: joins the request in flight
        store.requestDetails("a");
        QCOMPARE(net.detailFetches, 2);
        QVERIFY(net.answerDetails("a", detailsJson("a", "Pack A")));
        QCOMPARE(page.shownTitle, QStringLiteral("Pack A"));
        QVERIFY(net.answerDetails("b", detailsJson("b", "Pack B")));
        QCOMPARE(page.shownTitle, QStringLiteral("Pack A"));
    }

    void storeFailureIsNotCached()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        QSignalSpy ready(&store, &TechnicDetailsStore::detailsReady);
        QSignalSpy failed(&store, &TechnicDetailsStore::detailsFailed);

        store.requestDetails("a");
        QVERIFY(net.answerDetails("a", std::unexpected(QStringLiteral("Host not found"))));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("a"));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("Host not found"));
        QVERIFY(!store.details("a").has_value());
        QVERIFY(!store.isLoadingDetails("a"));

        // an error answer from the API is a failure too, and not cached either
        QVERIFY(!store.requestDetails("a"));
        QVERIFY(net.answerDetails("a", kUnknownPack));
        QCOMPARE(failed.count(), 2);
        QVERIFY(failed.last().at(1).toString().contains("Modpack does not exist"));

        QVERIFY(!store.requestDetails("a"));  // retry
        QCOMPARE(net.detailFetches, 3);
        QVERIFY(net.answerDetails("a", detailsJson("a", "Pack A")));
        QCOMPARE(ready.count(), 1);
        QVERIFY(store.details("a").has_value());
    }

    void storeDropsRepliesThatAreNotInFlight()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        QSignalSpy ready(&store, &TechnicDetailsStore::detailsReady);
        QSignalSpy failed(&store, &TechnicDetailsStore::detailsFailed);

        // a fetcher that answers twice: only the first answer counts
        store.requestDetails("a");
        auto first = net.details.takeFirst().reply;
        first(std::unexpected(QStringLiteral("timeout")));
        first(detailsJson("a", "Pack A"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(ready.count(), 0);
        QVERIFY(!store.details("a").has_value());

        // the retry's answer is not confused with a late answer of the first request
        store.requestDetails("a");
        first(detailsJson("a", "Stale"));
        QCOMPARE(ready.count(), 0);
        QVERIFY(store.isLoadingDetails("a"));
        QVERIFY(net.answerDetails("a", detailsJson("a", "Fresh")));
        QCOMPARE(ready.count(), 1);
        QCOMPARE(store.details("a")->displayName, QStringLiteral("Fresh"));
    }

    void storeInsertedDetailsSupersedeAFetch()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        QSignalSpy ready(&store, &TechnicDetailsStore::detailsReady);

        store.requestDetails("a");
        auto inserted = parsePackDetails(detailsJson("a", "From search"));
        QVERIFY(inserted.has_value());
        store.insertDetails(*inserted);
        QCOMPARE(ready.count(), 1);
        QVERIFY(!store.isLoadingDetails("a"));
        QVERIFY(net.answerDetails("a", detailsJson("a", "From fetch")));
        QCOMPARE(ready.count(), 1);
        QCOMPARE(store.details("a")->displayName, QStringLiteral("From search"));
        QVERIFY(store.requestDetails("a"));
        QCOMPARE(net.detailFetches, 1);
    }

    void storeKeysByRequestedSlug()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        store.requestDetails("Tekkit-Legends");
        QVERIFY(net.answerDetails("Tekkit-Legends", detailsJson("tekkit-legends", "Tekkit Legends")));
        QVERIFY(store.details("Tekkit-Legends").has_value());
        QCOMPARE(store.details("Tekkit-Legends")->slug, QStringLiteral("Tekkit-Legends"));
    }

    void storeEmptySlugAndSynchronousFetcher()
    {
        int fetches = 0;
        TechnicDetailsStore store(
            [&fetches](const QString& slug, TechnicDetailsStore::Reply reply) {
                fetches++;
                reply(detailsJson(slug, "Instant"));
            },
            {});
        QSignalSpy ready(&store, &TechnicDetailsStore::detailsReady);
        QSignalSpy failed(&store, &TechnicDetailsStore::detailsFailed);

        QVERIFY(!store.requestDetails({}));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(fetches, 0);

        QVERIFY(!store.requestDetails("a"));  // answered before returning: the signal already came
        QCOMPARE(ready.count(), 1);
        QVERIFY(store.requestDetails("a"));
        QCOMPARE(fetches, 1);

        // no versions fetcher: fails right away instead of hanging
        QSignalSpy versionsFailed(&store, &TechnicDetailsStore::versionsFailed);
        QVERIFY(!store.requestVersions("a", "https://s.org/api"));
        QCOMPARE(versionsFailed.count(), 1);
    }

    void storeDestroyedBeforeTheAnswer()
    {
        ManualNetwork net;
        auto store = std::make_unique<TechnicDetailsStore>(net.detailsFetcher(), net.versionsFetcher());
        store->requestDetails("a");
        store->requestVersions("a", "https://s.org/api");
        store.reset();
        QVERIFY(net.answerDetails("a", detailsJson("a", "Pack A")));  // must not touch the dead store
        QVERIFY(net.answerVersions("a", solderJson("1", { "1" })));
    }

    // ---- Solder versions ----

    void versionsCacheCoalesceAndRetry()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        QSignalSpy ready(&store, &TechnicDetailsStore::versionsReady);
        QSignalSpy failed(&store, &TechnicDetailsStore::versionsFailed);

        QVERIFY(!store.requestVersions("a", "https://solder.example/api/"));
        QVERIFY(!store.requestVersions("a", "https://solder.example/api/"));
        QCOMPARE(net.versionFetches, 1);
        QCOMPARE(net.versions.first().solderBase, QStringLiteral("https://solder.example/api/"));
        QVERIFY(store.isLoadingVersions("a"));

        QVERIFY(net.answerVersions("a", QByteArray("not json")));
        QCOMPARE(failed.count(), 1);
        QVERIFY(!store.versions("a").has_value());

        QVERIFY(!store.requestVersions("a", "https://solder.example/api/"));  // retry
        QVERIFY(net.answerVersions("a", solderJson("2", { "1", "2", "3" })));
        QCOMPARE(ready.count(), 1);
        QCOMPARE(store.versions("a")->recommended, QStringLiteral("2"));
        QVERIFY(store.requestVersions("a", "https://solder.example/api/"));
        QCOMPARE(net.versionFetches, 2);

        QVERIFY(!store.requestVersions("b", ""));  // no Solder API: fails right away
        QCOMPARE(failed.count(), 2);
        QCOMPARE(net.versionFetches, 2);
    }

    void versionsOutOfOrderNeverShowTheOldPack()
    {
        ManualNetwork net;
        TechnicDetailsStore store(net.detailsFetcher(), net.versionsFetcher());
        PreviewProbe page;
        page.attach(store);

        page.selection.select("a");
        store.requestVersions("a", "https://s.org/api");
        page.selection.select("b");
        store.requestVersions("b", "https://s.org/api");
        QVERIFY(net.answerVersions("b", solderJson("b1", { "b1" })));
        QVERIFY(net.answerVersions("a", solderJson("a2", { "a1", "a2" })));
        QCOMPARE(page.shownVersions, (QStringList{ "b1" }));
    }
};

QTEST_GUILESS_MAIN(TechnicMetaTest)

#include "TechnicMeta_test.moc"
