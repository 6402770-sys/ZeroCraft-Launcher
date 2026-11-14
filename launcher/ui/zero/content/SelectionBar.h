// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the selection bar of the Discover page (docs/zero/SHELL.md, "Discover > Install cart"): 56 px, docked under the
// section stack (never over the cards), visible only while the install cart holds something. Left to right: up to four overlapping
// 26 px item icons (+N when there are more), the summary ("2 mods, 1 shader selected"), a ghost **Clear**, a spacer and the primary
// **Review and add to <instance>** (the name is elided at 160 px). The bar slides up in 160 ms (instant with motion off); the layout
// gets its 56 px at once, only the surface moves.
//
// It never decides what the review is: the primary only emits reviewRequested(); Clear empties the cart.
#pragma once

#include <QPointer>
#include <QString>
#include <QVariantAnimation>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QPushButton;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

class InstallCartModel;

class SelectionBar : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kHeight = 56;
    static constexpr int kIconSize = 26;
    static constexpr int kIconOverlap = 8;
    static constexpr int kMaxIcons = 4;
    static constexpr int kTargetNameMax = 160;  ///< px of the instance name inside the primary button
    static constexpr int kSlideMs = 160;

    /// `images` null = RemoteImageCache::instance().
    SelectionBar(InstallCartModel* cart, Zero::RemoteImageCache* images, QWidget* parent = nullptr);

    /// The instance the review adds to (empty: the button says "Review and add").
    void setTargetName(const QString& name);
    QString targetName() const { return m_targetName; }

    /// The summary text ("3 mods selected").
    QString summaryText() const;
    /// Icons in the stack (at most kMaxIcons) and the "+N" text ("" when none).
    int iconCount() const;
    QString overflowText() const;
    QPushButton* clearButton() const { return m_clear; }
    QPushButton* reviewButton() const { return m_review; }
    bool isSliding() const { return m_slide.state() == QAbstractAnimation::Running; }
    /// The surface's offset from its resting place (kHeight while it waits below the page, 0 when it rests).
    int slideOffset() const { return m_offset; }

   signals:
    void reviewRequested();

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    class Surface;
    class IconStack;

    void sync();
    void placeSurface();
    void setOffset(int offset);

    QPointer<InstallCartModel> m_cart;
    Surface* m_surface;
    IconStack* m_icons;
    QLabel* m_more;
    QLabel* m_summary;
    QPushButton* m_clear;
    QPushButton* m_review;
    QVariantAnimation m_slide;
    QString m_targetName;
    int m_offset = 0;
    bool m_shown = false;
};

}  // namespace Zero::Content
