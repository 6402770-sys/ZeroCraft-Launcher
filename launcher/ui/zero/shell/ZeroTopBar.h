// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the contextual top bar of the main window. See docs/zero/SHELL.md.
#pragma once

#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QHBoxLayout;

namespace Zero {

namespace Motion {
class StateAnimator;
}

class AccountChip;
class ShellPage;
class TopBarDivider;

/// Page title, subtitle and optional clickable breadcrumb parent, drawn on one shared text baseline.
class ChromeTitle : public QWidget {
    Q_OBJECT
   public:
    explicit ChromeTitle(QWidget* parent = nullptr);

    void setContent(const QString& crumb, const QString& title, const QString& subtitle);
    void setSubtitle(const QString& subtitle);
    QString crumb() const { return m_crumb; }
    QString title() const { return m_title; }
    QString subtitle() const { return m_subtitle; }
    /// Hit area of the breadcrumb parent segment in widget coordinates (empty when there is none).
    QRect crumbRect() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

   signals:
    void crumbClicked();

   protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    struct Parts;
    Parts layoutParts() const;
    void refreshAccessibility();

    QString m_crumb;
    QString m_title;
    QString m_subtitle;
    Motion::StateAnimator* m_hover;
    bool m_pressed = false;
    bool m_keyboardFocus = false;
};

/// 60 px, transparent (the shell texture shows through), 1 px `border` line at the bottom. Left: ChromeTitle. Centre: an
/// optional widget slot (empty now, reserved for the universal search). Right: the current page's action widget, a
/// 1 x 24 px divider (only while there are actions) and the AccountChip.
class ZeroTopBar : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kHeight = 60;

    explicit ZeroTopBar(QWidget* parent = nullptr);

    /// Shows `page`'s chrome. With `animate`, title / subtitle / actions crossfade (140 ms, instant with motion off).
    void showPage(ShellPage* page, bool animate);
    /// Re-reads the current page's chrome (its chromeChanged() signal). Only a changed subtitle is swapped in place;
    /// a changed title, breadcrumb or action widget crossfades.
    void refresh();

    /// Reserved slot between the title and the actions; takes ownership. Nothing is shown while it is empty.
    void setCenterWidget(QWidget* widget);
    QWidget* centerWidget() const { return m_center; }

    AccountChip* accountChip() const { return m_chip; }
    ChromeTitle* titleBlock() const { return m_titleBlock; }
    QString titleText() const { return m_titleBlock->title(); }
    QString subtitleText() const { return m_titleBlock->subtitle(); }
    QString breadcrumbText() const { return m_titleBlock->crumb(); }
    /// The action widget currently shown (null = none).
    QWidget* actionsWidget() const { return m_actions; }
    bool dividerVisible() const;

   signals:
    void breadcrumbClicked();
    /// Global position of the chip's bottom-right corner: a popup menu should place its top-right corner just below it.
    void accountMenuRequested(const QPoint& anchor);

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    void apply(ShellPage* page);
    void releaseActions();

    QPointer<ShellPage> m_page;
    ChromeTitle* m_titleBlock;
    QWidget* m_centerHost;
    QPointer<QWidget> m_center;
    QWidget* m_actionsHost;
    QHBoxLayout* m_actionsLayout;
    QPointer<QWidget> m_actions;
    QPointer<QWidget> m_actionsOwner;
    TopBarDivider* m_divider;
    AccountChip* m_chip;
};

}  // namespace Zero
