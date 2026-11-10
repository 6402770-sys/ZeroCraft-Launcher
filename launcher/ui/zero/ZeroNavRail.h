// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - left navigation rail.
#pragma once

#include <QHash>
#include <QPropertyAnimation>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QVBoxLayout;

namespace Zero {

class NavItem;

/// Navigation rail: 220 px wide (64 px collapsed), `surface` background, 1px right border.
/// Layout (top to bottom): ZERO wordmark, items added before the first addStretch(), a stretch, the bottom widget
/// (account chip), then items added after the stretch (e.g. Settings). Items are 36 px tall; the active item has an
/// `elevated` pill and a 2 px `accent` indicator at the rail's left edge.
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

    void setCurrent(const QString& id);
    QString current() const { return m_current; }
    void setItemVisible(const QString& id, bool visible);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    int railWidth() const { return width(); }
    void setRailWidth(int w);

   signals:
    void currentChanged(const QString& id);
    void collapsedChanged(bool collapsed);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    void ensureBottomHolder();
    void refreshWordmark();

    QVBoxLayout* m_layout;
    QLabel* m_wordmark;
    QLabel* m_caption;
    QWidget* m_bottomHolder = nullptr;
    QVBoxLayout* m_bottomLayout = nullptr;
    QHash<QString, NavItem*> m_items;
    QString m_current;
    bool m_collapsed = false;
    bool m_hasStretch = false;
    QPropertyAnimation* m_anim;
};

}  // namespace Zero
