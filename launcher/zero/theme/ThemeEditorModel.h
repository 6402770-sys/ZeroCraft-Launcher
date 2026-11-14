// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the state of the theme editor (no widgets): the Definition being edited, which main colors the user touched,
// the per-token overrides, the background choice (a picked image waits as a source path until the theme is saved) and the
// derived live spec. The panel (ui/zero/pages/ThemeEditorPanel) shows it and writes to it; ThemeFiles.h saves it.
#pragma once

#include <QObject>
#include <QString>

#include "zero/theme/CustomTheme.h"

namespace Zero::Custom {

/// The three colors the editor exposes as rows (the surface is derived; it can only be overridden under "All colors").
enum class MainColor { Accent, Background, Text };

class ThemeEditorModel : public QObject {
    Q_OBJECT
   public:
    /// Id of the live spec of a theme that is not saved yet (never registered anywhere).
    static constexpr const char* kNewThemeId = "zero-custom-new-theme";

    explicit ThemeEditorModel(QObject* parent = nullptr);

    /// A new Obsidian theme called `name`.
    void beginNew(const QString& name);
    /// Edits the theme stored in `folder` (`def` = its loaded definition, `themeId` = its registered id).
    void beginEdit(const Definition& def, const QString& folder, const QString& themeId);

    bool isNew() const { return m_folder.isEmpty(); }
    const QString& folder() const { return m_folder; }
    /// The id of the live spec: the theme's own when editing, kNewThemeId otherwise.
    QString themeId() const;

    // ---- name / base / corners
    const QString& name() const { return m_def.name; }
    void setName(const QString& name);
    /// false for an empty / whitespace-only name (Save is disabled).
    bool nameIsValid() const { return !m_def.name.simplified().isEmpty(); }
    Base base() const { return m_def.base; }
    /// Re-derives from `base`: colors the user did not touch take the new base's colors, touched ones are kept, corners go back to
    /// the base's default. Overrides and the background stay.
    void setBase(Base base);
    CornerStyle corners() const { return m_def.corners; }
    void setCorners(CornerStyle corners);

    // ---- main colors
    /// The effective color (what the live spec uses).
    QColor color(MainColor which) const;
    bool touched(MainColor which) const { return m_touched[int(which)]; }
    /// Sets the color and marks it touched (`touched` = false puts it back as untouched: the color dialog's Cancel).
    void setColor(MainColor which, const QColor& color, bool touched = true);

    // ---- all tokens (overrideKeys())
    /// The effective color of a token (derived, or the override).
    QColor tokenColor(const QString& key) const;
    /// True when the user set this token by hand (an override; for "surface" also a surface stored in the definition).
    bool isOverridden(const QString& key) const;
    /// Sets a token. bg / accent / text are the main colors (same as setColor); the others are overrides, "surface" is stored as the
    /// definition's surface (so the borders derive from it).
    void setTokenColor(const QString& key, const QColor& color);
    /// Clears an override: the derived value shows again. No-op for tokens that are not overridden.
    void resetToken(const QString& key);
    /// The color of token `key` in an already derived `spec` (opaque; invalid for an unknown key): for loops over every token.
    static QColor specColor(const ThemeSpec& spec, const QString& key);
    /// Human name of a token ("surfaceHover" -> "Surface hover").
    static QString tokenLabel(const QString& key);

    // ---- background
    enum class BackgroundChoice { None, Image, Video };
    /// What the segmented control shows.
    BackgroundChoice backgroundChoice() const;
    /// An image is available: picked in this session, or stored in the theme.
    bool hasImage() const;
    /// A video is available: picked in this session, or stored in the theme.
    bool hasVideo() const;
    /// File name shown in the image row ("background.png", or the picked file's name); empty without an image.
    QString imageName() const;
    /// Same for the video row.
    QString videoName() const;
    /// Path of the picked background (image or video, whichever is chosen) that is not saved yet; empty when none was picked.
    const QString& pendingBackground() const { return m_pending; }
    /// The pending file when the chosen background is an image / a video; empty otherwise.
    QString pendingImage() const { return m_def.backgroundKind == BackgroundKind::Image ? m_pending : QString(); }
    QString pendingVideo() const { return m_def.backgroundKind == BackgroundKind::Video ? m_pending : QString(); }
    /// Chooses None. The image and the video stay remembered, so choosing one of them again brings it back.
    void selectNoBackground();
    /// Chooses Image / Video. False when none is available yet (the caller asks the user for a file, then calls setImage() /
    /// setVideo()); nothing changes then.
    bool selectImageBackground();
    bool selectVideoBackground();
    /// Uses the file at `path` as the background image / video (it is copied into the theme folder on save).
    void setImage(const QString& path);
    void setVideo(const QString& path);
    double dim() const { return m_def.dim; }
    void setDim(double dim);
    double cardOpacity() const { return m_def.cardOpacity; }
    void setCardOpacity(double opacity);

    // ---- video sound
    /// The background video plays its sound (default off). Only meaningful while the background is a video.
    bool sound() const { return m_def.sound; }
    void setSound(bool on);
    /// 0..1, used while sound is on.
    double volume() const { return m_def.volume; }
    void setVolume(double volume);

    // ---- launcher icon (Default = the ZERO mark, Custom = a file of the theme)
    enum class AssetChoice { Default, Custom };
    AssetChoice iconChoice() const { return hasIcon() ? AssetChoice::Custom : AssetChoice::Default; }
    /// A custom icon is chosen: picked in this session, or stored in the theme.
    bool hasIcon() const { return !m_icon.pending.isEmpty() || !m_def.iconFile.isEmpty(); }
    /// File name shown next to the preview ("icon.png", or the name of the picked file); empty with the default icon.
    QString iconName() const;
    /// The icon to preview: the picked file, else the one stored in the theme folder; empty with the default icon or a missing file.
    QString iconPath() const;
    /// The picked icon that is not copied into the theme folder yet; empty when none was picked.
    const QString& pendingIcon() const { return m_icon.pending; }
    /// Chooses Default. The custom icon stays remembered, so choosing Custom again brings it back.
    void selectDefaultIcon();
    /// Chooses Custom. False when no icon is remembered (the caller asks the user for a file, then calls setIcon()); nothing changes then.
    bool selectCustomIcon();
    /// Uses the file at `path` as the icon (copied into the theme folder on save).
    void setIcon(const QString& path);

    // ---- UI font (Default = the ZERO font, Custom = a file of the theme); same rules as the icon
    AssetChoice fontChoice() const { return hasFont() ? AssetChoice::Custom : AssetChoice::Default; }
    bool hasFont() const { return !m_font.pending.isEmpty() || !m_def.fontFile.isEmpty(); }
    QString fontName() const;
    QString fontPath() const;
    const QString& pendingFont() const { return m_font.pending; }
    void selectDefaultFont();
    bool selectCustomFont();
    void setFont(const QString& path);

    // ---- results
    /// The definition to save: colors filled in, card opacity 1.0 without an image, background / icon / font files named for the picked
    /// ones (ThemeFiles::saveTheme finishes that part).
    Definition definition() const;
    /// The live spec (what the whole app shows while editing). A picked image previews from its source path.
    ThemeSpec spec() const;
    ReadabilityReport readability() const { return Custom::readability(spec()); }

    /// "#RRGGBB" or "RRGGBB" (any case) -> color; invalid for anything else.
    static QColor parseHex(const QString& text);
    static QString toHex(const QColor& color) { return color.name(QColor::HexRgb).toUpper(); }

   signals:
    /// Anything changed (the panel refreshes its widgets).
    void changed();
    /// Something that changes how the app looks changed (everything but the name): the panel re-applies the live spec.
    void lookChanged();

   private:
    void edited(bool look = true);
    Definition effectiveDefinition() const;
    void begin(const Definition& def, const QString& folder, const QString& themeId);
    bool selectKind(BackgroundKind kind);
    void setSource(BackgroundKind kind, const QString& path);

    Definition m_def;
    QString m_folder;
    QString m_themeId;
    bool m_touched[3] = { false, false, false };
    QString m_pending;  // the picked file of the chosen kind (not copied into the folder yet)
    // What the kind that is not chosen held (index 0 = Image, 1 = Video), to bring it back.
    struct Stash {
        QString pending;
        QString file;
    };
    Stash m_stash[2];

    /// What the icon and the font hold besides the Definition file name: the picked file waiting to be copied, and what Default hid.
    struct Asset {
        QString pending;
        QString stashPending;
        QString stashFile;
    };
    QString nameOf(const Asset& asset, const QString& file) const;
    QString pathOf(const Asset& asset, const QString& file) const;
    void selectDefault(Asset& asset, QString Definition::* file);
    bool selectCustom(Asset& asset, QString Definition::* file);
    void setAsset(Asset& asset, QString Definition::* file, const QString& stem, const QString& path);
    Asset m_icon, m_font;
};

}  // namespace Zero::Custom
