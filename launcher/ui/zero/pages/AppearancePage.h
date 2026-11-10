// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Appearance page: theme picker with a live preview, and the motion setting. See docs/zero/SHELL.md.
#pragma once

#include <QList>
#include <QWidget>
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/shell/ShellPage.h"

class QButtonGroup;
class QLabel;
class QPushButton;

namespace Zero {

class ThemePreview;
class ThemeCard;

class AppearancePage : public ShellPage {
    Q_OBJECT
   public:
    explicit AppearancePage(QWidget* parent = nullptr);

    QString title() const override { return tr("Appearance"); }
    QString subtitle() const override { return tr("Themes and motion"); }

    /// Id of the theme card currently selected (not necessarily applied).
    QString selectedThemeId() const { return m_selectedId; }
    /// Selects a theme card by id: the preview crossfades to it. Nothing is applied.
    void selectTheme(const QString& id);

   signals:
    /// The user pressed "Apply theme". MainWindow applies and persists it.
    void applyThemeRequested(const QString& themeId);
    /// The user asked for Prism's own appearance options.
    void moreOptionsRequested();

   private:
    void buildThemeCards();
    void syncCurrent();       // current theme id changed elsewhere
    void syncMotion();        // segmented control + caption from the ZeroMotion setting
    void refreshApplyButton();
    QString currentThemeId() const;

    ThemePreview* m_preview;
    QList<ThemeCard*> m_cards;
    QWidget* m_cardRow;
    QPushButton* m_apply;
    QString m_selectedId;
    QString m_currentId;
    QButtonGroup* m_motionGroup;
    QLabel* m_motionCaption;
};

}  // namespace Zero
