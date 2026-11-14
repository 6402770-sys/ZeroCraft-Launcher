// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the frame pieces the install review dialogs share (ModpackInstallDialog, ModInstallDialog): the header (icon,
// name, author, provider badge), the note row (info icon + caption), the pills of a version, and the footer bar with Cancel and the
// primary action. Nothing here knows what is being installed.
#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include "ui/zero/PillTag.h"
#include "zero/content/ModpackSource.h"

class QLabel;
class QLayout;
class QPushButton;

namespace Zero {
class RemoteImageCache;
}

namespace Zero::Content {

/// The icon of a header or a row: the picture from the RemoteImageCache (already loaded for the card), `glyph` until it is there.
/// 56 px / `radius md` by default (the header); smaller sizes (a dependency row's 24 px) use `radius sm`.
class PackIconWidget : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kHeaderSize = 56;

    PackIconWidget(const QString& url, RemoteImageCache* images, QWidget* parent, const QString& glyph = QStringLiteral("package"),
                   int size = kHeaderSize);
    /// True once the picture is there or can never come (no URL, failed).
    bool settled() const;

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString m_url;
    RemoteImageCache* m_images;
    QString m_glyph;
    int m_size;
};

/// The placeholder of a dependency check on its way: two rows (icon, two text lines) with the soft sweep of ZeroSkeleton (the sweep
/// only runs while the widget is visible and motion is on). Shared by the single mod review and the install cart's review.
class DependencySkeleton : public QWidget {
   public:
    static constexpr int kRowHeight = 32;
    static constexpr int kRowIcon = 24;

    explicit DependencySkeleton(QWidget* parent);
    bool isSweeping() const { return m_timer.isActive(); }

   protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent*) override;

   private:
    QTimer m_timer;
    QElapsedTimer m_clock;
};

namespace Parts {

/// The footer bar: `surface` with a 1 px `border` rule on top (the Create Instance dialog's footer).
class FooterBar : public QWidget {
   public:
    explicit FooterBar(QWidget* parent);

   protected:
    void paintEvent(QPaintEvent*) override;
};

QLabel* overline(const QString& text, QWidget* parent);
/// Hides and schedules deletion of every widget of `layout` (a pill must never stay painted at its old spot).
void clearLayout(QLayout* layout);
/// Release green, beta amber, alpha red.
PillTag::Tone versionTypeTone(ModpackVersion::Type type);

struct HeaderParts {
    QWidget* root = nullptr;
    PackIconWidget* icon = nullptr;
    QLabel* name = nullptr;
    QLabel* author = nullptr;  ///< hidden when the item has no author
    QWidget* pills = nullptr;  ///< holds the provider badge
};
/// Icon, name (accessible name `nameAccessible`), "by <author>" and the provider badge of `item`.
HeaderParts buildHeader(QWidget* parent, const ModpackItem& item, RemoteImageCache* images, const QString& nameAccessible,
                        const QString& glyph);

struct NoteParts {
    QWidget* row = nullptr;
    QLabel* icon = nullptr;  ///< 16 px; the dialog sets the pixmap (it follows the theme)
    QLabel* text = nullptr;  ///< caption, tone "info" (the dialog may change it)
};
NoteParts buildNoteRow(QWidget* parent);

struct FooterParts {
    QWidget* bar = nullptr;
    QPushButton* cancel = nullptr;
    QPushButton* primary = nullptr;  ///< lg primary, default button, starts disabled
};
FooterParts buildFooter(QWidget* parent, const QString& primaryText);

}  // namespace Parts
}  // namespace Zero::Content
