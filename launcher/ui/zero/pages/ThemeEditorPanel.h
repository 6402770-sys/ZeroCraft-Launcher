// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the theme editor: a right-side panel of the Appearance page (docs/zero/SHELL.md, "Theme editor").
//
// Name, base (Obsidian | Minecraft), the three main colors with accent presets and a readability label, "All colors" (every
// token, with Reset for the overridden ones), the launcher icon (Default | Custom), the background (None | Image | Video with Dim and
// Cards sliders, and the video's Sound / Volume), the UI font (Default | Custom) and the corners, with
// Cancel / Save theme pinned at the bottom. Every change re-derives the spec and applies it app-wide (throttled to one apply per
// 120 ms); Cancel / Esc re-applies the theme that was active when the panel opened and writes nothing. The state lives in
// Custom::ThemeEditorModel, the disk and ThemeManager work in ThemeLibrary.
#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "zero/theme/ThemeEditorModel.h"

class QCheckBox;
class QColorDialog;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;

namespace Zero {

class ThemeLibrary;
class ElidedLabel;
namespace Content {
class SegmentedControl;
}

class ThemeEditorPanel : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kMinWidth = 250;
    static constexpr int kDefaultWidth = 300;
    static constexpr int kMaxWidth = 320;
    static constexpr int kLiveMs = 120;  ///< at most one live apply per this many ms

    ThemeEditorPanel(ThemeLibrary* library, QWidget* parent = nullptr);

    /// Opens on a new theme ("My theme", Obsidian).
    void openNew();
    /// Opens on the saved custom theme `themeId`. False when it cannot be read (a toast says why).
    bool openEdit(const QString& themeId);
    bool isOpen() const { return m_open; }
    /// Id of the saved theme being edited; empty for a new theme or while closed.
    QString editingThemeId() const;

    /// Writes the theme, registers, applies and persists it, then closes. False (panel stays open, an error toast shows) on failure
    /// or while the name is empty.
    bool save();
    /// Cancel / Esc: re-applies the theme that was active when the panel opened, closes, writes nothing.
    void cancel();
    /// Closes and re-applies the persisted theme (the edited theme was deleted).
    void discard();
    /// Applies the pending live preview now.
    void flushPreview();
    /// Opens the color dialog of a token (what a swatch click does). Window-modal; it deletes itself when closed.
    QColorDialog* pickColor(const QString& key);

    /// Sizes the panel for a page `pageWidth` wide: 28 %, clamped to 250..320 px.
    void fitTo(int pageWidth);

    Custom::ThemeEditorModel* model() const { return m_model; }

    // Test / automation access.
    QLineEdit* nameEdit() const { return m_name; }
    QPushButton* saveButton() const { return m_save; }
    QPushButton* cancelButton() const { return m_cancel; }
    QToolButton* menuButton() const { return m_menuButton; }
    Content::SegmentedControl* baseControl() const { return m_base; }
    Content::SegmentedControl* backgroundControl() const { return m_background; }
    Content::SegmentedControl* cornersControl() const { return m_corners; }
    QSlider* dimSlider() const { return m_dim; }
    QSlider* cardsSlider() const { return m_cards; }
    QCheckBox* soundCheck() const { return m_sound; }
    QSlider* volumeSlider() const { return m_volume; }
    QWidget* soundBlock() const { return m_soundBlock; }
    Content::SegmentedControl* iconControl() const { return m_iconControl; }
    QWidget* iconBlock() const { return m_iconBlock; }
    QLabel* iconPreview() const { return m_iconPreview; }
    Content::SegmentedControl* fontControl() const { return m_fontControl; }
    QWidget* fontBlock() const { return m_fontBlock; }
    QLabel* fontPreview() const { return m_fontPreview; }
    QPushButton* allColorsButton() const { return m_allToggle; }
    QLabel* readabilityLabel() const { return m_readability; }
    /// Hex field of a main color ("accent", "bg", "text") or, with `allColors`, of any token.
    QLineEdit* hexEdit(const QString& key, bool allColors = false) const;
    QPushButton* resetButton(const QString& key) const;
    QList<QAbstractButton*> presetDots() const { return m_dots; }
    QWidget* imageBlock() const { return m_imageBlock; }
    QWidget* allColorsBlock() const { return m_allBlock; }
    bool livePreviewPending() const { return m_live.isActive(); }
    /// The tokens listed under "All colors": every overrideKeys() entry except the three main colors (accent, bg, text), which have rows above.
    QStringList allColorsKeys() const;
    void setAllColorsExpanded(bool expanded);

   signals:
    /// The panel opened (also on another theme while open) or closed; `editingThemeId()` tells which theme, if any.
    void openChanged(bool open);
    /// The live spec was applied to the app (on open, when the throttle rings, on flushPreview): the page's big preview follows it.
    void previewed(const ThemeSpec& spec);
    /// The theme was saved; `id` is its id.
    void saved(const QString& id);

   protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

   private:
    struct Row {
        QString key;
        QWidget* widget = nullptr;
        QAbstractButton* swatch = nullptr;
        QLabel* label = nullptr;
        QLineEdit* hex = nullptr;
        QPushButton* reset = nullptr;
        QHBoxLayout* layout = nullptr;
    };

    void build();
    Row makeRow(const QString& key, const QString& label, bool withReset, bool isMain, QWidget* parent);
    void begin(const std::function<void()>& loadModel);
    void closePanel();
    void syncFromModel();
    void refreshIcons();
    void scheduleLive();
    void applyLive();
    void applyColor(const QString& key, const QColor& color);
    void hexEdited(const QString& key, bool isMain);
    void hexFinished(const QString& key, bool isMain);
    void chooseImage();
    void chooseVideo();
    void chooseIcon();
    void chooseFont();
    void previewSwatch(const QString& key, const QColor& color);
    void showMenu();

    ThemeLibrary* m_library;
    Custom::ThemeEditorModel* m_model;
    bool m_open = false;
    bool m_syncing = false;
    QString m_activeAtOpen;
    QTimer m_live;

    QLabel* m_title = nullptr;
    QToolButton* m_menuButton = nullptr;
    QLineEdit* m_name = nullptr;
    Content::SegmentedControl* m_base = nullptr;
    Content::SegmentedControl* m_background = nullptr;
    Content::SegmentedControl* m_corners = nullptr;
    QLabel* m_readability = nullptr;
    QPushButton* m_allToggle = nullptr;
    QWidget* m_allBlock = nullptr;
    QWidget* m_imageBlock = nullptr;
    ElidedLabel* m_imageName = nullptr;
    QPushButton* m_imageChange = nullptr;
    QCheckBox* m_sound = nullptr;
    QSlider* m_volume = nullptr;
    QLabel* m_volumeValue = nullptr;
    QWidget* m_soundBlock = nullptr;
    QWidget* m_volumeRow = nullptr;
    Content::SegmentedControl* m_iconControl = nullptr;
    QWidget* m_iconBlock = nullptr;
    QLabel* m_iconPreview = nullptr;
    ElidedLabel* m_iconName = nullptr;
    Content::SegmentedControl* m_fontControl = nullptr;
    QWidget* m_fontBlock = nullptr;
    QLabel* m_fontPreview = nullptr;
    ElidedLabel* m_fontName = nullptr;
    QSlider* m_dim = nullptr;
    QSlider* m_cards = nullptr;
    QLabel* m_dimValue = nullptr;
    QLabel* m_cardsValue = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_save = nullptr;
    QHash<QString, Row> m_main;    // accent, bg, text
    QHash<QString, Row> m_tokens;  // every overrideKeys() entry
    QList<QAbstractButton*> m_dots;
};

}  // namespace Zero
