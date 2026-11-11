// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - descriptors the controller (MainWindow) hands to the command palette and the shortcut sheet.
#pragma once

#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

#include "zero/palette/CommandMatcher.h"

namespace Zero {

/// One runnable thing. The palette never knows how to do it: `run` / `runSecondary` call existing launcher actions or
/// APIs. A command that is not currently possible must simply not be listed.
struct PaletteCommand {
    QString id;  ///< stable, e.g. "instance:<id>", "nav:library", "action:new-instance"
    Palette::Kind kind = Palette::Kind::Action;
    QString title;
    QString subtitle;  ///< caption after the title ("1.20.1 · Forge · Played 3 h ago", "Running")
    QStringList keywords;
    QString iconName;  ///< Lucide icon (16 px); ignored when `icon` is set
    QIcon icon;        ///< instance icon (20 px)
    QKeySequence shortcut;
    QString hint;            ///< right side of the selected row when there is no shortcut, e.g. "Play ↵"
    QString badge;           ///< right side, always shown ("Current")
    QString primaryLabel;    ///< footer: "↵ <label>" (default "Run")
    QString secondaryLabel;  ///< footer: "Ctrl+↵ <label>"; shown only when runSecondary is set
    qint64 lastUsedMs = 0;   ///< instances: last launch, drives recency and the "Continue" section
    bool suggested = false;  ///< listed under "Suggested" when the query is empty
    bool favorite = false;   ///< instances: a solid star after the title
    std::function<void()> run;
    std::function<void()> runSecondary;
};

/// Key caps for a shortcut: "Ctrl+N" -> { "Ctrl", "N" }.
QStringList keyCaps(const QKeySequence& sequence);

struct ShortcutEntry {
    QString label;
    QStringList keys;  ///< one key cap per element, e.g. { "Alt", "1-4" }
};

struct ShortcutGroup {
    QString title;
    QList<ShortcutEntry> entries;
};

}  // namespace Zero
