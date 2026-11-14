// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Appearance page: theme picker with a live preview, and the motion setting. See docs/zero/SHELL.md.
#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/shell/ShellPage.h"

class QButtonGroup;
class QGridLayout;
class QLabel;
class QMenu;
class QPixmap;
class QPushButton;
class QResizeEvent;

namespace Zero {

class ThemePreview;
class ThemeCard;
class ThemeEditorPanel;
class ThemeLibrary;

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

    /// The theme editor panel and the custom-theme file actions (tests, dev aids).
    ThemeEditorPanel* themeEditor() const { return m_editor; }
    ThemeLibrary* themeLibrary() const { return m_library; }
    /// Dev aid (ZERO_SCREENSHOT_THEME_EDITOR): opens the editor on "new" or on a custom theme id, optionally followed by
    /// ",minecraft" (start from Minecraft), ",hard" (a text color that is hard to read) and ",all" (All colors expanded).
    void openThemeEditor(const QString& target);
    /// Dev aid (ZERO_SCREENSHOT_THEME_MENU): paints the options menu of the card of `themeId` onto a window grab, where it would open.
    void paintThemeMenu(QPixmap* shot, QWidget* window, const QString& themeId);

   signals:
    /// The user pressed "Apply theme". MainWindow applies and persists it.
    void applyThemeRequested(const QString& themeId);
    /// The user asked for Prism's own appearance options.
    void moreOptionsRequested();

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    /// (Re)builds the grid of theme cards; `select` (or else the current selection, else the applied theme) ends up selected.
    void buildThemeCards(const QString& select = {});
    void refreshRings();
    void showSelectedInPreview();
    void showThemeMenu(const QString& themeId, QWidget* anchor);
    void syncCurrent();       // current theme id changed elsewhere
    void syncMotion();        // segmented control + caption from the ZeroMotion setting
    void refreshApplyButton();
    QString currentThemeId() const;

    ThemePreview* m_preview;
    QList<ThemeCard*> m_cards;
    QWidget* m_cardRow;
    QGridLayout* m_cardGrid;
    ThemeLibrary* m_library = nullptr;
    ThemeEditorPanel* m_editor = nullptr;
    QPushButton* m_apply;
    QString m_selectedId;
    QString m_currentId;
    QButtonGroup* m_motionGroup;
    QLabel* m_motionCaption;
};

}  // namespace Zero
