// SPDX-License-Identifier: GPL-3.0-only
// Create Instance dialog (offscreen, with a fake MetaSource and environment): extractTask() parity for Vanilla / Modded /
// Import, which kind is remembered, Enter, the 1-4 shortcuts, suggested-name truncation, the loading -> failed -> retry
// states of the lists, the type-card focus ring, and the Modded layout (lists start on the same line).
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "InstanceTask.h"
#include "ui/zero/create/ChoiceChips.h"
#include "ui/zero/create/CreateInstanceDialog.h"
#include "ui/zero/create/CreateMetaSource.h"
#include "ui/zero/create/ImportPanel.h"
#include "ui/zero/create/TypeCardRow.h"
#include "ui/zero/create/VersionListView.h"
#include "ui/zero/create/VersionPanel.h"

using namespace Zero::Create;

namespace {

/// In-memory version lists: tests set a list Ready or Failed; request() moves an Idle / Failed list to Loading.
class FakeMetaSource : public MetaSource {
   public:
    using MetaSource::MetaSource;

    Status status(const QString& uid) const override { return m_status.value(uid, Status::Idle); }
    QString errorText(const QString& uid) const override { return m_errors.value(uid); }
    void request(const QString& uid) override
    {
        requested << uid;
        const auto s = status(uid);
        if (s == Status::Ready || s == Status::Loading)
            return;
        m_status[uid] = Status::Loading;
        emit statusChanged(uid);
    }
    QList<Meta::Version::Ptr> versions(const QString& uid) const override
    {
        return status(uid) == Status::Ready ? m_versions.value(uid) : QList<Meta::Version::Ptr>{};
    }

    void setReady(const QString& uid, const QList<Meta::Version::Ptr>& versions)
    {
        m_versions[uid] = versions;
        m_status[uid] = Status::Ready;
        emit statusChanged(uid);
    }
    void setFailed(const QString& uid, const QString& error)
    {
        m_errors[uid] = error;
        m_status[uid] = Status::Failed;
        emit statusChanged(uid);
    }
    /// Quietly (no signal): the list was there before the panel looked.
    void preload(const QString& uid, const QList<Meta::Version::Ptr>& versions)
    {
        m_versions[uid] = versions;
        m_status[uid] = Status::Ready;
    }

    QStringList requested;

   private:
    QHash<QString, Status> m_status;
    QHash<QString, QString> m_errors;
    QHash<QString, QList<Meta::Version::Ptr>> m_versions;
};

Meta::Version::Ptr metaVersion(const QString& uid, const QString& id, const QString& type, qint64 time, const QString& requiresUid = {},
                               const QString& equals = {})
{
    auto v = std::make_shared<Meta::Version>(uid, id);
    v->setType(type);
    v->setTime(time);
    if (!requiresUid.isEmpty()) {
        Meta::RequireSet reqs;
        Meta::Require r;
        r.uid = requiresUid;
        r.equalsVersion = equals;
        reqs.insert(r);
        v->setRequires(reqs, {});
    }
    return v;
}

QList<Meta::Version::Ptr> minecraftVersions()
{
    return { metaVersion(kMinecraftUid, "1.20.1", "release", 1686571200), metaVersion(kMinecraftUid, "1.19.4", "release", 1679000000),
             metaVersion(kMinecraftUid, "1.12.2", "release", 1505700000) };
}

/// Fabric (via intermediary) and Forge for 1.20.1 and 1.19.4; every other list empty.
void fillAll(FakeMetaSource* source, bool quietly)
{
    auto put = [source, quietly](const QString& uid, const QList<Meta::Version::Ptr>& versions) {
        if (quietly)
            source->preload(uid, versions);
        else
            source->setReady(uid, versions);
    };
    put(kMinecraftUid, minecraftVersions());
    put(kIntermediaryUid, { metaVersion(kIntermediaryUid, "1.20.1", "release", 1, kMinecraftUid, "1.20.1"),
                            metaVersion(kIntermediaryUid, "1.19.4", "release", 1, kMinecraftUid, "1.19.4") });
    put(loaderUid(Loader::Fabric), { metaVersion(loaderUid(Loader::Fabric), "0.15.0", "release", 100),
                                     metaVersion(loaderUid(Loader::Fabric), "0.14.0", "release", 50) });
    put(loaderUid(Loader::Forge), { metaVersion(loaderUid(Loader::Forge), "47.1.0", "release", 100, kMinecraftUid, "1.20.1") });
    for (Loader l : { Loader::NeoForge, Loader::Quilt, Loader::LiteLoader })
        put(loaderUid(l), {});
}

struct Harness {
    QTemporaryDir dir;
    QMap<QString, QVariant> settings;
    FakeMetaSource* source = nullptr;
    bool preloadAll = true;
    bool preloadMinecraft = true;

    CreateInstanceEnvironment env()
    {
        CreateInstanceEnvironment e;
        e.groups = { QStringLiteral("Existing") };
        e.instanceDirs = [this]() { return QList<QPair<QString, QString>>{ { QStringLiteral("Default"), dir.path() } }; };
        e.lastUsedDir = dir.path();
        e.readSetting = [this](const QString& key) { return settings.value(key); };
        e.writeSetting = [this](const QString& key, const QVariant& value) { settings.insert(key, value); };
        e.instanceIcon = [](const QString&) { return QIcon(); };
        e.makeMetaSource = [this](QObject* parent) -> MetaSource* {
            source = new FakeMetaSource(parent);
            if (preloadAll)
                fillAll(source, true);
            else if (preloadMinecraft)
                source->preload(kMinecraftUid, minecraftVersions());
            return source;
        };
        return e;
    }
    std::unique_ptr<CreateInstanceDialog> dialog(CreateInstanceDialog::Kind kind, const QString& group = {})
    {
        auto d = std::make_unique<CreateInstanceDialog>(env(), group, kind);
        d->resize(CreateInstanceDialog::kDefaultWidth, CreateInstanceDialog::kDefaultHeight);
        d->show();
        QTest::qWaitForWindowActive(d.get());  // focus and shortcuts need an active window
        return d;
    }
};

std::unique_ptr<InstanceTask> takeTask(CreateInstanceDialog& d)
{
    return std::unique_ptr<InstanceTask>(d.extractTask());
}

}  // namespace

class ZeroCreateInstanceTest : public QObject {
    Q_OBJECT

   private slots:
    // ---- extractTask parity ----

    void extractTask_vanilla()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla, "Existing");
        QVERIFY(d->canCreate());
        QCOMPARE(d->nameField()->text(), QStringLiteral("1.20.1"));
        d->nameField()->setText("  My pack ");
        auto task = takeTask(*d);
        QVERIFY(task);
        QCOMPARE(task->name(), QStringLiteral("My pack"));         // trimmed
        QCOMPARE(task->originalName(), QStringLiteral("1.20.1"));  // the suggestion
        QCOMPARE(task->group(), QStringLiteral("Existing"));
        QCOMPARE(task->targetDir(), h.dir.path());
        QVERIFY(!task->shouldCopyTemplateDir());
    }

    void extractTask_modded()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Modded);
        QVERIFY(d->canCreate());
        QCOMPARE(d->versionPanel()->loader().value(), Loader::Fabric);
        QCOMPARE(d->nameField()->text(), QStringLiteral("1.20.1 Fabric"));
        d->groupBox()->setCurrentText("Brand new");
        auto task = takeTask(*d);
        QVERIFY(task);
        QCOMPARE(task->name(), QStringLiteral("1.20.1 Fabric"));
        QCOMPARE(task->originalName(), QStringLiteral("1.20.1 Fabric"));
        QCOMPARE(task->group(), QStringLiteral("Brand new"));
        QCOMPARE(task->targetDir(), h.dir.path());
    }

    void extractTask_import()
    {
        Harness h;
        QTemporaryDir files;
        QFile pack(files.filePath("Cool Pack.mrpack"));
        QVERIFY(pack.open(QIODevice::WriteOnly));
        pack.write("x");
        pack.close();
        auto d = h.dialog(CreateInstanceDialog::Kind::Import, "Existing");
        QVERIFY(!d->canCreate());
        QVERIFY(!d->extractTask());
        d->importPanel()->setLocalFile(pack.fileName());
        QVERIFY(d->canCreate());
        QCOMPARE(d->nameField()->text(), QStringLiteral("Cool Pack"));
        auto task = takeTask(*d);
        QVERIFY(task);
        QCOMPARE(task->name(), QStringLiteral("Cool Pack"));
        QCOMPARE(task->originalName(), QStringLiteral("Cool Pack"));
        QCOMPARE(task->group(), QStringLiteral("Existing"));
        QCOMPARE(task->targetDir(), h.dir.path());
    }

    void modpackCreatesNothing()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Modpack);
        QVERIFY(!d->canCreate());
        QVERIFY(!d->extractTask());
    }

    // ---- Remembered kind ----

    void rememberedKind_onlyAUserChoiceOfVanillaOrModded()
    {
        {
            // Opened from "Import": nothing chosen, nothing written.
            Harness h;
            auto d = h.dialog(CreateInstanceDialog::Kind::Import);
            d->reject();
            QVERIFY(!h.settings.contains(CreateInstanceDialog::kKindKey));
        }
        {
            // Programmatic setKind is not a user choice either.
            Harness h;
            auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
            d->setKind(CreateInstanceDialog::Kind::Modded);
            d->reject();
            QVERIFY(!h.settings.contains(CreateInstanceDialog::kKindKey));
        }
        {
            // The user picks Modded on the cards: remembered.
            Harness h;
            auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
            QTest::mouseClick(d->typeCards()->card(1), Qt::LeftButton);
            QCOMPARE(d->kind(), CreateInstanceDialog::Kind::Modded);
            d->reject();
            QCOMPARE(h.settings.value(CreateInstanceDialog::kKindKey).toString(), QStringLiteral("modded"));
            QCOMPARE(CreateInstanceDialog::rememberedKind(h.env()), CreateInstanceDialog::Kind::Modded);
        }
        {
            // The user picks Modpack / Import: never written (the previous value stays).
            Harness h;
            h.settings.insert(CreateInstanceDialog::kKindKey, QStringLiteral("modded"));
            auto d = h.dialog(CreateInstanceDialog::Kind::Modded);
            QTest::mouseClick(d->typeCards()->card(2), Qt::LeftButton);
            QCOMPARE(d->kind(), CreateInstanceDialog::Kind::Modpack);
            d->reject();
            QCOMPARE(h.settings.value(CreateInstanceDialog::kKindKey).toString(), QStringLiteral("modded"));
        }
    }

    // ---- Enter ----

    void enter_createsOnlyFromTheNameField()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
        QVERIFY(d->canCreate());

        // Version search: Enter selects the first match and does not create.
        auto* search = d->versionPanel()->searchField();
        search->setFocus();
        QTest::keyClicks(search, "1.19");
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(d->result(), int(QDialog::Rejected));
        QVERIFY(d->isVisible());
        QCOMPARE(d->versionPanel()->minecraftVersion(), QStringLiteral("1.19.4"));

        // Group combo: consumed.
        d->groupBox()->setFocus();
        QTest::keyClick(d->groupBox()->lineEdit(), Qt::Key_Return);
        QVERIFY(d->isVisible());

        // Name field: creates.
        d->nameField()->setFocus();
        QTest::keyClick(d->nameField(), Qt::Key_Return);
        QCOMPARE(d->result(), int(QDialog::Accepted));
    }

    void enter_onNameFieldDoesNothingWhenCreateIsDisabled()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
        d->nameField()->clear();
        QVERIFY(!d->canCreate());
        d->nameField()->setFocus();
        QTest::keyClick(d->nameField(), Qt::Key_Return);
        QVERIFY(d->isVisible());
        QCOMPARE(d->result(), int(QDialog::Rejected));
    }

    void enter_onTheCreateButtonClicksIt()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
        d->createButton()->setFocus();
        QTest::keyClick(d->createButton(), Qt::Key_Return);
        QCOMPARE(d->result(), int(QDialog::Accepted));
    }

    // ---- Digit shortcuts ----

    void digits_chooseAKindOnlyInTheCardRow()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);

        // A version list has focus: "2" is type-ahead, not "Modded".
        d->versionPanel()->minecraftList()->listView()->setFocus();
        QTest::keyClick(d->versionPanel()->minecraftList()->listView(), Qt::Key_2);
        QCOMPARE(d->kind(), CreateInstanceDialog::Kind::Vanilla);

        // The name field: digits are text.
        d->nameField()->setFocus();
        QTest::keyClick(d->nameField(), Qt::Key_3);
        QCOMPARE(d->kind(), CreateInstanceDialog::Kind::Vanilla);

        // A card has focus: 2 chooses Modded, and the focus follows to that card.
        d->nameField()->setFocus();
        d->typeCards()->card(0)->setFocus(Qt::TabFocusReason);
        QTest::keyClick(d->typeCards()->card(0), Qt::Key_2);
        QCOMPARE(d->kind(), CreateInstanceDialog::Kind::Modded);
        QVERIFY(d->typeCards()->card(1)->hasFocus());
        QVERIFY(d->typeCards()->card(1)->isFocusVisible());
    }

    // ---- Type card focus ----

    void typeCards_focusRingIsForTheKeyboard()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
        auto* cards = d->typeCards();

        d->nameField()->setFocus();
        cards->card(0)->setFocus(Qt::TabFocusReason);
        QVERIFY(cards->card(0)->isFocusVisible());

        // A mouse click focuses (and chooses) the card without a ring.
        QTest::mouseClick(cards->card(2), Qt::LeftButton);
        QVERIFY(cards->card(2)->hasFocus());
        QVERIFY(!cards->card(2)->isFocusVisible());
        QCOMPARE(cards->current(), 2);

        // Arrows move the focus with the choice, ring visible.
        QTest::keyClick(cards->card(2), Qt::Key_Right);
        QCOMPARE(cards->current(), 3);
        QVERIFY(cards->card(3)->hasFocus());
        QVERIFY(cards->card(3)->isFocusVisible());

        // A programmatic change moves a card's focus to the chosen card.
        d->setKind(CreateInstanceDialog::Kind::Vanilla);
        QVERIFY(cards->card(0)->hasFocus());
    }

    // ---- Suggested name ----

    void suggestedName_longImportNameIsTruncated()
    {
        Harness h;
        QTemporaryDir files;
        const QString base = QString(200, QLatin1Char('a')) + QStringLiteral(" tail");
        QFile pack(files.filePath(base + ".mrpack"));
        if (!pack.open(QIODevice::WriteOnly))
            QSKIP("cannot create a file with a 205 character name here");
        pack.write("x");
        pack.close();
        auto d = h.dialog(CreateInstanceDialog::Kind::Import);
        d->importPanel()->setLocalFile(pack.fileName());
        QVERIFY(d->importPanel()->source().isImportable());
        QCOMPARE(d->nameField()->text().size(), kMaxNameLength);
        QVERIFY(d->nameField()->text().startsWith(QString(10, QLatin1Char('a'))));
        QVERIFY(d->nameError().isEmpty());
        QVERIFY(d->canCreate());

        // A name the user types keeps its error.
        d->nameField()->setText(QString(kMaxNameLength + 5, QLatin1Char('b')));
        QTest::keyClick(d->nameField(), Qt::Key_X);  // textEdited
        QVERIFY(!d->nameError().isEmpty());
        QVERIFY(!d->canCreate());
        QCOMPARE(d->nameField()->accessibleDescription(), d->nameError());
    }

    // ---- Loading, failure, retry ----

    void lists_loadingFailedRetry()
    {
        Harness h;
        h.preloadAll = false;
        h.preloadMinecraft = false;
        auto d = h.dialog(CreateInstanceDialog::Kind::Modded);
        auto* panel = d->versionPanel();
        QVERIFY(h.source->requested.contains(kMinecraftUid));
        QVERIFY(!panel->isSettled());
        QVERIFY(!d->canCreate());

        for (const QString& uid : moddedMetaUids())
            if (uid != kMinecraftUid)
                h.source->setReady(uid, {});
        QVERIFY(!panel->isSettled());  // Minecraft is still loading
        h.source->setFailed(kMinecraftUid, "offline");
        QVERIFY(panel->isSettled());
        QVERIFY(!d->canCreate());
        // The loader pane does not tell the user to choose a version next to the Minecraft error.
        QVERIFY(!panel->loaderList()->emptyText().contains("Choose a Minecraft version first"));
        QVERIFY(!panel->loaderList()->errorText().contains("Choose a Minecraft version first"));

        // Retry (the Minecraft list's own retry request): Loading again, then Ready.
        h.source->request(kMinecraftUid);
        QVERIFY(!panel->isSettled());
        fillAll(h.source, false);
        QVERIFY(panel->isSettled());
        QVERIFY(d->canCreate());
        QCOMPARE(d->nameField()->text(), QStringLiteral("1.20.1 Fabric"));
    }

    void modded_alreadyReadyListsAreUsed()
    {
        // The loader lists were Ready before Modded was shown (switch from Vanilla): the catalog must know them.
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
        d->setKind(CreateInstanceDialog::Kind::Modded);
        QVERIFY(d->versionPanel()->loader().has_value());
        QVERIFY(d->canCreate());
    }

    // ---- Layout ----

    void vanilla_chipsAndSearchShareARow()
    {
        Harness h;
        auto d = h.dialog(CreateInstanceDialog::Kind::Vanilla);
        QApplication::processEvents();
        auto* panel = d->versionPanel();
        QVERIFY(!panel->loaderList()->isVisible());
        QVERIFY(panel->minecraftList()->width() > d->width() / 2 - 100);  // the list uses the whole column
        QVERIFY(panel->searchField()->geometry().left() >= panel->filterChips()->geometry().right());
        QVERIFY(panel->filterChips()->height() <= ChoiceChipRow::kChipHeight + 2);  // one line
    }

    void modded_listsStartOnTheSameLine()
    {
        for (const QSize size : { QSize(CreateInstanceDialog::kDefaultWidth, CreateInstanceDialog::kDefaultHeight),
                                  QSize(CreateInstanceDialog::kMinWidth, CreateInstanceDialog::kMinHeight) }) {
            Harness h;
            auto d = h.dialog(CreateInstanceDialog::Kind::Modded);
            d->resize(size);
            QApplication::processEvents();
            auto* mc = d->versionPanel()->minecraftList();
            auto* loader = d->versionPanel()->loaderList();
            QVERIFY(mc->isVisible() && loader->isVisible());
            QCOMPARE(mc->mapTo(d.get(), QPoint(0, 0)).y(), loader->mapTo(d.get(), QPoint(0, 0)).y());
            // The chips take at most two lines, the search sits under them (not beside).
            auto* chips = d->versionPanel()->filterChips();
            QVERIFY2(chips->height() <= 2 * ChoiceChipRow::kChipHeight + 16, qPrintable(QString::number(chips->height())));
            QVERIFY(d->versionPanel()->searchField()->geometry().top() >= chips->geometry().bottom());
        }
    }
};

QTEST_MAIN(ZeroCreateInstanceTest)

#include "ZeroCreateInstance_test.moc"
