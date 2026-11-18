// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - tests of the Discover install cart (docs/zero/SHELL.md, "Discover > Install cart"), Part A: the pure core (keys across
// kinds, selection order, the cap of 50, the summary texts), the check box of the cards (hit-test rect, Ctrl+click, plain click opens the
// details, installed cards have none, Space), the details panel's "Add to cart" toggle and the selection bar of the page (visibility,
// text, Clear, one cart for the three sections, the slide). Part B: the review dialog (row defaults, skipped rows, remove, the ONE
// dependency run, merge and dedupe, Checking, stale answers, turn-on), the pure plan / merge / text core and the pack turn-on helper.
// Offscreen, with fake sources and resolvers and hand-built TargetInstances: no network, no instances, no APPLICATION.
#include <optional>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QListView>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <functional>
#include <memory>

#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/content/CartReviewDialog.h"
#include "ui/zero/content/ContentDetailsPanel.h"
#include "ui/zero/content/ContentToolbar.h"
#include "ui/zero/content/ModRows.h"
#include "ui/zero/content/ModpackRows.h"
#include "ui/zero/content/ModpacksSection.h"
#include "ui/zero/content/ModsSection.h"
#include "ui/zero/content/PackRows.h"
#include "ui/zero/content/RemoteImageCache.h"
#include "ui/zero/content/SelectionBar.h"
#include "ui/zero/pages/DiscoverPage.h"
#include "zero/content/CartInstall.h"
#include "zero/content/InstallCart.h"
#include "zero/content/InstalledMods.h"
#include "zero/content/PackOrder.h"

using namespace Zero;
using namespace Zero::Content;

namespace {

void setMotion(bool on)
{
    ThemeSpec spec = ThemeSpec::obsidian();
    spec.reduceMotion = !on;
    Theme::setCurrent(spec);
}

QString S(const char* text)
{
    return QString::fromUtf8(text);
}

class FakeSource : public ModpackSource {
   public:
    struct SearchCall {
        ResourceAPI::SearchArgs args;
        quint64 generation = 0;
    };
    struct ItemCall {
        ModpackItemPtr item;
        quint64 generation = 0;
    };

    explicit FakeSource(Provider provider) : m_provider(provider) {}

    Provider provider() const override { return m_provider; }
    void search(const ResourceAPI::SearchArgs& args, quint64 generation) override { searches.append({ args, generation }); }
    void details(const ModpackItemPtr& item, quint64 generation) override { detailCalls.append({ item, generation }); }
    void versions(const ModpackItemPtr& item, quint64 generation) override { versionCalls.append({ item, generation }); }
    void changelog(const ModpackItemPtr&, const QString&, quint64) override {}
    void versionsFor(const ModpackItemPtr& item, quint64 generation, const VersionFilter& filter) override
    {
        versionsForCalls.append({ item, generation });
        versionsForFilters.append(filter);
    }

    void answerSearch(int call, const QList<ModpackItemPtr>& items)
    {
        emit searchFinished(searches.at(call).generation, searches.at(call).args.offset, items, int(items.size()));
    }
    void answerVersions(int call, const QList<ModpackVersion>& versions)
    {
        emit versionsFinished(versionCalls.at(call).generation, versionCalls.at(call).item, versions);
    }

    QList<SearchCall> searches;
    QList<ItemCall> detailCalls;
    QList<ItemCall> versionCalls;
    QList<ItemCall> versionsForCalls;
    QList<VersionFilter> versionsForFilters;

   private:
    Provider m_provider;
};

class FakeInstalled : public InstalledModsSource {
   public:
    void setInstance(const QString& instanceId) override { instance = instanceId; }
    void setActive(bool on) override { active = on; }
    QList<InstalledMod> mods() const override { return list; }
    void set(const QList<InstalledMod>& mods)
    {
        list = mods;
        emit changed();
    }

    QString instance;
    bool active = false;
    QList<InstalledMod> list;
};

TargetInstance makeTarget(const QString& id, const QString& name, const QString& mc, ModPlatform::ModLoaderTypes loaders)
{
    TargetInstance t;
    t.id = id;
    t.name = name;
    t.iconKey = QStringLiteral("default");
    t.mcVersion = mc;
    t.loaders = loaders;
    t.loaderText = loaderDisplayNames(loaders).join(QStringLiteral(", "));
    return t;
}

TargetInstance fabric121() { return makeTarget(S("fab"), S("Fabric Fun"), S("1.21.1"), ModPlatform::Fabric); }

ModpackItemPtr makeItem(const QString& id, const QString& name, const QString& loader, Provider provider = Provider::Modrinth)
{
    ModPlatform::IndexedPack pack;
    pack.addonId = id;
    pack.provider = provider == Provider::CurseForge ? ModPlatform::ResourceProvider::FLAME : ModPlatform::ResourceProvider::MODRINTH;
    pack.name = name;
    pack.slug = name.toLower();
    pack.description = S("About %1").arg(name);
    pack.authors = { { S("someone"), QString() } };
    pack.logoUrl = S("https://example.test/icon/%1.png").arg(id);
    pack.gameVersions = { S("1.21.1") };
    pack.loaderNames = { loader };
    pack.downloads = 1234;
    pack.websiteUrl = S("https://example.test/%1").arg(id);
    return itemFromPack(provider, pack);
}

ModpackItemPtr makeMod(const QString& id, const QString& name = QString(), Provider provider = Provider::Modrinth)
{
    return makeItem(id, name.isEmpty() ? S("Mod %1").arg(id) : name, S("fabric"), provider);
}
ModpackItemPtr makePack(const QString& id, const QString& name = QString())
{
    return makeItem(id, name.isEmpty() ? S("Pack %1").arg(id) : name, S("minecraft"));
}

QList<ModpackItemPtr> makeMods(int count)
{
    QList<ModpackItemPtr> out;
    for (int i = 1; i <= count; ++i) {
        out.append(makeMod(QString::number(i)));
    }
    return out;
}
QList<ModpackItemPtr> makePacks(int count)
{
    QList<ModpackItemPtr> out;
    for (int i = 1; i <= count; ++i) {
        out.append(makePack(QString::number(i)));
    }
    return out;
}

ModpackVersion makeVersion(const QString& id, const QString& name, const QString& loader)
{
    ModpackVersion v;
    v.id = id;
    v.name = name;
    v.downloadUrl = S("https://cdn.test/") + id;
    v.type = ModpackVersion::Type::Release;
    v.gameVersions = { S("1.21.1") };
    v.loaders = { loader };
    v.date = QDateTime(QDate(2024, 3, 12), QTime(10, 0), Qt::UTC);
    return v;
}

/// The fakes of one section.
struct Env {
    FakeSource modrinth{ Provider::Modrinth };
    FakeSource curseforge{ Provider::CurseForge };
    FakeInstalled installed;
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };

    ModsEnvironment environment()
    {
        ModsEnvironment env;
        env.sources.modrinth = &modrinth;
        env.images = &images;
        env.installedMods = &installed;
        env.icons = [](const QString&) {
            QPixmap pm(20, 20);
            pm.fill(Qt::darkGreen);
            return QIcon(pm);
        };
        return env;
    }
};

/// One section (Mods, Resource Packs or Shaders) with the cart, the first page answered.
template <class Section>
struct SectionRig {
    Env env;
    InstallCartModel cart;
    std::unique_ptr<Section> section;
    QString loader = S("fabric");

    void open(const QList<ModpackItemPtr>& items, const QList<TargetInstance>& targets = { fabric121() })
    {
        ModsEnvironment e = env.environment();
        e.cart = &cart;
        section = std::make_unique<Section>(e);
        section->resize(1280, 720);
        section->show();
        QVERIFY2(QTest::qWaitForWindowExposed(section.get()), "section not exposed");
        section->setTargets(targets, S("fab"));
        QVERIFY(!env.modrinth.searches.isEmpty());
        env.modrinth.answerSearch(int(env.modrinth.searches.size()) - 1, items);
        QTRY_VERIFY(section->view()->visualRect(section->model()->index(0)).width() > 0);
    }

    QRect cellOf(int row) const { return section->view()->visualRect(section->model()->index(row)); }
    QRect cardOf(int row) const { return section->cardDelegate()->cardRect(cellOf(row)); }
    QRect checkOf(int row) const { return section->cardDelegate()->checkRect(cellOf(row)); }
    QWidget* viewport() const { return section->view()->viewport(); }
    void click(const QPoint& at, Qt::KeyboardModifiers mods = Qt::NoModifier) { QTest::mouseClick(viewport(), Qt::LeftButton, mods, at); }
    /// A point on the card that is not the check box (the middle of its lower half).
    QPoint bodyOf(int row) const
    {
        const QRect card = cardOf(row);
        return QPoint(card.center().x(), card.bottom() - card.height() / 4);
    }
    ContentItemPtr item(int row) const { return section->model()->item(row); }
    /// Opens the item's versions and answers them.
    void answerVersions(int row, const QList<ModpackVersion>& versions)
    {
        section->selectRow(row);
        const int call = int(env.modrinth.versionCalls.size());
        QTRY_VERIFY(env.modrinth.versionCalls.size() > call);
        env.modrinth.answerVersions(call, versions);
    }
};


// ---- Part B: the review dialog and the combined install ----

using Release = ModPlatform::IndexedVersionTypeValue;
const ModPlatform::ModLoaderTypes kFabric{ ModPlatform::Fabric };

ModPlatform::IndexedVersion ver(const QString& fileId, const QString& name, Release type = Release::Release,
                                const QStringList& mc = { S("1.21.1") }, ModPlatform::ModLoaderTypes loaders = kFabric)
{
    ModPlatform::IndexedVersion v;
    v.addonId = S("PROJ");
    v.fileId = fileId;
    v.version = name;
    v.versionNumber = name;
    v.versionType = ModPlatform::IndexedVersionType(type);
    v.mcVersion = mc;
    v.loaders = loaders;
    v.downloadUrl = S("https://cdn.test/%1.jar").arg(fileId);
    v.fileName = name + S(".jar");
    v.date = S("2024-03-10T10:00:00Z");
    v.fileSize = 1536 * 1024;
    return v;
}

/// A pack or shader version: no loader.
ModPlatform::IndexedVersion packVer(const QString& fileId, const QString& name)
{
    auto v = ver(fileId, name, Release::Release, { S("1.21.1"), S("1.12.2") }, ModPlatform::ModLoaderTypes());
    v.fileName = name + S(".zip");
    return v;
}

QList<ModPlatform::IndexedVersion> twoVersions()
{
    return { ver(S("f2"), S("2.0")), ver(S("f1"), S("1.0")) };
}

ModpackItemPtr withVersions(const ModpackItemPtr& item, const QList<ModPlatform::IndexedVersion>& versions)
{
    item->pack->versions = versions;
    item->pack->versionsLoaded = true;
    return item;
}

PlannedMod depOf(const QString& id, const QString& name, const QString& requiredBy, bool maybeInstalled = false)
{
    PlannedMod mod;
    mod.pack = std::make_shared<ModPlatform::IndexedPack>();
    mod.pack->addonId = id;
    mod.pack->provider = ModPlatform::ResourceProvider::MODRINTH;
    mod.pack->name = name;
    mod.version = ver(S("dep-%1").arg(id), S("9.9"));
    mod.version.addonId = id;
    mod.reason = reasonDependency();
    mod.dependentOn = S("file-of-%1").arg(requiredBy);
    mod.requiredBy = { requiredBy };
    mod.maybeInstalled = maybeInstalled;
    return mod;
}

/// Answers by hand: records every request; the test decides when and what it answers.
class FakeResolver : public DependencyResolver {
   public:
    explicit FakeResolver(std::shared_ptr<int> cancelCounter) : m_cancelCounter(std::move(cancelCounter)) {}
    ~FakeResolver() override { cancel(); }
    void resolve(quint64 token, const Request& request) override
    {
        requests.append({ token, request });
        if (answerOnResolve) {
            emit resolved(token, {});
        }
    }
    void cancel() override
    {
        ++cancels;
        if (m_cancelCounter) {
            ++*m_cancelCounter;
        }
    }
    void answer(const QList<PlannedMod>& deps) { emit resolved(requests.last().first, deps); }
    void answerFor(int request, const QList<PlannedMod>& deps) { emit resolved(requests.at(request).first, deps); }
    void fail(const QString& reason) { emit failed(requests.last().first, reason); }

    QList<QPair<quint64, Request>> requests;
    int cancels = 0;
    bool answerOnResolve = false;  ///< answer "nothing needed" inside resolve() (a synchronous resolver)

   private:
    std::shared_ptr<int> m_cancelCounter;
};

/// The environment of a review: one fake source, a fake resolver, hand-made installed data.
struct ReviewRig {
    RemoteImageCache images{ [](const QString&, std::function<void(const RemoteImageCache::FetchResult&)> done) { done({}); } };
    InstallCartModel cart;
    FakeSource modrinth{ Provider::Modrinth };
    FakeResolver* resolver = nullptr;
    QList<InstalledMod> installed;
    QHash<QString, QString> installedFile;  ///< project id -> installed file id (an update row)
    std::shared_ptr<int> cancelCounter = std::make_shared<int>(0);
    bool running = false;
    bool dependencyCheck = true;
    int delayMs = 20;
    int dependencyTimeoutMs = 0;  ///< 0 = the environment's default
    TargetInstance target = fabric121();
    std::optional<VersionFilter> sectionFilter;  ///< the filter of the section's source; unset = the one of the target (a section for the same instance)

    CartReviewEnvironment environment()
    {
        CartReviewEnvironment env;
        env.images = &images;
        env.sourceFor = [this](const CartEntry& entry) -> ModpackSource* {
            if (sectionFilter) {
                modrinth.setVersionFilter(*sectionFilter);
            } else {
                VersionFilter filter;
                filter.mcVersions = { target.mcVersion };
                filter.loaders = entry.kind == Kind::Mod ? target.loaders : ModPlatform::ModLoaderTypes();
                modrinth.setVersionFilter(filter);
            }
            return &modrinth;
        };
        env.makeResolver = [this](QObject* parent) -> DependencyResolver* {
            resolver = new FakeResolver(cancelCounter);
            resolver->setParent(parent);
            return resolver;
        };
        env.dependencyCheckEnabled = [this]() { return dependencyCheck; };
        env.installedMods = [this]() { return installed; };
        env.installedFileId = [this](const CartEntry& entry) { return installedFile.value(entry.item->pack->addonId.toString()); };
        env.isRunning = [this](const QString&) { return running; };
        env.dependencyDelayMs = delayMs;
        if (dependencyTimeoutMs > 0) {
            env.dependencyTimeoutMs = dependencyTimeoutMs;
        }
        return env;
    }

    ModpackItemPtr addMod(const QString& id, const QString& name, const QList<ModPlatform::IndexedVersion>& versions = twoVersions())
    {
        const auto item = withVersions(makeMod(id, name), versions);
        cart.add(Kind::Mod, item);
        return item;
    }
    ModpackItemPtr addPack(const QString& id, const QString& name, const QList<ModPlatform::IndexedVersion>& versions = { packVer(S("p1"), S("1.0")) })
    {
        const auto item = withVersions(makePack(id, name), versions);
        cart.add(Kind::ResourcePack, item);
        return item;
    }
    ModpackItemPtr addShader(const QString& id, const QString& name, const QList<ModPlatform::IndexedVersion>& versions = { packVer(S("s1"), S("1.0")) })
    {
        const auto item = withVersions(makePack(id, name), versions);
        cart.add(Kind::Shader, item);
        return item;
    }
    std::unique_ptr<CartReviewDialog> open()
    {
        return std::make_unique<CartReviewDialog>(environment(), &cart, target);
    }
    std::unique_ptr<CartReviewDialog> open(const TargetInstance& other)
    {
        target = other;
        return open();
    }
};

QString modKey(const QString& id)
{
    return S("mod:modrinth:") + id;
}

}  // namespace

class ZeroInstallCartTest : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase()
    {
        qRegisterMetaType<Zero::Content::ModpackItemPtr>("Zero::Content::ModpackItemPtr");
        qRegisterMetaType<Zero::Content::ContentItemPtr>("Zero::Content::ContentItemPtr");
        qRegisterMetaType<Zero::Content::ContentDetailsPanel::Tab>("Zero::Content::ContentDetailsPanel::Tab");
    }
    void init() { setMotion(false); }
    void cleanup() { setMotion(true); }

    // ---- the core ----

    void keysAreKindProviderAndProject()
    {
        const auto mod = makeMod(S("AANobbMI"));
        QCOMPARE(InstallCart::keyFor(Kind::Mod, *mod), S("mod:modrinth:AANobbMI"));
        QCOMPARE(InstallCart::keyFor(Kind::ResourcePack, *mod), S("resourcepack:modrinth:AANobbMI"));
        QCOMPARE(InstallCart::keyFor(Kind::Shader, *mod), S("shader:modrinth:AANobbMI"));
        QCOMPARE(InstallCart::keyFor(Kind::Mod, *makeMod(S("123"), S("Flame"), Provider::CurseForge)), S("mod:curseforge:123"));
        QCOMPARE(InstallCart::keyFor(Kind::Modpack, *mod), QString());  // modpacks have no cart
    }

    void toggleKeepsSelectionOrderAndKeysAcrossKinds()
    {
        InstallCart cart;
        const auto a = makeMod(S("1"));
        const auto b = makeMod(S("2"));
        const auto samePackId = makePack(S("1"));  // the same project id in another kind is another entry
        QCOMPARE(cart.toggle(Kind::Mod, b), InstallCart::ToggleResult::Added);
        QCOMPARE(cart.toggle(Kind::Mod, a), InstallCart::ToggleResult::Added);
        QCOMPARE(cart.toggle(Kind::ResourcePack, samePackId), InstallCart::ToggleResult::Added);
        QCOMPARE(cart.count(), 3);
        QCOMPARE(cart.countOf(Kind::Mod), 2);
        QCOMPARE(cart.countOf(Kind::ResourcePack), 1);
        QCOMPARE(cart.countOf(Kind::Shader), 0);
        QVERIFY(cart.contains(Kind::Mod, *a));
        QVERIFY(cart.contains(S("resourcepack:modrinth:1")));
        QVERIFY(!cart.contains(S("shader:modrinth:1")));
        // selection order, with what a review needs on every entry
        QCOMPARE(cart.entries().at(0).item, b);
        QCOMPARE(cart.entries().at(1).item, a);
        QCOMPARE(cart.entries().at(2).kind, Kind::ResourcePack);
        QCOMPARE(cart.entries().at(2).provider, Provider::Modrinth);
        QCOMPARE(cart.entries().at(2).key, S("resourcepack:modrinth:1"));
        QCOMPARE(cart.entriesOf(Kind::Mod).size(), 2);
        // toggling again removes; adding twice is not a second entry
        QCOMPARE(cart.toggle(Kind::Mod, b), InstallCart::ToggleResult::Removed);
        QCOMPARE(cart.add(Kind::Mod, a), InstallCart::AddResult::AlreadyIn);
        QCOMPARE(cart.count(), 2);
        QVERIFY(cart.remove(S("mod:modrinth:1")));
        QVERIFY(!cart.remove(S("mod:modrinth:1")));
        QCOMPARE(cart.entries().first().key, S("resourcepack:modrinth:1"));
        cart.clear();
        QVERIFY(cart.isEmpty());
        // nothing that is not content of the three kinds
        QCOMPARE(cart.add(Kind::Modpack, a), InstallCart::AddResult::Invalid);
        QCOMPARE(cart.add(Kind::Mod, ContentItemPtr()), InstallCart::AddResult::Invalid);
        QCOMPARE(cart.toggle(Kind::Mod, ContentItemPtr()), InstallCart::ToggleResult::Invalid);
        QVERIFY(cart.isEmpty());
    }

    void capIsFifty()
    {
        InstallCartModel cart;
        QSignalSpy changed(&cart, &InstallCartModel::changed);
        QSignalSpy limit(&cart, &InstallCartModel::limitReached);
        const auto items = makeMods(InstallCart::kCap + 1);
        for (int i = 0; i < InstallCart::kCap; ++i) {
            QCOMPARE(cart.add(Kind::Mod, items.at(i)), InstallCart::AddResult::Added);
        }
        QCOMPARE(cart.count(), 50);
        QCOMPARE(changed.size(), 50);
        QCOMPARE(cart.add(Kind::Mod, items.last()), InstallCart::AddResult::Full);
        QCOMPARE(cart.toggle(Kind::ResourcePack, makePack(S("x"))), InstallCart::ToggleResult::Full);
        QCOMPARE(cart.count(), 50);
        QCOMPARE(changed.size(), 50);  // a refusal changes nothing
        QCOMPARE(limit.size(), 2);
        // a place frees up: the next add works; removing is always allowed
        QVERIFY(cart.remove(S("mod:modrinth:1")));
        QCOMPARE(cart.add(Kind::Mod, items.last()), InstallCart::AddResult::Added);
        QCOMPARE(cart.count(), 50);
    }

    void summaryTexts()
    {
        QCOMPARE(InstallCart::summaryText(0, 0, 0), QString());
        QCOMPARE(InstallCart::summaryText(1, 0, 0), S("1 mod selected"));
        QCOMPARE(InstallCart::summaryText(3, 0, 0), S("3 mods selected"));
        QCOMPARE(InstallCart::summaryText(0, 0, 1), S("1 shader selected"));
        QCOMPARE(InstallCart::summaryText(0, 0, 4), S("4 shaders selected"));
        QCOMPARE(InstallCart::summaryText(0, 1, 0), S("1 resource pack selected"));
        QCOMPARE(InstallCart::summaryText(0, 2, 0), S("2 resource packs selected"));
        QCOMPARE(InstallCart::summaryText(2, 0, 1), S("2 mods, 1 shader selected"));
        QCOMPARE(InstallCart::summaryText(1, 2, 3), S("1 mod, 2 resource packs, 3 shaders selected"));
        QCOMPARE(InstallCart::countsText(3, 0, 1), S("3 mods, 1 shader"));
        InstallCart cart;
        cart.add(Kind::Shader, makePack(S("s")));
        cart.add(Kind::Mod, makeMod(S("m1")));
        cart.add(Kind::Mod, makeMod(S("m2")));
        QCOMPARE(cart.summaryText(), S("2 mods, 1 shader selected"));  // kind order, not selection order
    }

    void modelEmitsChangedOncePerRealChange()
    {
        InstallCartModel cart;
        QSignalSpy changed(&cart, &InstallCartModel::changed);
        const auto mod = makeMod(S("1"));
        cart.toggle(Kind::Mod, mod);
        QCOMPARE(changed.size(), 1);
        cart.add(Kind::Mod, mod);  // already in
        QCOMPARE(changed.size(), 1);
        QVERIFY(!cart.remove(S("mod:modrinth:zzz")));
        QCOMPARE(changed.size(), 1);
        cart.clear();
        QCOMPARE(changed.size(), 2);
        cart.clear();  // empty already
        QCOMPARE(changed.size(), 2);
    }

    // ---- the check box on the cards ----

    void gridCardCheckRectAndClicks()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(3));
        auto* delegate = qobject_cast<ModDelegate*>(rig.section->cardDelegate());
        QVERIFY(delegate && delegate->hasCart());
        const QRect card = rig.cardOf(0);
        const QRect check = rig.checkOf(0);
        QCOMPARE(check.size(), QSize(20, 20));
        QVERIFY(card.contains(check));
        QCOMPARE(check.right(), card.right() - ModDelegate::kPad);     // top-right corner, inside the card padding
        QVERIFY(check.top() >= card.top() + ModDelegate::kPad - 4 && check.bottom() < card.top() + 60);
        // never over the icon (top-left)
        QVERIFY(!check.intersects(QRect(card.left() + ModDelegate::kPad, card.top() + ModDelegate::kPad, ModDelegate::kIcon, ModDelegate::kIcon)));

        // the box toggles and does not open the details
        rig.click(check.center());
        QVERIFY(rig.cart.contains(Kind::Mod, *rig.item(0)));
        QVERIFY(!rig.section->selectedItem());
        QVERIFY(!rig.section->panel()->isOpen());
        rig.click(check.center());
        QVERIFY(rig.cart.isEmpty());

        // a plain click on the card opens the details exactly as before and ticks nothing
        rig.click(rig.bodyOf(1));
        QCOMPARE(rig.section->selectedItem(), rig.item(1));
        QVERIFY(rig.section->panel()->isOpen());
        QVERIFY(rig.cart.isEmpty());

        // Ctrl+click anywhere on a card ticks it and leaves the open details alone
        rig.click(rig.bodyOf(2), Qt::ControlModifier);
        QVERIFY(rig.cart.contains(Kind::Mod, *rig.item(2)));
        QCOMPARE(rig.section->selectedItem(), rig.item(1));
        rig.click(rig.bodyOf(2), Qt::ControlModifier);
        QVERIFY(rig.cart.isEmpty());
        // a click on the gap under a card is not on the card
        const QRect cell = rig.cellOf(0);
        rig.click(QPoint(rig.cardOf(0).center().x(), cell.bottom()), Qt::ControlModifier);
        QVERIFY(rig.cart.isEmpty());
    }

    void listRowCheckSitsLeftOfTheIcon()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(3));
        emit rig.section->toolbar()->viewModeChanged(ContentToolbar::ViewMode::List);
        QCOMPARE(rig.section->cardDelegate()->mode(), ContentDelegate::Mode::List);
        QTRY_VERIFY(rig.cardOf(0).height() == ModDelegate::kRowHeight);
        const QRect row = rig.cardOf(0);
        const QRect check = rig.checkOf(0);
        QCOMPARE(check.size(), QSize(20, 20));
        QCOMPARE(check.left(), row.left() + Tokens::spaceMd);
        QCOMPARE(check.center().y(), row.center().y());
        QVERIFY(check.right() < row.left() + Tokens::spaceMd + ModDelegate::kRowIcon + 20);  // inside the gutter before the icon
        rig.click(check.center());
        QVERIFY(rig.cart.contains(Kind::Mod, *rig.item(0)));
        rig.click(rig.bodyOf(1), Qt::ControlModifier);
        QCOMPARE(rig.cart.count(), 2);
        QVERIFY(!rig.section->panel()->isOpen());
    }

    void galleryCardsOfPacksAndShadersHaveTheBoxToo()
    {
        {
            SectionRig<PacksSection> rig;
            rig.open(makePacks(2));
            auto* delegate = qobject_cast<PackDelegate*>(rig.section->cardDelegate());
            QVERIFY(delegate && delegate->hasCart());
            const QRect check = rig.checkOf(0);
            QCOMPARE(check.size(), QSize(20, 20));
            QCOMPARE(check.topLeft(), rig.cardOf(0).topLeft() + QPoint(8, 8));  // the artwork's top-left corner
            rig.click(check.center());
            QVERIFY(rig.cart.contains(Kind::ResourcePack, *rig.item(0)));
            QVERIFY(!rig.cart.contains(Kind::Mod, *rig.item(0)));
            QCOMPARE(rig.cart.entries().first().key, S("resourcepack:modrinth:1"));
            rig.click(rig.bodyOf(1), Qt::ControlModifier);
            QCOMPARE(rig.cart.count(), 2);
        }
        {
            SectionRig<ShadersSection> rig;
            rig.open(makePacks(2));
            rig.click(rig.checkOf(1).center());
            QCOMPARE(rig.cart.entries().first().key, S("shader:modrinth:2"));
            QCOMPARE(rig.cart.summaryText(), S("1 shader selected"));
        }
    }

    void noBoxWithoutACartAndNoneForModpacks()
    {
        Env env;
        ModsSection section(env.environment());  // no cart in the environment
        QVERIFY(!section.cardDelegate()->hasCart());
        QVERIFY(section.cardDelegate()->checkRect(QRect(0, 0, 300, 140)).isNull());
        QVERIFY(!section.cart());
        ModpackDelegate modpacks(nullptr);
        QVERIFY(!modpacks.hasCart());
        QVERIFY(modpacks.checkRect(QRect(0, 0, 300, 300)).isNull());
    }

    void installedCardHasNoBoxButAnUpdateDoes()
    {
        SectionRig<ModsSection> rig;
        rig.env.installed.list = { { S("modrinth:1"), S("v2"), S("Mod 1"), S("2.0") },   // the newest
                                   { S("modrinth:2"), S("v0"), S("Mod 2"), S("0.5") },   // older: an update
                                   { S("modrinth:3"), S("v2"), S("Mod 3"), S("2.0") } };  // installed, versions never loaded
        rig.open(makeMods(4));
        const QList<ModpackVersion> versions = { makeVersion(S("v2"), S("2.0"), S("fabric")), makeVersion(S("v1"), S("1.0"), S("fabric")) };
        // before any versions are known an installed item is not eligible (its primary state is unknown); a new one is
        QVERIFY(!rig.section->cartEligible(*rig.item(0)));
        QVERIFY(rig.section->cartEligible(*rig.item(3)));
        rig.answerVersions(0, versions);
        QCOMPARE(rig.section->panel()->primaryButton()->text(), S("Installed"));
        QVERIFY(!rig.section->cartEligible(*rig.item(0)));
        QVERIFY(!rig.section->panel()->cartButton()->isVisibleTo(rig.section->panel()));  // no toggle for it either
        rig.answerVersions(1, versions);
        QCOMPARE(rig.section->panel()->primaryButton()->text(), S("Update"));
        QVERIFY(rig.section->cartEligible(*rig.item(1)));  // "Update available" is eligible
        QVERIFY(!rig.section->cartEligible(*rig.item(2)));

        // clicking where the box would be on the exactly-installed card is a plain click: the details open, nothing is ticked
        rig.section->selectItem(nullptr);
        rig.click(rig.checkOf(0).center());
        QCOMPARE(rig.section->selectedItem(), rig.item(0));
        QVERIFY(rig.cart.isEmpty());
        rig.click(rig.bodyOf(0), Qt::ControlModifier);  // Ctrl+click on it: still nothing to tick
        QVERIFY(rig.cart.isEmpty());
        QVERIFY(!rig.section->toggleCart(rig.item(0)));
        // the update can be ticked
        rig.click(rig.checkOf(1).center());
        QVERIFY(rig.cart.contains(Kind::Mod, *rig.item(1)));
    }

    void noTargetMeansNoBox()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(2));
        QVERIFY(rig.section->cartEligible(*rig.item(0)));
        const ContentItemPtr first = rig.item(0);
        rig.section->setTargets({}, QString());  // the target is gone
        QVERIFY(!rig.section->hasTarget());
        QVERIFY(!rig.section->cartEligible(*first));
        QVERIFY(!rig.section->toggleCart(first));
        QVERIFY(rig.cart.isEmpty());
    }

    void spaceTicksTheFocusedCard()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(3));
        rig.section->selectRow(1);  // the current card (and the open details)
        rig.section->view()->setFocus();
        QTest::keyClick(rig.section->view(), Qt::Key_Space);
        QCOMPARE(rig.cart.count(), 1);
        QVERIFY(rig.cart.contains(Kind::Mod, *rig.item(1)));
        QCOMPARE(rig.section->selectedItem(), rig.item(1));
        QTest::keyClick(rig.section->view(), Qt::Key_Space);
        QVERIFY(rig.cart.isEmpty());
        // Space with a modifier is not ours
        QTest::keyClick(rig.section->view(), Qt::Key_Space, Qt::ControlModifier);
        QVERIFY(rig.cart.isEmpty());
    }

    void cardsPaintTheCartState()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(2));
        rig.section->resize(900, 500);
        const auto grab = [&]() { return rig.viewport()->grab().toImage(); };
        const QRect card = rig.cardOf(1);
        const QPoint probe = card.topLeft() + QPoint(1, card.height() / 2);  // on the left border
        const QColor before = grab().pixelColor(probe);
        rig.section->toggleCartRow(1);
        QCoreApplication::processEvents();
        const QImage after = grab();
        // a ticked card has a 2 px accent border; its check box is filled with the accent
        QCOMPARE(after.pixelColor(probe), Theme::current().accent);
        QVERIFY(after.pixelColor(probe) != before);
        const QRect check = rig.checkOf(1);
        QCOMPARE(after.pixelColor(check.topLeft() + QPoint(2, 10)).rgb(), Theme::current().accent.rgb());
        // the cart is not empty: every eligible card shows its (empty) box, the other one has a scrim and a hairline at its corner
        const QRect other = rig.checkOf(0);
        QVERIFY(after.pixelColor(other.center()) != Theme::current().surface);
    }

    // ---- the details panel ----

    void detailsPanelHasTheCartToggle()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(2));
        auto* panel = rig.section->panel();
        rig.answerVersions(0, { makeVersion(S("v1"), S("1.0"), S("fabric")) });
        QVERIFY(panel->cartButton()->isVisibleTo(panel));
        QCOMPARE(panel->cartButton()->text(), S("Add to cart"));
        QCOMPARE(panel->primaryButton()->text(), S("Add to Fabric Fun"));
        // the primary action is untouched: it still opens today's single-item flow
        QSignalSpy requests(rig.section.get(), &ModsSection::installRequested);
        panel->primaryButton()->click();
        QCOMPARE(requests.size(), 1);
        QVERIFY(rig.cart.isEmpty());

        panel->cartButton()->click();
        QVERIFY(rig.cart.contains(Kind::Mod, *rig.item(0)));
        QCOMPARE(panel->cartButton()->text(), S("In cart"));
        panel->cartButton()->click();
        QVERIFY(rig.cart.isEmpty());
        QCOMPARE(panel->cartButton()->text(), S("Add to cart"));
        // a tick on the card shows in the panel too
        rig.section->toggleCartRow(0);
        QCOMPARE(panel->cartButton()->text(), S("In cart"));
        // another item: the toggle follows it
        rig.section->selectRow(1);
        QTRY_COMPARE(panel->nameText(), rig.item(1)->name);
        rig.section->selectRow(0);
        QTRY_COMPARE(panel->nameText(), rig.item(0)->name);
    }

    void cartToggleSitsRightUnderThePrimaryAction()
    {
        SectionRig<ModsSection> rig;
        rig.open(makeMods(1), { makeTarget(S("fab"), S("An instance with a rather long name"), S("1.21.1"), ModPlatform::Fabric) });
        rig.answerVersions(0, { makeVersion(S("v1"), S("1.0"), S("fabric")) });
        auto* panel = rig.section->panel();
        QTRY_VERIFY(panel->cartButton()->isVisibleTo(panel));
        QCoreApplication::processEvents();
        // primary + Website keep their row (they already fill the panel's width with a long instance name); the toggle is the next row
        const QPoint primary = panel->primaryButton()->mapTo(panel, QPoint(0, 0));
        const QPoint cart = panel->cartButton()->mapTo(panel, QPoint(0, 0));
        QVERIFY(cart.y() >= primary.y() + panel->primaryButton()->height());
        QVERIFY(cart.y() - (primary.y() + panel->primaryButton()->height()) <= 2 * Tokens::spaceLg);
        QVERIFY(panel->cartButton()->geometry().right() <= panel->width());
    }

    // ---- the page: one cart, the bar ----

    void pageBarAndOneCartForThreeSections()
    {
        Env modsEnv;
        Env packsEnv;
        Env shadersEnv;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &modsEnv.images;
        DiscoverHooks hooks;
        hooks.readTargets = []() { return QList<TargetInstance>{ fabric121() }; };
        DiscoverPage page(modpacks, modsEnv.environment(), packsEnv.environment(), shadersEnv.environment(), hooks, nullptr);
        page.setLibrarySelection([]() { return QString(); });
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto* bar = page.selectionBar();
        QVERIFY(page.cart() && bar);
        QVERIFY(!bar->isVisible());  // empty cart: no bar
        QCOMPARE(page.mods()->cart(), page.cart());
        QCOMPARE(page.packs()->cart(), page.cart());
        QCOMPARE(page.shaders()->cart(), page.cart());

        page.showSection(S("mods"));
        QVERIFY(!modsEnv.modrinth.searches.isEmpty());
        modsEnv.modrinth.answerSearch(int(modsEnv.modrinth.searches.size()) - 1, makeMods(3));
        QTRY_VERIFY(page.mods()->view()->visualRect(page.mods()->model()->index(0)).width() > 0);
        page.mods()->toggleCartRow(0);
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->summaryText(), S("1 mod selected"));
        QCOMPARE(bar->iconCount(), 1);
        QCOMPARE(bar->overflowText(), QString());
        QCOMPARE(bar->height(), int(SelectionBar::kHeight));
        QCOMPARE(bar->reviewButton()->text(), S("Review and add to Fabric Fun"));
        // docked under the section stack, not over the cards
        const int sectionBottom = page.mods()->mapTo(&page, QPoint(0, page.mods()->height())).y();
        const int barTop = bar->mapTo(&page, QPoint(0, 0)).y();
        QVERIFY2(barTop >= sectionBottom, qPrintable(S("bar top %1 under section bottom %2").arg(barTop).arg(sectionBottom)));
        QCOMPARE(bar->width(), page.width());

        // the cart survives a section switch and the other sections add to the same bar
        page.showSection(S("resourcepacks"));
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->summaryText(), S("1 mod selected"));
        QVERIFY(!packsEnv.modrinth.searches.isEmpty());
        packsEnv.modrinth.answerSearch(int(packsEnv.modrinth.searches.size()) - 1, makePacks(2));
        QTRY_VERIFY(page.packs()->view()->visualRect(page.packs()->model()->index(0)).width() > 0);
        page.packs()->toggleCartRow(1);
        QCOMPARE(bar->summaryText(), S("1 mod, 1 resource pack selected"));
        page.showSection(S("shaders"));
        QVERIFY(!shadersEnv.modrinth.searches.isEmpty());
        shadersEnv.modrinth.answerSearch(int(shadersEnv.modrinth.searches.size()) - 1, makePacks(2));
        QTRY_VERIFY(page.shaders()->view()->visualRect(page.shaders()->model()->index(0)).width() > 0);
        page.shaders()->toggleCartRow(0);
        page.mods()->toggleCartRow(1);
        QCOMPARE(bar->summaryText(), S("2 mods, 1 resource pack, 1 shader selected"));
        QCOMPARE(bar->iconCount(), 4);
        QCOMPARE(bar->overflowText(), QString());
        page.packs()->toggleCartRow(0);
        QCOMPARE(bar->iconCount(), 4);
        QCOMPARE(bar->overflowText(), S("+1"));  // five entries: four icons and "+1"
        page.showSection(S("mods"));
        QVERIFY(page.mods()->inCart(*page.mods()->model()->item(0)));  // still ticked after the round trip
        QVERIFY(page.mods()->inCart(*page.mods()->model()->item(1)));
        QVERIFY(!page.mods()->inCart(*page.mods()->model()->item(2)));
        page.showSection(S("modpacks"));
        QVERIFY(bar->isVisible());  // modpacks have no cart, the bar stays for the rest

        // the primary only says "review"; Clear empties the cart and hides the bar
        QSignalSpy review(&page, &DiscoverPage::cartReviewRequested);
        bar->reviewButton()->click();
        QCOMPARE(review.size(), 1);
        QCOMPARE(page.cart()->count(), 5);  // nothing was installed or removed
        bar->clearButton()->click();
        QVERIFY(page.cart()->isEmpty());
        QVERIFY(!bar->isVisible());
        QVERIFY(!page.mods()->inCart(*page.mods()->model()->item(0)));
    }

    void barTextsAndTargetName()
    {
        Env modsEnv;
        Env packsEnv;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &modsEnv.images;
        TargetInstance longName = makeTarget(S("fab"), S("An instance with a very long name that cannot fit in the button"), S("1.21.1"), ModPlatform::Fabric);
        QList<TargetInstance> targets = { longName };
        DiscoverHooks hooks;
        hooks.readTargets = [&targets]() { return targets; };
        DiscoverPage page(modpacks, modsEnv.environment(), packsEnv.environment(), hooks, nullptr);
        page.setLibrarySelection([]() { return QString(); });
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto* bar = page.selectionBar();
        page.cart()->add(Kind::Mod, makeMod(S("1")));
        QVERIFY(bar->isVisible());
        const QString text = bar->reviewButton()->text();
        const QString prefix = S("Review and add to ");
        QVERIFY(text.startsWith(prefix));
        QVERIFY(text.endsWith(QChar(0x2026)));  // elided
        QVERIFY(QFontMetrics(bar->reviewButton()->font()).horizontalAdvance(text.mid(prefix.size())) <= SelectionBar::kTargetNameMax);
        // the target changed: the bar's button follows, the entries stay
        targets = { fabric121() };
        page.refreshTargets();
        QCOMPARE(bar->reviewButton()->text(), S("Review and add to Fabric Fun"));
        QCOMPARE(page.cart()->count(), 1);
        QCOMPARE(bar->targetName(), S("Fabric Fun"));

        page.cart()->add(Kind::Mod, makeMod(S("2")));
        page.cart()->add(Kind::Shader, makePack(S("3")));
        QCOMPARE(bar->summaryText(), S("2 mods, 1 shader selected"));
        page.cart()->remove(S("mod:modrinth:1"));
        page.cart()->remove(S("mod:modrinth:2"));
        QCOMPARE(bar->summaryText(), S("1 shader selected"));
        page.cart()->remove(S("shader:modrinth:3"));
        QVERIFY(!bar->isVisible());
        QCOMPARE(bar->summaryText(), QString());
    }

    void barSlidesUpOrAppearsAtOnce()
    {
        InstallCartModel cart;
        QWidget host;
        host.resize(800, 200);
        SelectionBar bar(&cart, nullptr, &host);
        bar.setGeometry(0, 144, 800, SelectionBar::kHeight);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QVERIFY(!bar.isVisible());

        setMotion(false);  // motion off: instant
        cart.add(Kind::Mod, makeMod(S("1")));
        QVERIFY(bar.isVisible());
        QVERIFY(!bar.isSliding());
        QCOMPARE(bar.slideOffset(), 0);
        cart.clear();
        QVERIFY(!bar.isVisible());

        setMotion(true);  // motion on: 160 ms from below
        cart.add(Kind::Mod, makeMod(S("1")));
        QVERIFY(bar.isVisible());
        QVERIFY(bar.isSliding());
        QCOMPARE(bar.slideOffset(), int(SelectionBar::kHeight));
        QTRY_VERIFY_WITH_TIMEOUT(!bar.isSliding(), 2000);
        QCOMPARE(bar.slideOffset(), 0);
    }

    void fullCartToastsOnce()
    {
        Env modsEnv;
        FakeSource modpackSource(Provider::Modrinth);
        ModpacksEnvironment modpacks;
        modpacks.sources.modrinth = &modpackSource;
        modpacks.images = &modsEnv.images;
        DiscoverHooks hooks;
        hooks.readTargets = []() { return QList<TargetInstance>{ fabric121() }; };
        DiscoverPage page(modpacks, modsEnv.environment(), hooks, nullptr);
        page.resize(1280, 720);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        const auto items = makeMods(InstallCart::kCap + 1);
        for (int i = 0; i < InstallCart::kCap; ++i) {
            page.cart()->add(Kind::Mod, items.at(i));
        }
        QCOMPARE(ZeroToast::count(&page), 0);
        QCOMPARE(page.cart()->toggle(Kind::Mod, items.last()), InstallCart::ToggleResult::Full);
        QCOMPARE(ZeroToast::count(&page), 1);
        QCOMPARE(page.cart()->count(), 50);
        QCOMPARE(page.selectionBar()->summaryText(), S("50 mods selected"));
    }

    // ====================================================================================================================
    // Part B: the review dialog and the combined install
    // ====================================================================================================================

    // ---- the pure core ----

    void cartTexts()
    {
        QCOMPARE(cartAddText(3, 0, 0), S("Add 3 mods"));
        QCOMPARE(cartAddText(1, 0, 0), S("Add 1 mod"));
        QCOMPARE(cartAddText(0, 2, 0), S("Add 2 resource packs"));
        QCOMPARE(cartAddText(0, 0, 1), S("Add 1 shader"));
        QCOMPARE(cartAddText(2, 1, 0), S("Add 3 items"));
        QCOMPARE(cartTitleText(1, 1, 1), S("Add 3 items"));
        QCOMPARE(cartPrimaryText(0, 0, 0), S("Add"));
        QCOMPARE(cartPrimaryText(4, 0, 0), S("Add 4 mods"));
        QCOMPARE(cartCheckingText(), S("Checking…"));
        QCOMPARE(cartSubtitleText(S("Pack Test"), S("1.21.1 Fabric")), S("To Pack Test · 1.21.1 Fabric"));
        QCOMPARE(cartToastText(3, 0, 1, S("Pack Test")), S("Added 3 mods and 1 shader to Pack Test"));
        QCOMPARE(cartToastText(2, 1, 1, S("Pack Test")), S("Added 2 mods, 1 resource pack and 1 shader to Pack Test"));
        QCOMPARE(cartToastText(1, 0, 0, S("Pack Test")), S("Added 1 mod to Pack Test"));
        QCOMPARE(cartToastText(0, 2, 0, S("Pack Test")), S("Added 2 resource packs to Pack Test"));
        QCOMPARE(cartToastText(2, 0, 0, S("Pack Test"), true), S("Updated 2 mods in Pack Test"));
        QCOMPARE(cartCloseText(S("Pack Test")), S("Close Pack Test to add content"));
        QCOMPARE(cartFindingVersionsText(), S("Finding versions…"));
        QCOMPARE(cartVersionsFailedText(), S("Couldn't load versions"));
        QCOMPARE(cartNoVersionText(S("1.21.1 Fabric")), S("No version for 1.21.1 Fabric · will be skipped"));
        QCOMPARE(cartNeedsModdedText(), S("Needs a modded instance · will be skipped"));
        QCOMPARE(cartUpdateText(S("2.0")), S("Update → 2.0"));
    }

    void requestListsEveryMod()
    {
        DependencyResolver::Request request;
        QVERIFY(request.all().isEmpty());
        const auto a = makeMod(S("1"));
        const auto b = makeMod(S("2"));
        request.pack = a->pack;
        request.version = ver(S("a"), S("1.0"));
        request.also = { { b->pack, ver(S("b"), S("2.0")) }, { nullptr, ver(S("c"), S("3.0")) } };
        const auto all = request.all();
        QCOMPARE(all.size(), 2);  // the entry without a pack is dropped
        QCOMPARE(all.at(0).pack, a->pack);
        QCOMPARE(all.at(1).pack, b->pack);
        request.pack = nullptr;  // the cart's review: everything in `also`
        QCOMPARE(request.all().size(), 1);
    }

    void mergeDependenciesDedupesAndSplits()
    {
        const PlannedMod fabricForSodium = depOf(S("F"), S("Fabric API"), S("Sodium"));
        PlannedMod fabricForLithium = depOf(S("F"), S("Fabric API"), S("Lithium"));
        PlannedMod cloth = depOf(S("C"), S("Cloth Config"), S("Sodium"));
        const PlannedMod lithium = depOf(S("L"), S("Lithium"), S("Sodium"));  // project L is a cart row
        const PlannedMod oldLib = depOf(S("O"), S("Old Lib"), S("Sodium"), true);
        const PlannedMod oldLibAgain = depOf(S("O"), S("Old Lib"), S("Lithium"), true);
        PlannedMod nothing;  // no pack
        const auto merged = mergeDependencies({ fabricForSodium, cloth, fabricForLithium, lithium, oldLib, oldLibAgain, nothing },
                                              { projectKeyOf(lithium.pack) });
        QStringList names;
        for (const auto& dep : merged.toAdd) {
            names.append(dep.name());
        }
        QCOMPARE(names, (QStringList{ S("Fabric API"), S("Cloth Config") }));
        QCOMPARE(merged.toAdd.at(0).requiredBy, (QStringList{ S("Sodium"), S("Lithium") }));
        QCOMPARE(merged.toAdd.at(0).dependentOn, S("file-of-Sodium"));  // the first stays
        QCOMPARE(merged.alreadyInstalled, (QStringList{ S("Old Lib") }));
    }

    void planGroupsByKindInCartOrder()
    {
        const auto row = [](Kind kind, const QString& id, bool update = false) {
            CartPlanRow r;
            r.kind = kind;
            r.cartKey = InstallCart::kindId(kind) + S(":modrinth:") + id;
            r.pack = makeMod(id)->pack;
            r.version = kind == Kind::Mod ? ver(S("v") + id, S("1.0")) : packVer(S("v") + id, S("1.0"));
            r.update = update;
            return r;
        };
        // selection order: pack, mod, shader, mod (update), pack (update)
        const QList<CartPlanRow> rows = { row(Kind::ResourcePack, S("p1")), row(Kind::Mod, S("m1")), row(Kind::Shader, S("s1")),
                                          row(Kind::Mod, S("m2"), true), row(Kind::ResourcePack, S("p2"), true) };
        const auto plan = buildCartPlan(rows, { depOf(S("D"), S("Dep"), S("m1")), depOf(S("D"), S("Dep"), S("m2")) }, true);
        QList<Kind> kinds;
        QStringList names;
        for (const auto& d : plan.downloads) {
            kinds.append(d.kind);
            names.append(d.mod.pack->addonId.toString());
        }
        QCOMPARE(kinds, (QList<Kind>{ Kind::Mod, Kind::Mod, Kind::Mod, Kind::ResourcePack, Kind::ResourcePack, Kind::Shader }));
        QCOMPARE(names, (QStringList{ S("m1"), S("m2"), S("D"), S("p1"), S("p2"), S("s1") }));  // mods, their dependency, packs, shaders
        QCOMPARE(plan.count(), 6);
        QCOMPARE(plan.countOf(Kind::Mod), 3);
        QCOMPARE(plan.dependencyCount(), 1);  // the repeated dependency counts once
        QCOMPARE(plan.firstPageId(), S("mods"));
        QCOMPARE(plan.cartKeys().size(), 5);
        QCOMPARE(plan.downloads.at(0).mod.reason, reasonStandalone());
        QCOMPARE(plan.downloads.at(1).mod.reason, reasonUpdate());
        QCOMPARE(plan.downloads.at(2).mod.reason, reasonDependency());
        QVERIFY(!plan.allUpdates());
        // only the pack that is not an update goes on top
        QCOMPARE(plan.packsToTurnOn().size(), 1);
        QCOMPARE(plan.packsToTurnOn().first().mod.pack->addonId.toString(), S("p1"));
        QVERIFY(buildCartPlan(rows, {}, false).packsToTurnOn().isEmpty());
        // only updates
        QVERIFY(buildCartPlan({ row(Kind::Mod, S("m2"), true) }, {}, true).allUpdates());
        QCOMPARE(buildCartPlan({ row(Kind::ResourcePack, S("p1")) }, {}, true).firstPageId(), S("resourcepacks"));
        QCOMPARE(buildCartPlan({ row(Kind::Shader, S("s1")) }, {}, true).firstPageId(), S("shaderpacks"));
        QVERIFY(buildCartPlan({}, {}, true).firstPageId().isEmpty());
        // a version without a download URL is not downloadable
        CartPlanRow noUrl = row(Kind::Mod, S("m9"));
        noUrl.version.downloadUrl.clear();
        QVERIFY(buildCartPlan({ noUrl }, {}, true).isEmpty());
    }

    void turnOnManyPutsTheFirstSelectedOnTop()
    {
        using namespace Zero::Content::Packs;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString packs = dir.filePath(S("resourcepacks"));
        QVERIFY(QDir().mkpath(packs));
        for (const char* name : { "Old.zip", "A.zip", "B.zip", "C.zip" }) {
            QFile file(packs + QLatin1Char('/') + S(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("zip");
        }
        const QString options = dir.filePath(S("options.txt"));
        {
            QFile file(options);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("resourcePacks:[\"vanilla\",\"file/Old.zip\"]\nlang:en_us\n");
        }
        const QString history = historyPathFor(dir.path());
        const auto idle = []() { return false; };

        // cart order A, B, C: A ends up highest; Gone.zip is not in the folder and ignored
        const auto outcome = turnOnManyAtTop(options, history, packs, { S("A.zip"), S("B.zip"), S("Gone.zip"), S("C.zip") }, idle);
        QVERIFY2(outcome.ok(), qPrintable(outcome.error));
        QCOMPARE(activeFiles(readOptions(options).entries), (QStringList{ S("A.zip"), S("B.zip"), S("C.zip"), S("Old.zip") }));
        QCOMPARE(readHistory(history).size(), 1);  // ONE write: one history entry, so ONE undo
        QVERIFY(readOptions(options).entries.contains(S("vanilla")));
        QVERIFY(undoLast(options, history, idle).ok());
        QCOMPARE(activeFiles(readOptions(options).entries), (QStringList{ S("Old.zip") }));
        QCOMPARE(readHistory(history).size(), 0);

        // the game runs: refused, nothing written
        QCOMPARE(turnOnManyAtTop(options, history, packs, { S("A.zip") }, []() { return true; }).result, Zero::Content::Packs::Result::Running);
        QCOMPARE(activeFiles(readOptions(options).entries), (QStringList{ S("Old.zip") }));
        // none of the packs is in the folder
        QCOMPARE(turnOnManyAtTop(options, history, packs, { S("Gone.zip") }, idle).result, Zero::Content::Packs::Result::IoError);

        // the pure order
        QCOMPARE(orderAfterTurningOn({ S("A"), S("B") }, { S("B"), S("C"), S("Gone") }, { S("A"), S("B"), S("C") }),
                 (QStringList{ S("A"), S("B"), S("C") }));
        QCOMPARE(orderAfterTurningOn({ S("A"), S("A"), S("X") }, {}, { S("A") }), (QStringList{ S("A") }));
    }

    // ---- the dialog ----

    void rowDefaultsAndOneDependencyRun()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"), { ver(S("s3"), S("3.0-beta"), Release::Beta), ver(S("s2"), S("2.0")), ver(S("s1"), S("1.0")) });
        rig.addMod(S("2"), S("Lithium"));
        rig.addMod(S("3"), S("Iris"));
        const auto dialog = rig.open();
        QCOMPARE(dialog->titleText(), S("Add 3 mods"));
        QCOMPARE(dialog->subtitleText(), S("To Fabric Fun · 1.21.1 Fabric"));
        QCOMPARE(dialog->groupTitles(), (QStringList{ S("Mods") }));
        QCOMPARE(dialog->rowCount(), 3);
        QCOMPARE(dialog->rowKeys(), (QStringList{ modKey(S("1")), modKey(S("2")), modKey(S("3")) }));  // cart order
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Ready);
        // the newest release is preselected, a beta is labelled
        auto* box = dialog->versionBox(modKey(S("1")));
        QCOMPARE(box->count(), 3);
        QCOMPARE(box->currentIndex(), 1);
        QCOMPARE(box->currentText(), S("2.0"));
        QVERIFY(box->itemText(0).contains(S("3.0-beta")) && box->itemText(0).contains(S("Beta"), Qt::CaseInsensitive));
        QCOMPARE(dialog->rowStatus(modKey(S("1"))), S("by someone"));
        QCOMPARE(dialog->rowName(modKey(S("1"))), S("Sodium"));

        // ONE dependency run with the three mods together
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        const auto request = rig.resolver->requests.first().second;
        QVERIFY(!request.pack);
        QCOMPARE(request.all().size(), 3);
        QCOMPARE(request.instanceId, S("fab"));
        QCOMPARE(request.all().at(0).version.fileId.toString(), S("s2"));  // the chosen versions, not the newest
        QVERIFY(dialog->isChecking());
        QCOMPARE(dialog->primaryButton()->text(), S("Checking…"));
        QVERIFY(!dialog->primaryButton()->isEnabled());
        QVERIFY(dialog->dependencyBlockVisible());
        QCOMPARE(dialog->dependencyStatus(), S("Checking dependencies…"));

        rig.resolver->answer({});
        QVERIFY(!dialog->isChecking());
        QCOMPARE(dialog->dependencyStatus(), S("No other mods needed"));
        QCOMPARE(dialog->primaryButton()->text(), S("Add 3 mods"));
        QVERIFY(dialog->primaryButton()->isEnabled());
        QCOMPARE(dialog->plan().count(), 3);
        QCOMPARE(rig.resolver->requests.size(), 1);  // no second run
    }

    void mixedKindsGroupsTitleAndTurnOn()
    {
        ReviewRig rig;
        rig.addPack(S("20"), S("Faithful"));
        rig.addMod(S("1"), S("Sodium"));
        rig.addShader(S("30"), S("Complementary"));
        const auto dialog = rig.open();
        QCOMPARE(dialog->titleText(), S("Add 3 items"));
        QCOMPARE(dialog->windowTitle(), S("Add 3 items"));
        QCOMPARE(dialog->groupTitles(), (QStringList{ S("Mods"), S("Resource packs"), S("Shaders") }));  // fixed order, not selection order
        QVERIFY(dialog->turnOnBox()->isVisibleTo(dialog.get()));
        QVERIFY(dialog->turnOnBox()->isChecked());
        QVERIFY(dialog->turnThemOn());
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QCOMPARE(rig.resolver->requests.first().second.all().size(), 1);  // packs and shaders have no dependency step
        rig.resolver->answer({});
        QCOMPARE(dialog->primaryButton()->text(), S("Add 3 items"));
        auto plan = dialog->plan();
        QCOMPARE(plan.countOf(Kind::Mod), 1);
        QCOMPARE(plan.countOf(Kind::ResourcePack), 1);
        QCOMPARE(plan.countOf(Kind::Shader), 1);
        QCOMPARE(plan.packsToTurnOn().size(), 1);
        dialog->turnOnBox()->setChecked(false);
        QVERIFY(!dialog->turnThemOn());
        QVERIFY(dialog->plan().packsToTurnOn().isEmpty());
        QCOMPARE(dialog->plan().count(), 3);

        // before Minecraft 1.13 options.txt cannot say which packs are on: no box
        ReviewRig old;
        old.addPack(S("20"), S("Faithful"));
        const auto oldDialog = old.open(makeTarget(S("old"), S("Old"), S("1.12.2"), ModPlatform::Fabric));
        QVERIFY(!oldDialog->turnOnBox()->isVisibleTo(oldDialog.get()));
        QVERIFY(!oldDialog->turnThemOn());
        QVERIFY(!oldDialog->dependencyBlockVisible());  // no mods: nothing to check
        QCOMPARE(oldDialog->primaryButton()->text(), S("Add 1 resource pack"));
    }

    void rowsThatWillBeSkipped()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Wrong Version"), { ver(S("o1"), S("1.0"), Release::Release, { S("1.20.1") }) });
        rig.addMod(S("2"), S("Wrong Loader"), { ver(S("o2"), S("1.0"), Release::Release, { S("1.21.1") }, ModPlatform::ModLoaderTypes(ModPlatform::Forge)) });
        rig.addPack(S("20"), S("Faithful"));
        auto dialog = rig.open();
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::NoVersion);
        QCOMPARE(dialog->rowState(modKey(S("2"))), CartReviewDialog::RowState::NoVersion);
        QCOMPARE(dialog->rowStatus(modKey(S("1"))), S("No version for 1.21.1 Fabric · will be skipped"));
        QVERIFY(!dialog->versionBox(modKey(S("1")))->isVisibleTo(dialog.get()));
        QCOMPARE(dialog->titleText(), S("Add 3 items"));  // the title counts the cart
        QVERIFY(!dialog->dependencyBlockVisible());       // no mod can be added: nothing to check
        QCOMPARE(dialog->primaryButton()->text(), S("Add 1 resource pack"));
        QVERIFY(dialog->primaryButton()->isEnabled());
        QCOMPARE(dialog->plan().count(), 1);
        QVERIFY(rig.resolver->requests.isEmpty());

        // a target without a loader takes no mods
        ReviewRig vanilla;
        vanilla.addMod(S("1"), S("Sodium"));
        vanilla.addPack(S("20"), S("Faithful"));
        const auto vanillaDialog = vanilla.open(makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes()));
        QCOMPARE(vanillaDialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::NeedsModded);
        QCOMPARE(vanillaDialog->rowStatus(modKey(S("1"))), S("Needs a modded instance · will be skipped"));
        QCOMPARE(vanillaDialog->plan().count(), 1);
        QCOMPARE(vanillaDialog->plan().countOf(Kind::ResourcePack), 1);
        QVERIFY(vanilla.resolver->requests.isEmpty());

        // everything skipped: nothing to add
        ReviewRig none;
        none.addMod(S("1"), S("Sodium"));
        const auto noneDialog = none.open(makeTarget(S("van"), S("Vanilla"), S("1.21.1"), ModPlatform::ModLoaderTypes()));
        QCOMPARE(noneDialog->primaryButton()->text(), S("Add"));
        QVERIFY(!noneDialog->primaryButton()->isEnabled());
    }

    void versionsLoadFailAndRetry()
    {
        ReviewRig rig;
        const auto item = makeMod(S("1"), S("Sodium"));  // carries no versions: the source is asked
        rig.cart.add(Kind::Mod, item);
        const auto dialog = rig.open();
        QCOMPARE(rig.modrinth.versionCalls.size(), 1);
        const quint64 first = rig.modrinth.versionCalls.at(0).generation;
        QVERIFY(first > CartReviewDialog::kGenerationBase);
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Loading);
        QCOMPARE(dialog->rowStatus(modKey(S("1"))), S("Finding versions…"));
        QVERIFY(dialog->isChecking());
        QCOMPARE(dialog->primaryButton()->text(), S("Checking…"));
        QVERIFY(!dialog->primaryButton()->isEnabled());

        emit rig.modrinth.versionsFailed(first, S("offline"));
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Failed);
        QCOMPARE(dialog->rowStatus(modKey(S("1"))), S("Couldn't load versions"));
        QVERIFY(dialog->retryButton(modKey(S("1")))->isVisibleTo(dialog.get()));
        QVERIFY(!dialog->isChecking());
        QVERIFY(!dialog->primaryButton()->isEnabled());

        dialog->retryButton(modKey(S("1")))->click();
        QCOMPARE(rig.modrinth.versionCalls.size(), 2);
        const quint64 second = rig.modrinth.versionCalls.at(1).generation;
        QVERIFY(second != first);
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Loading);
        emit rig.modrinth.versionsFailed(first, S("late"));  // the answer of the replaced request
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Loading);

        withVersions(item, twoVersions());
        emit rig.modrinth.versionsFinished(second, item, {});
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Ready);
        QCOMPARE(dialog->versionBox(modKey(S("1")))->currentText(), S("2.0"));
        QTRY_COMPARE(rig.resolver->requests.size(), 1);  // the dependency run starts once nothing is loading
    }

    // The owner's "No version for Fabric 26.2" for a mod that has 26.2 builds: the section was filtered for another instance (26.3),
    // so the item carried 26.3 versions only. The review asks under the CART target's filter and leaves the section's cache alone.
    void otherSectionFilterIsNotUsedForTheReview()
    {
        ReviewRig rig;
        rig.target = makeTarget(S("t"), S("Target"), S("26.2"), ModPlatform::Fabric);
        VersionFilter section;
        section.mcVersions = { S("26.3") };
        section.loaders = ModPlatform::Fabric;
        rig.sectionFilter = section;
        const auto item = rig.addMod(S("1"), S("JEI"), { ver(S("n1"), S("9.0"), Release::Release, { S("26.3") }) });
        const auto dialog = rig.open();
        QCOMPARE(rig.modrinth.versionCalls.size(), 0);  // the 26.3-only cache is not used
        QCOMPARE(rig.modrinth.versionsForCalls.size(), 1);
        QCOMPARE(rig.modrinth.versionsForFilters.at(0).mcVersions, QStringList{ S("26.2") });
        QCOMPARE(rig.modrinth.versionsForFilters.at(0).loaders, ModPlatform::ModLoaderTypes(ModPlatform::Fabric));
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Loading);

        emit rig.modrinth.versionsForFinished(rig.modrinth.versionsForCalls.at(0).generation, item,
                                              { ver(S("o1"), S("8.0"), Release::Release, { S("26.2") }) });
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Ready);
        QCOMPARE(dialog->versionBox(modKey(S("1")))->currentText(), S("8.0"));
        QCOMPARE(item->pack->versions.size(), 1);  // the section's cache is untouched
        QCOMPARE(item->pack->versions.first().fileId, S("n1"));
        QVERIFY(item->pack->versionsLoaded);
    }

    void sameSectionFilterReusesTheCache()
    {
        ReviewRig rig;
        rig.target = makeTarget(S("t"), S("Target"), S("26.2"), ModPlatform::Fabric);
        rig.addMod(S("1"), S("JEI"), { ver(S("o1"), S("8.0"), Release::Release, { S("26.2") }) });
        const auto dialog = rig.open();
        QCOMPARE(rig.modrinth.versionsForCalls.size(), 0);
        QCOMPARE(rig.modrinth.versionCalls.size(), 0);
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Ready);
    }

    void cacheLoadedForAnEarlierFilterIsAskedAgain()
    {
        // owner's case: JEI's list was loaded while the section was filtered for 26.3; the section (and the cart) now target 26.2
        ReviewRig rig;
        rig.target = makeTarget(S("t"), S("Target"), S("26.2"), ModPlatform::Fabric);
        const auto jei = withVersions(makeMod(S("1"), S("JEI")), { ver(S("n1"), S("9.0"), Release::Release, { S("26.3") }) });
        VersionFilter old;
        old.mcVersions = { S("26.3") };
        old.loaders = ModPlatform::Fabric;
        jei->versionsFilter = old;
        rig.cart.add(Kind::Mod, jei);
        const auto dialog = rig.open();
        QCOMPARE(rig.modrinth.versionCalls.size(), 1);  // the section's filter is the target's: a fresh versions() request
        QCOMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Loading);
        withVersions(jei, { ver(S("o1"), S("8.0"), Release::Release, { S("26.2") }) });
        emit rig.modrinth.versionsFinished(rig.modrinth.versionCalls.at(0).generation, jei, {});
        QTRY_COMPARE(dialog->rowState(modKey(S("1"))), CartReviewDialog::RowState::Ready);
    }

    void resourcePackReviewFilterHasNoLoaders()
    {
        ReviewRig rig;
        rig.target = makeTarget(S("t"), S("Target"), S("26.2"), ModPlatform::Fabric);
        VersionFilter section;
        section.mcVersions = { S("26.3") };
        rig.sectionFilter = section;
        rig.addPack(S("20"), S("Faithful"));
        const auto dialog = rig.open();
        QCOMPARE(rig.modrinth.versionsForCalls.size(), 1);
        QCOMPARE(rig.modrinth.versionsForFilters.at(0).mcVersions, QStringList{ S("26.2") });
        QCOMPARE(rig.modrinth.versionsForFilters.at(0).loaders, ModPlatform::ModLoaderTypes());
    }

    void dependencyRunWaitsForEveryRowToLoad()
    {
        ReviewRig rig;
        const auto slow = makeMod(S("1"), S("Slow"));
        rig.cart.add(Kind::Mod, slow);
        rig.addMod(S("2"), S("Fast"));
        const auto dialog = rig.open();
        QTest::qWait(rig.delayMs * 4);
        QVERIFY(rig.resolver->requests.isEmpty());  // one row still loads
        QVERIFY(dialog->isChecking());
        withVersions(slow, twoVersions());
        emit rig.modrinth.versionsFinished(rig.modrinth.versionCalls.at(0).generation, slow, {});
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QCOMPARE(rig.resolver->requests.first().second.all().size(), 2);
    }

    // The owner's intermittent "Checking dependencies…" forever: the mods' versions arrive first, the due run finds a pack or a
    // shader still loading and steps aside, and the LAST answer is the pack's or the shader's. Only a mod row's answer scheduled the
    // run again, so it never started (reopening the dialog worked because every version was cached by then).
    void dependencyRunStartsWhenAPackOrShaderLoadsLast()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));  // its versions are known: Ready at once
        const auto pack = makePack(S("20"), S("Faithful"));
        rig.cart.add(Kind::ResourcePack, pack);
        const auto shader = makePack(S("30"), S("BSL"));
        rig.cart.add(Kind::Shader, shader);
        const auto dialog = rig.open();
        QCOMPARE(rig.modrinth.versionCalls.size(), 2);
        QTest::qWait(rig.delayMs * 4);  // the run was due and found the pack and the shader loading
        QVERIFY(rig.resolver->requests.isEmpty());
        QCOMPARE(dialog->dependencyStatus(), S("Checking dependencies…"));

        withVersions(pack, { packVer(S("p1"), S("1.0")) });
        emit rig.modrinth.versionsFinished(rig.modrinth.versionCalls.at(0).generation, pack, {});
        QTest::qWait(rig.delayMs * 4);
        QVERIFY(rig.resolver->requests.isEmpty());  // the shader still loads
        QVERIFY(dialog->isChecking());

        withVersions(shader, { packVer(S("s1"), S("1.0")) });
        emit rig.modrinth.versionsFinished(rig.modrinth.versionCalls.at(1).generation, shader, {});
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QCOMPARE(rig.resolver->requests.first().second.all().size(), 1);
        rig.resolver->answer({});
        QVERIFY(!dialog->isChecking());
        QCOMPARE(dialog->dependencyStatus(), S("No other mods needed"));
        QVERIFY(dialog->primaryButton()->isEnabled());
    }

    void dependencyRunStartsWhenTheLastLoadingPackFailsOrGoes()
    {
        {
            ReviewRig rig;
            rig.addMod(S("1"), S("Sodium"));
            const auto pack = makePack(S("20"), S("Faithful"));
            rig.cart.add(Kind::ResourcePack, pack);
            const auto dialog = rig.open();
            QTest::qWait(rig.delayMs * 4);
            QVERIFY(rig.resolver->requests.isEmpty());
            emit rig.modrinth.versionsFailed(rig.modrinth.versionCalls.at(0).generation, S("offline"));
            QTRY_COMPARE(rig.resolver->requests.size(), 1);
            rig.resolver->answer({});
            QVERIFY(!dialog->isChecking());

            // a pack retried after the check: "Checking…" while it loads, and its answer does not need a new run
            dialog->retryButton(S("resourcepack:modrinth:20"))->click();
            QVERIFY(dialog->isChecking());
            withVersions(pack, { packVer(S("p1"), S("1.0")) });
            emit rig.modrinth.versionsFinished(rig.modrinth.versionCalls.at(1).generation, pack, {});
            QVERIFY(!dialog->isChecking());  // the dependency answer is still valid: a pack does not change it
        }
        {
            ReviewRig rig;
            rig.addMod(S("1"), S("Sodium"));
            rig.cart.add(Kind::Shader, makePack(S("30"), S("BSL")));
            const auto dialog = rig.open();
            QTest::qWait(rig.delayMs * 4);
            QVERIFY(rig.resolver->requests.isEmpty());
            dialog->removeButton(S("shader:modrinth:30"))->click();  // the only loading row goes
            QTRY_COMPARE(rig.resolver->requests.size(), 1);
            rig.resolver->answer({});
            QVERIFY(!dialog->isChecking());
        }
    }

    void packsOnlyCartNeverChecksDependencies()
    {
        ReviewRig rig;
        const auto pack = makePack(S("20"), S("Faithful"));
        rig.cart.add(Kind::ResourcePack, pack);
        rig.addShader(S("30"), S("BSL"));
        const auto dialog = rig.open();
        QVERIFY(dialog->isChecking());  // the pack's versions load
        withVersions(pack, { packVer(S("p1"), S("1.0")) });
        emit rig.modrinth.versionsFinished(rig.modrinth.versionCalls.at(0).generation, pack, {});
        QVERIFY(!dialog->isChecking());
        QVERIFY(!dialog->dependencyBlockVisible());
        QVERIFY(dialog->primaryButton()->isEnabled());
        QTest::qWait(rig.delayMs * 4);
        QVERIFY(rig.resolver->requests.isEmpty());
    }

    void synchronousDependencyAnswerSettles()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        rig.addMod(S("2"), S("Lithium"));
        const auto dialog = rig.open();
        rig.resolver->answerOnResolve = true;  // answers inside resolve(), before it returns
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QVERIFY(!dialog->isChecking());
        QCOMPARE(dialog->dependencyStatus(), S("No other mods needed"));
        QVERIFY(dialog->primaryButton()->isEnabled());
    }

    // The safety net: a check that never answers ends in the error state with Retry, and Retry runs it again.
    void silentDependencyCheckTimesOutAndRetries()
    {
        ReviewRig rig;
        rig.dependencyTimeoutMs = 150;
        rig.addMod(S("1"), S("Sodium"));
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QVERIFY(dialog->isChecking());
        QTRY_VERIFY(dialog->dependencyCheckFailed());  // nobody answered
        QVERIFY(!dialog->isChecking());
        QVERIFY(dialog->dependencyStatus().startsWith(S("Couldn't check dependencies")));
        QVERIFY(dialog->dependencyRetryButton()->isVisibleTo(dialog.get()));
        const int cancelsBefore = rig.resolver->cancels;
        rig.resolver->answerFor(0, {});  // a late answer of the abandoned run is dropped
        QVERIFY(dialog->dependencyCheckFailed());

        dialog->dependencyRetryButton()->click();
        QVERIFY(rig.resolver->cancels > cancelsBefore);
        QTRY_COMPARE(rig.resolver->requests.size(), 2);
        QVERIFY(dialog->isChecking());
        rig.resolver->answer({});
        QVERIFY(!dialog->isChecking());
        QCOMPARE(dialog->dependencyStatus(), S("No other mods needed"));
        QTest::qWait(300);  // the watchdog of the answered run is off
        QVERIFY(!dialog->dependencyCheckFailed());
    }

    void removeUpdatesTitleCountAndCart()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        rig.addMod(S("2"), S("Lithium"));
        rig.addMod(S("3"), S("Iris"));
        const auto dialog = rig.open();
        QSignalSpy cartChanged(&rig.cart, &InstallCartModel::changed);
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QCOMPARE(dialog->primaryButton()->text(), S("Add 3 mods"));

        dialog->removeButton(modKey(S("2")))->click();
        QCOMPARE(dialog->rowCount(), 2);
        QVERIFY(!dialog->hasRow(modKey(S("2"))));
        QCOMPARE(dialog->titleText(), S("Add 2 mods"));
        QCOMPARE(rig.cart.count(), 2);  // removed from the cart too
        QVERIFY(!rig.cart.contains(modKey(S("2"))));
        QCOMPARE(cartChanged.count(), 1);
        QVERIFY(dialog->isChecking());  // the dependencies are asked again
        QCOMPARE(dialog->primaryButton()->text(), S("Checking…"));
        QTRY_COMPARE(rig.resolver->requests.size(), 2);
        QCOMPARE(rig.resolver->requests.last().second.all().size(), 2);
        rig.resolver->answer({});
        QCOMPARE(dialog->primaryButton()->text(), S("Add 2 mods"));

        // the last row goes: the dialog closes and the cart is empty
        QSignalSpy rejected(dialog.get(), &QDialog::rejected);
        dialog->removeButton(modKey(S("1")))->click();
        QCOMPARE(dialog->rowCount(), 1);
        dialog->removeButton(modKey(S("3")))->click();
        QCOMPARE(rejected.count(), 1);
        QVERIFY(rig.cart.isEmpty());
    }

    void removingAPackLeavesTheDependencyCheckAlone()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        rig.addPack(S("20"), S("Faithful"));
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        dialog->removeButton(S("resourcepack:modrinth:20"))->click();
        QCOMPARE(dialog->groupTitles(), (QStringList{ S("Mods") }));  // the empty group is gone
        QCOMPARE(dialog->titleText(), S("Add 1 mod"));
        QVERIFY(!dialog->isChecking());
        QCOMPARE(dialog->primaryButton()->text(), S("Add 1 mod"));
        QTest::qWait(rig.delayMs * 4);
        QCOMPARE(rig.resolver->requests.size(), 1);
    }

    void dependenciesAreMergedAndDeduplicated()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        rig.addMod(S("2"), S("Lithium"));
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({ depOf(S("F"), S("Fabric API"), S("Sodium")), depOf(S("F"), S("Fabric API"), S("Lithium")),
                               depOf(S("2"), S("Lithium"), S("Sodium")),  // a cart row: shown once, as the row
                               depOf(S("O"), S("Old Lib"), S("Sodium"), true) });
        QCOMPARE(dialog->dependencyNames(), (QStringList{ S("Fabric API") }));
        QCOMPARE(dialog->dependencyRequiredBy(), (QStringList{ S("Required by Sodium, Lithium") }));
        QCOMPARE(dialog->alreadyInstalledText(), S("Already installed: Old Lib"));
        const auto plan = dialog->plan();
        QCOMPARE(plan.count(), 3);  // two cart mods and one dependency
        QCOMPARE(plan.dependencyCount(), 1);
        QCOMPARE(dialog->titleText(), S("Add 2 mods"));            // the cart
        QCOMPARE(dialog->primaryButton()->text(), S("Add 3 mods"));  // what downloads
        QVERIFY(dialog->primaryButton()->isEnabled());
        QCOMPARE(plan.downloads.last().mod.reason, reasonDependency());
        QCOMPARE(plan.downloads.last().mod.requiredBy, (QStringList{ S("Sodium"), S("Lithium") }));
    }

    void installedDependenciesComeFromTheTarget()
    {
        ReviewRig rig;
        auto versions = twoVersions();
        ModPlatform::Dependency need;
        need.addonId = S("F");
        need.type = ModPlatform::DependencyType(ModPlatform::DependencyTypeValue::REQUIRED);
        versions[0].dependencies = { need };
        rig.addMod(S("1"), S("Sodium"), versions);
        InstalledMod fabricApi;
        fabricApi.key = managedKey(managedPackType(Provider::Modrinth), S("F"));
        fabricApi.name = S("Fabric API");
        rig.installed = { fabricApi };
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QCOMPARE(dialog->alreadyInstalledText(), S("Already installed: Fabric API"));
    }

    void staleDependencyAnswersAreDropped()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        rig.addMod(S("2"), S("Lithium"));
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        const int cancelsBefore = rig.resolver->cancels;

        // a version change: the running check is cancelled, a new one starts after the wait (and only one for two quick changes)
        dialog->versionBox(modKey(S("1")))->setCurrentIndex(1);
        dialog->versionBox(modKey(S("1")))->setCurrentIndex(0);
        dialog->versionBox(modKey(S("1")))->setCurrentIndex(1);
        QVERIFY(rig.resolver->cancels > cancelsBefore);
        QVERIFY(dialog->dependencyPending());
        QVERIFY(dialog->isChecking());
        // the answer of the first run is late: nothing happens
        rig.resolver->answerFor(0, { depOf(S("F"), S("Fabric API"), S("Sodium")) });
        QVERIFY(dialog->dependencyNames().isEmpty());
        QVERIFY(dialog->isChecking());
        QTRY_COMPARE(rig.resolver->requests.size(), 2);
        QTest::qWait(rig.delayMs * 4);
        QCOMPARE(rig.resolver->requests.size(), 2);  // the three quick changes made ONE run
        QVERIFY(rig.resolver->requests.at(1).first != rig.resolver->requests.at(0).first);
        QCOMPARE(rig.resolver->requests.at(1).second.all().at(0).version.fileId.toString(), S("f1"));  // Sodium's new version
        rig.resolver->answerFor(0, { depOf(S("F"), S("Fabric API"), S("Sodium")) });  // still the old token
        QVERIFY(dialog->dependencyNames().isEmpty());
        rig.resolver->answer({ depOf(S("G"), S("Geckolib"), S("Lithium")) });
        QCOMPARE(dialog->dependencyNames(), (QStringList{ S("Geckolib") }));
        QVERIFY(!dialog->isChecking());
    }

    void checkingBlocksEnter()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        const auto dialog = rig.open();
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        QSignalSpy accepted(dialog.get(), &QDialog::accepted);
        QVERIFY(dialog->isChecking());
        QTest::keyClick(dialog.get(), Qt::Key_Return);
        dialog->accept();
        QCOMPARE(accepted.count(), 0);
        QVERIFY(dialog->isVisible());
        rig.resolver->answer({});
        QVERIFY(dialog->canInstall());
        QTest::keyClick(dialog.get(), Qt::Key_Return);
        QCOMPARE(accepted.count(), 1);
    }

    void failedDependencyCheckNeedsConsent()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->fail(S("offline"));
        QVERIFY(dialog->dependencyCheckFailed());
        QCOMPARE(dialog->dependencyStatus(), S("Couldn't check dependencies: offline"));
        QVERIFY(dialog->dependencyRetryButton()->isVisibleTo(dialog.get()));
        QVERIFY(!dialog->primaryButton()->isEnabled());
        QVERIFY(!dialog->canInstall());
        dialog->skipDependenciesBox()->setChecked(true);
        QVERIFY(dialog->canInstall());
        QCOMPARE(dialog->plan().count(), 1);  // the mod alone
        dialog->dependencyRetryButton()->click();
        QTRY_COMPARE(rig.resolver->requests.size(), 2);
        QVERIFY(dialog->isChecking());
        rig.resolver->answer({});
        QVERIFY(dialog->canInstall());
    }

    void dependencyCheckTurnedOff()
    {
        ReviewRig rig;
        rig.dependencyCheck = false;
        rig.addMod(S("1"), S("Sodium"));
        const auto dialog = rig.open();
        QTRY_VERIFY(!dialog->isChecking());
        QVERIFY(rig.resolver->requests.isEmpty());
        QCOMPARE(dialog->dependencyStatus(), S("Dependency check is turned off in Settings"));
        QVERIFY(dialog->canInstall());
        QCOMPARE(dialog->primaryButton()->text(), S("Add 1 mod"));
    }

    void runningTargetBlocksAdd()
    {
        ReviewRig rig;
        rig.running = true;
        rig.addMod(S("1"), S("Sodium"));
        const auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        QCOMPARE(dialog->noteText(), S("Close Fabric Fun to add content"));
        QVERIFY(!dialog->canInstall());
        QVERIFY(!dialog->primaryButton()->isEnabled());
    }

    void updateRowsReadUpdateAndStayInPlace()
    {
        ReviewRig rig;
        rig.installedFile.insert(S("1"), S("old-file"));
        rig.installedFile.insert(S("20"), S("old-pack-file"));
        rig.addMod(S("1"), S("Sodium"));
        rig.addPack(S("20"), S("Faithful"));
        rig.addPack(S("21"), S("Fresh Pack"));
        const auto dialog = rig.open();
        QCOMPARE(dialog->rowStatus(modKey(S("1"))), S("Update → 2.0"));
        dialog->versionBox(modKey(S("1")))->setCurrentIndex(1);
        QCOMPARE(dialog->rowStatus(modKey(S("1"))), S("Update → 1.0"));
        QCOMPARE(dialog->rowStatus(S("resourcepack:modrinth:21")), S("by someone"));  // not installed: no update text
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        rig.resolver->answer({});
        const auto plan = dialog->plan();
        QCOMPARE(plan.downloads.at(0).mod.reason, reasonUpdate());
        QCOMPARE(plan.downloads.at(1).mod.reason, reasonUpdate());       // Faithful: installed
        QCOMPARE(plan.downloads.at(2).mod.reason, reasonStandalone());   // Fresh Pack
        QVERIFY(dialog->turnThemOn());
        QCOMPARE(plan.packsToTurnOn().size(), 1);  // an update keeps its place in the game's list
        QCOMPARE(plan.packsToTurnOn().first().mod.pack->addonId.toString(), S("21"));
    }

    void closingCancelsTheCheckAndKeepsTheCart()
    {
        ReviewRig rig;
        rig.addMod(S("1"), S("Sodium"));
        rig.addMod(S("2"), S("Lithium"));
        auto dialog = rig.open();
        QTRY_COMPARE(rig.resolver->requests.size(), 1);
        const int before = *rig.cancelCounter;
        QSignalSpy rejected(dialog.get(), &QDialog::rejected);
        dialog->cancelButton()->click();
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rig.cart.count(), 2);  // Cancel leaves the cart as it was
        dialog.reset();
        QVERIFY(*rig.cancelCounter > before);  // the check was cancelled with the dialog
    }

    void manyRowsScrollInsteadOfGrowing()
    {
        ReviewRig rig;
        for (int i = 1; i <= 20; ++i) {
            rig.addMod(QString::number(i), S("Mod %1").arg(i));
        }
        const auto dialog = rig.open();
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
        QCOMPARE(dialog->width(), CartReviewDialog::kWidth);
        QVERIFY(dialog->height() < 700);
        QCOMPARE(dialog->titleText(), S("Add 20 mods"));
    }
};

QTEST_MAIN(ZeroInstallCartTest)
#include "ZeroInstallCart_test.moc"
