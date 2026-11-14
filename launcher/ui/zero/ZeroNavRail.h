// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - left navigation rail.
#pragma once

#include <QHash>
#include <QPropertyAnimation>
#include <QRectF>
#include <QVariantAnimation>
#include <QWidget>

class QAbstractButton;
class QVBoxLayout;

namespace Zero {

class NavItem;

/// Navigation rail: 220 px wide (64 px collapsed), `surface` background, 1px right border.
/// Layout (top to bottom): a 60 px header (logo + "ZERO" wordmark, 1 px bottom line), items added before the first
/// addStretch(), a stretch, an optional bottom widget, then items added after the stretch (e.g. Settings). Items are 36 px tall; the active item has an
/// `elevated` pill and a 2 px `accent` indicator at the rail's left edge; both are painted once by the rail and glide
/// between items (200 ms OutQuint) when the current item changes.
class ZeroNavRail : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int railWidth READ railWidth WRITE setRailWidth)
   public:
    explicit ZeroNavRail(QWidget* parent = nullptr);

    /// `iconName` is a Lucide icon name (see ZeroIcons); it is tinted per state (textSecondary / text).
    void addItem(const QString& id, const QString& iconName, const QString& label);
    /// Like addItem(), but the item is an action button: clicking it never becomes the current page. Connect to the
    /// returned button's clicked(); its geometry can be used to anchor a menu.
    QAbstractButton* addActionItem(const QString& id, const QString& iconName, const QString& label);
    void addSpacing(int px);
    /// Adds a stretch; the bottom widget and later items are placed after the first stretch.
    void addStretch();
    /// Widget pinned above the items that follow the first stretch. The rail takes ownership.
    void setBottomWidget(QWidget* widget);

    /// Text appended to the item's tooltip ("Alt+2"); an item with a hint always has a tooltip.
    void setItemShortcutHint(const QString& id, const QString& hint);
    /// Tooltip sentence about what the section is for (pages and action items).
    void setItemDescription(const QString& id, const QString& description);
    void setCurrent(const QString& id);
    QString current() const { return m_current; }
    void setItemVisible(const QString& id, bool visible);
    /// The rail item of a page (null when there is none): what the guided tour highlights.
    QWidget* itemWidget(const QString& id) const;

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    int railWidth() const { return width(); }
    void setRailWidth(int w);

   signals:
    void currentChanged(const QString& id);
    void collapsedChanged(bool collapsed);

   protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void ensureBottomHolder();
    QRectF targetPill() const;   // active item's pill from live geometry (empty if none)
    QRectF currentPill() const;  // what is painted: the target when idle, interpolated while gliding
    void startGlide(const QRectF& from);
    void updatePill();

    QVBoxLayout* m_layout;
    QWidget* m_bottomHolder = nullptr;
    QVBoxLayout* m_bottomLayout = nullptr;
    QHash<QString, NavItem*> m_items;
    QString m_current;
    bool m_collapsed = false;
    bool m_hasStretch = false;
    QPropertyAnimation* m_anim;
    QVariantAnimation* m_glide;  // 0..1 progress of the pill glide (200 ms OutQuint)
    QRectF m_glideFrom;
    QRectF m_lastPill;
    qreal m_glideT = 0.0;
    bool m_gliding = false;
};

}  // namespace Zero
