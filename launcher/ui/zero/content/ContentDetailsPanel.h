// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the details panel of the Discover sections (docs/zero/CONTENT.md, "Modpacks" > Details): 400 px, hidden until
// something is selected (the page's grid reflows), slides in 24 px + fade over 200 ms; switching to another item crossfades the
// content over 140 ms. Top to bottom: hero artwork with the icon overlapping it, name, author, pills, stats, the primary action
// (the page decides its text: Install / Update) and a Website button, then the tabs Overview / Gallery / Versions / What's new.
// A tab without data is hidden.
//
// The panel is a dumb view: the page tells it what it knows (showItem, setOverview, setGallery, setVersions ...) and reacts to
// its signals. It never talks to a provider, so answers for an item that is no longer selected never reach it - the page drops
// them by generation before calling in. Pictures go through the RemoteImageCache. The panel creates no dialogs.
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantAnimation>
#include <QUrl>
#include <QWidget>

#include "ContentGalleryViewer.h"
#include "zero/content/ModpackSource.h"

class QGraphicsOpacityEffect;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;
class ProjectDescriptionPage;

namespace Zero {
class RemoteImageCache;
class ZeroSkeleton;
}  // namespace Zero

namespace Zero::Content {

enum class LoadState { None, Loading, Ready, Failed };

/// Opens `url` in the system browser, but only when it is an http or https link (provider-authored HTML is not trusted).
/// Returns whether it was opened.
bool openProviderLink(const QUrl& url);
/// The filter behind openProviderLink().
bool isOpenableProviderLink(const QUrl& url);

/// What the panel's header shows. `key` identifies the item: a new key = another item (crossfade); the same key = refresh.
struct DetailsHeader {
    struct Stat {
        QString icon;  ///< Lucide icon name
        QString text;
        QString tooltip;
    };
    QString key;
    QString name;
    QString author;
    QString iconUrl;
    QString artworkUrl;
    QString providerLabel;  ///< "Modrinth": the first pill
    QStringList pills;      ///< Minecraft range, loaders, tags ...
    QString websiteUrl;     ///< empty hides the Website button
    QList<Stat> stats;      ///< downloads, updated, license ...
    /// Mods: no hero picture, only a slim band behind the overlapping icon (unless the project has a featured gallery image).
    bool compactHero = false;
    QString placeholderIcon = QStringLiteral("package");  ///< the glyph while the icon / artwork is missing ("blocks" for mods)
};

class ContentDetailsPanel : public QWidget {
    Q_OBJECT
   public:
    enum class Tab { Overview, Gallery, Versions, WhatsNew };

    static constexpr int kWidth = 400;
    static constexpr int kSlideMs = 200;
    static constexpr int kSlidePx = 24;
    static constexpr int kSwitchMs = 140;
    static constexpr int kHeroHeight = 136;
    static constexpr int kCompactHeroHeight = 56;
    static constexpr int kIconSize = 56;
    /// Versions are listed in pages so a pack with hundreds of them does not build hundreds of rows at once.
    static constexpr int kVersionPage = 40;

    explicit ContentDetailsPanel(Zero::RemoteImageCache* images, QWidget* parent = nullptr);

    /// The widget the gallery viewer covers (default: the panel's parent chain's top-level content, set by the page).
    void setOverlayHost(QWidget* host);
    ContentGalleryViewer* viewer() const { return m_viewer; }

    // ---- selection ----
    /// Opens the panel on this item: slides in when it was hidden, crossfades when another item was open, refreshes the header when
    /// it is the same item. The tabs start in their loading state (Overview and Versions), Gallery / What's new hidden.
    void showItem(const DetailsHeader& header);
    /// Hides the panel at once (no animation) and forgets the item.
    void clear();
    bool isOpen() const { return !m_key.isEmpty(); }
    QString key() const { return m_key; }

    // ---- data ----
    /// The primary action: text ("Install", "Update"), whether it can be pressed, and a note under the buttons (empty hides it).
    void setPrimaryAction(const QString& text, bool enabled, const QString& note = {});
    /// The install cart's secondary toggle next to the primary action: "Add to cart" / "In cart". `visible` false (an item that cannot
    /// be added to the cart) hides it.
    void setCartAction(bool visible, bool inCart);
    void setOverview(LoadState state, const QString& html = {}, const QString& error = {});
    void setGallery(const QList<GalleryEntry>& entries);
    void setVersions(LoadState state, const QList<ModpackVersion>& versions, const QString& error = {});
    /// The changelog (HTML) of one version, or `failed`; the open row shows it.
    void setChangelog(const QString& versionId, const QString& html, bool failed);
    /// The What's new tab (Technic feed, latest changelog); empty hides the tab.
    void setWhatsNew(const QString& html);

    Tab currentTab() const { return m_tab; }
    void setCurrentTab(Tab tab);
    bool isTabVisible(Tab tab) const;
    QStringList visibleTabLabels() const;

    // ---- access for the page and the tests ----
    QPushButton* primaryButton() const { return m_primary; }
    QPushButton* websiteButton() const { return m_website; }
    QPushButton* cartButton() const { return m_cart; }
    QToolButton* closeButton() const { return m_close; }
    QString nameText() const;
    QString authorText() const;
    QString noteText() const;
    QStringList pillTexts() const;
    QStringList statTexts() const;
    int versionRowCount() const;
    /// Expands / collapses a version row (the same as clicking it); emits changelogRequested() when its changelog is not there yet.
    void toggleVersion(int row);
    bool isVersionExpanded(int row) const;
    QString versionChangelogText(int row) const;
    void openGallery(int index);
    int galleryCount() const { return int(m_gallery.size()); }
    ProjectDescriptionPage* overviewBrowser() const { return m_overview; }
    LoadState overviewState() const { return m_overviewState; }
    LoadState versionsState() const { return m_versionsState; }
    /// A picture arrived or is fading in: repaint the hero, the thumbnails and the viewer.
    void refreshImages();
    bool isSliding() const { return m_slide.state() == QAbstractAnimation::Running; }

   signals:
    void primaryActionRequested();
    /// The "Add to cart" / "In cart" toggle was pressed.
    void cartToggleRequested();
    void websiteRequested();
    void closeRequested();
    /// A Retry button of a failed tab (Overview or Versions) was pressed.
    void retryRequested(Zero::Content::ContentDetailsPanel::Tab tab);
    /// A version row was opened and its changelog is not loaded: the page fetches it and calls setChangelog().
    void changelogRequested(const QString& versionId);
    void tabChanged(Zero::Content::ContentDetailsPanel::Tab tab);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

   private:
    class HeroArt;
    class TabBar;
    class ThumbGrid;
    class VersionRow;

    void applyHeader(const DetailsHeader& header);
    void resetTabs();
    void refreshTabs();
    void rebuildVersions();
    void layoutBody();
    void slideIn();

    Zero::RemoteImageCache* m_images;
    QWidget* m_body;
    HeroArt* m_hero;
    QToolButton* m_close;
    QLabel* m_name;
    QLabel* m_author;
    QWidget* m_pills;
    QWidget* m_stats;
    QPushButton* m_primary;
    QPushButton* m_cart;
    QPushButton* m_website;
    QLabel* m_note;
    TabBar* m_tabs;
    QStackedWidget* m_pages;

    // Overview
    QWidget* m_overviewPage;
    QLabel* m_overviewStatus;
    QPushButton* m_overviewRetry;
    ProjectDescriptionPage* m_overview;
    // Gallery
    QScrollArea* m_galleryScroll;
    ThumbGrid* m_thumbs;
    // Versions
    QScrollArea* m_versionsScroll;
    QWidget* m_versionsBody;
    QVBoxLayout* m_versionsLayout;
    // What's new
    QTextBrowser* m_whatsNew;

    ContentGalleryViewer* m_viewer;
    QGraphicsOpacityEffect* m_effect;
    QVariantAnimation m_slide;

    QString m_key;
    DetailsHeader m_header;
    Tab m_tab = Tab::Overview;
    LoadState m_overviewState = LoadState::None;
    QString m_overviewHtml;
    LoadState m_versionsState = LoadState::None;
    QString m_versionsError;
    QList<ModpackVersion> m_versions;
    int m_versionsShown = 0;
    QList<GalleryEntry> m_gallery;
    QString m_whatsNewHtml;
    int m_bodyOffset = 0;
};

}  // namespace Zero::Content

Q_DECLARE_METATYPE(Zero::Content::ContentDetailsPanel::Tab)
