// SPDX-License-Identifier: GPL-3.0-only
// Library 2.0 widgets that do not need the Application: FilterChipBar (chips, selection, reuse, fallback, scrolling arrows),
// FlowLayout (wrapping), Artwork::cover (cover crop).
#include <QAbstractButton>
#include <QAccessible>
#include <QLabel>
#include <QApplication>
#include <QImage>
#include <QListView>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "ui/zero/pages/ArtworkCache.h"
#include "ui/zero/pages/FilterChipBar.h"
#include "InstanceList.h"
#include "ui/zero/pages/FlowLayout.h"
#include "ui/zero/pages/ZeroInstanceDelegate.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/profiles/LibraryFilter.h"

using namespace Zero;
using namespace Zero::LibraryFilter;
Q_DECLARE_METATYPE(Zero::LibraryFilter::Chip)

namespace {

QList<ChipInfo> chipsFor(const QList<Facts>& library)
{
    return buildChips(library);
}

Facts inst(const QString& name, const QString& loader, const QStringList& tags = {})
{
    Facts f;
    f.name = name;
    f.loader = loader;
    f.tags = tags;
    return f;
}

std::unique_ptr<FilterChipBar> makeBar(int width)
{
    auto bar = std::make_unique<FilterChipBar>();
    bar->resize(width, FilterChipBar::kChipHeight);
    bar->show();
    return bar;
}

}  // namespace

class ZeroLibraryTest : public QObject {
    Q_OBJECT
   private slots:
    void clickableCardIsAnAccessibleButton()
    {
        Zero::ZeroCard card;
        card.setClickable(true);
        new QLabel(QStringLiteral("Storage"), &card);
        QSignalSpy clicked(&card, &Zero::ZeroCard::clicked);
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(&card);
        QVERIFY(iface);
        QCOMPARE(iface->role(), QAccessible::Button);
        QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Storage"));  // falls back to the card's first text
        card.setAccessibleName(QStringLiteral("Minecraft"));
        QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Minecraft"));
        QVERIFY(iface->actionInterface());
        iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(clicked.count(), 1);

        Zero::ZeroCard plain;  // not clickable: a group, and "press" does nothing
        QSignalSpy plainClicked(&plain, &Zero::ZeroCard::clicked);
        QAccessibleInterface* plainIface = QAccessible::queryAccessibleInterface(&plain);
        QCOMPARE(plainIface->role(), QAccessible::Grouping);
        if (QAccessibleActionInterface* a = plainIface->actionInterface())
            a->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(plainClicked.count(), 0);
    }

    void initTestCase()
    {
        qRegisterMetaType<Chip>();
        Theme::setCurrent(ThemeSpec::obsidian());
    }

    void chipBarShowsTheChipsInOrder()
    {
        auto bar = makeBar(900);
        bar->setChips(chipsFor({ inst("a", "Fabric", { "pvp" }), inst("b", "") }), Chip{});
        QCOMPARE(bar->labels(), (QStringList{ "All", "Favorites", "Running", "Vanilla", "Fabric", "#pvp" }));
        QVERIFY(bar->isChipActive(Chip{}));
        QVERIFY(!bar->isChipActive(Chip{ Kind::Favorites, {} }));
    }

    void clickingAChipSelectsItAndEmits()
    {
        auto bar = makeBar(900);
        bar->setChips(chipsFor({ inst("a", "Fabric") }), Chip{});
        QSignalSpy spy(bar.get(), &FilterChipBar::chipActivated);
        auto* fabric = static_cast<QAbstractButton*>(bar->chipWidget(Chip{ Kind::Loader, "Fabric" }));
        QVERIFY(fabric);
        fabric->click();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().value<Chip>(), (Chip{ Kind::Loader, "Fabric" }));
        QVERIFY(bar->isChipActive(Chip{ Kind::Loader, "Fabric" }));
        QVERIFY(!bar->isChipActive(Chip{}));  // single select
    }

    void existingChipsKeepTheirWidgetsWhenTheDataChanges()
    {
        auto bar = makeBar(900);
        bar->setChips(chipsFor({ inst("a", "Fabric", { "pvp" }) }), Chip{});
        QWidget* fabric = bar->chipWidget(Chip{ Kind::Loader, "Fabric" });
        QWidget* pvp = bar->chipWidget(Chip{ Kind::Tag, "pvp" });
        bar->setChips(chipsFor({ inst("a", "Fabric", { "pvp", "tech" }), inst("b", "Forge") }), Chip{ Kind::Tag, "pvp" });
        QCOMPARE(bar->chipWidget(Chip{ Kind::Loader, "Fabric" }), fabric);
        QCOMPARE(bar->chipWidget(Chip{ Kind::Tag, "pvp" }), pvp);
        QVERIFY(bar->chipWidget(Chip{ Kind::Tag, "tech" }));
        QVERIFY(bar->chipWidget(Chip{ Kind::Loader, "Forge" }));
        QVERIFY(bar->isChipActive(Chip{ Kind::Tag, "pvp" }));
    }

    void aChipThatDisappearsFallsBackToAll()
    {
        auto bar = makeBar(900);
        bar->setChips(chipsFor({ inst("a", "Fabric", { "pvp" }) }), Chip{ Kind::Tag, "pvp" });
        QVERIFY(bar->isChipActive(Chip{ Kind::Tag, "pvp" }));
        bar->setChips(chipsFor({ inst("a", "Fabric") }), Chip{ Kind::Tag, "pvp" });  // the tag is gone
        QVERIFY(!bar->chipWidget(Chip{ Kind::Tag, "pvp" }));
        QCOMPARE(bar->active(), Chip{});
        QVERIFY(bar->isChipActive(Chip{}));
    }

    void rowScrollsAndShowsArrowsWhenItDoesNotFit()
    {
        QList<Facts> library;
        for (int i = 0; i < 12; ++i)
            library << inst(QStringLiteral("i%1").arg(i), "Fabric", { QStringLiteral("tag-number-%1").arg(i) });
        auto bar = makeBar(300);
        bar->setChips(chipsFor(library), Chip{});
        QTRY_VERIFY(bar->scrollMaximum() > 0);
        QTRY_VERIFY(bar->rightArrowVisible());
        QVERIFY(!bar->leftArrowVisible());  // at the start

        bar->scrollBy(FilterChipBar::kScrollStep);
        QTRY_VERIFY(bar->scrollValue() > 0);
        QTRY_VERIFY(bar->leftArrowVisible());

        // a roomy bar needs no arrows
        bar->resize(5000, FilterChipBar::kChipHeight);
        QTRY_COMPARE(bar->scrollMaximum(), 0);
        QTRY_VERIFY(!bar->leftArrowVisible());
        QTRY_VERIFY(!bar->rightArrowVisible());
    }

    void flowLayoutWrapsItems()
    {
        QWidget host;
        auto* flow = new FlowLayout(&host, 4, 4);
        for (int i = 0; i < 6; ++i) {
            auto* w = new QWidget(&host);
            w->setFixedSize(50, 20);
            flow->addWidget(w);
        }
        QCOMPARE(flow->heightForWidth(1000), 20);              // one row
        QCOMPARE(flow->heightForWidth(110), 3 * 20 + 2 * 4);   // two items per row
        QCOMPARE(flow->heightForWidth(20), 6 * 20 + 5 * 4);    // narrower than an item: one per row
        flow->setGeometry(QRect(0, 0, 110, flow->heightForWidth(110)));
        QCOMPARE(flow->itemAt(2)->geometry().topLeft(), QPoint(0, 24));
        QCOMPARE(flow->itemAt(3)->geometry().topLeft(), QPoint(54, 24));
    }

    void starClickTogglesTheFavoriteWithoutSelecting()
    {
        QStandardItemModel model;
        for (const char* id : { "first", "second" }) {
            auto* item = new QStandardItem(QString::fromLatin1(id));
            item->setData(QString::fromLatin1(id), InstanceList::InstanceIDRole);
            model.appendRow(item);
        }
        QListView view;
        view.setModel(&model);
        ZeroInstanceDelegate delegate;
        view.setItemDelegate(&delegate);
        delegate.attachTo(&view);
        view.resize(220, 420);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QSignalSpy spy(&delegate, &ZeroInstanceDelegate::favoriteToggleRequested);
        const QModelIndex before = view.currentIndex();
        const QRect second = view.visualRect(model.index(1, 0));
        const QPoint star = ZeroInstanceDelegate::starRect(second).center();
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, star);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("second"));
        QCOMPARE(view.currentIndex(), before);  // the click did not select the card

        // a click elsewhere on the card selects it and does not toggle
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, second.center());
        QCOMPARE(spy.size(), 1);
        QCOMPARE(view.currentIndex().row(), 1);

        // press on the star but release away from it: no toggle
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, star);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, second.center());
        QCOMPARE(spy.size(), 1);
    }

    void artworkCoverCropsToTheRequestedBox()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // left half red, right half blue, 400x100
        QImage image(400, 100, QImage::Format_RGB32);
        image.fill(Qt::red);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 200; x < image.width(); ++x)
                image.setPixelColor(x, y, Qt::blue);
        const QString path = dir.path() + "/a.png";
        QVERIFY(image.save(path, "PNG"));

        const QPixmap wide = Artwork::cover(path, QSize(160, 90), 1.0);
        QCOMPARE(wide.size(), QSize(160, 90));
        const QPixmap square = Artwork::cover(path, QSize(50, 50), 2.0);  // 100x100 device pixels
        QCOMPARE(square.size(), QSize(100, 100));
        QCOMPARE(square.devicePixelRatio(), 2.0);
        const QImage cropped = square.toImage();
        QCOMPARE(cropped.pixelColor(5, 50), QColor(Qt::red));    // centre crop of a 400 px wide image keeps both halves
        QCOMPARE(cropped.pixelColor(94, 50), QColor(Qt::blue));
        QVERIFY(Artwork::cover(dir.path() + "/missing.png", QSize(10, 10)).isNull());
    }
};

QTEST_MAIN(ZeroLibraryTest)
#include "ZeroLibrary_test.moc"
