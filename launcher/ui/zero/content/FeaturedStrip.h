// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the featured strip above the Modpacks list (docs/zero/CONTENT.md, "Modpacks" > Featured strip): a label with
// the provider's own wording ("Featured on CurseForge", "Popular on Modrinth"), up to five hero cards side by side, snap
// scrolling with chevrons. It collapses (height + opacity, 200 ms, instant with motion off) when the list is no longer the
// default view. The strip never invents labels: the page passes the text.
#pragma once

#include <QList>
#include <QSet>
#include <QVariantAnimation>
#include <QWidget>

#include "zero/content/ModpackSource.h"

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QScrollArea;
class QToolButton;

namespace Zero {
class RemoteImageCache;
class ZeroSkeleton;
}  // namespace Zero

namespace Zero::Content {

class ContentHeroCard;

class FeaturedStrip : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kMaxCards = 5;
    static constexpr int kHeaderHeight = 28;
    static constexpr int kGap = 16;
    static constexpr int kCollapseMs = 200;
    static constexpr int kScrollMs = 260;

    explicit FeaturedStrip(Zero::RemoteImageCache* images, QWidget* parent = nullptr);

    /// The strip's heading ("Featured on CurseForge"); hidden while empty.
    void setLabel(const QString& label);
    QString label() const;
    /// At most kMaxCards items; replaces the cards.
    void setItems(const QList<ModpackItemPtr>& items);
    QList<ModpackItemPtr> items() const { return m_items; }
    /// A loading placeholder (the Cinematic skeleton) while there are no cards yet.
    void setLoading(bool loading);
    /// The Installed overlay of the cards whose key is in `keys`.
    void setInstalledKeys(const QSet<QString>& keys);
    /// Highlights the card of the item whose details are open.
    void setSelectedKey(const QString& key);

    /// Collapsed = folded away (a query or a filter is active). Animated unless `animate` is false or motion is off.
    void setCollapsed(bool collapsed, bool animate = true);
    bool isCollapsed() const { return m_collapsed; }
    /// 1 = fully open, 0 = folded away.
    qreal openProgress() const { return m_progress; }
    /// True when there is something to show (cards or a loading placeholder) and the strip is not collapsed.
    bool isShown() const { return m_hasContent && !m_collapsed; }
    /// The height of the fully open strip at its current width.
    int fullHeight() const;
    static int heroWidthFor(int stripWidth);

    QList<ContentHeroCard*> cards() const { return m_cards; }
    QToolButton* previousButton() const { return m_previous; }
    QToolButton* nextButton() const { return m_next; }
    QScrollArea* scrollArea() const { return m_scroll; }
    /// Index of the card at the left edge, and scrolling to a card (animated unless `animate` is false).
    int firstVisibleIndex() const;
    void scrollToIndex(int index, bool animate = true);
    void step(int direction);
    /// A picture arrived or is fading in: repaint the cards.
    void refreshImages();

   signals:
    void activated(const Zero::Content::ModpackItemPtr& item);
    void installRequested(const Zero::Content::ModpackItemPtr& item);

   protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void relayout();
    void applyProgress();
    void updateChevrons();
    int heroWidth() const;

    Zero::RemoteImageCache* m_images;
    QLabel* m_label;
    QToolButton* m_previous;
    QToolButton* m_next;
    QScrollArea* m_scroll;
    QWidget* m_inner;
    Zero::ZeroSkeleton* m_skeleton = nullptr;
    QList<ContentHeroCard*> m_cards;
    QList<ModpackItemPtr> m_items;
    QSet<QString> m_installed;
    QString m_selected;
    QVariantAnimation m_collapse;
    QPropertyAnimation* m_scrollAnim;
    QGraphicsOpacityEffect* m_effect;
    qreal m_progress = 1.0;
    bool m_collapsed = false;
    bool m_hasContent = false;
    bool m_loading = false;
    qint64 m_lastWheel = 0;
};

}  // namespace Zero::Content
