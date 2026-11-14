// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - which resource packs the game uses and in which order (docs/zero/CONTENT-PACKS.md, "D2 Installed resource packs").
//
// The game keeps that in `<gameRoot>/options.txt`, a line `resourcePacks:["vanilla","file/Faithful 32x.zip","fabric"]` (Minecraft 1.13
// and newer): the array runs bottom to top, the LAST entry wins. This file is the user's, the game rewrites it on exit and it holds every
// setting they have, so the rules are strict:
//   * the file is edited as BYTES: every line but the `resourcePacks` one stays byte for byte, so do the newline style (LF / CRLF), a
//     missing final newline and a BOM; the one line is replaced in place (or appended when the key is missing);
//   * the non-`file/` entries (`vanilla`, `fabric`, `mod_resources`, mod ids) keep their places, the `file/` entries are rewritten in the
//     slots `file/` entries occupied, new ones are appended (= top); entries of files that no longer exist are kept untouched;
//   * a value that is not a JSON array of strings (Minecraft 1.12 and older write another format) is never rewritten;
//   * nothing is written while the game runs (the caller passes the running state, this code has no instance dependency) and a change
//     that changes nothing writes nothing;
//   * every write goes through QSaveFile (a temporary file that replaces the old one when complete);
//   * BEFORE a write the previous `resourcePacks` and `incompatibleResourcePacks` lines (or "absent") are saved to
//     `<instance root>/.zero/pack-order.json` (newest first, at most 20, QSaveFile); undoLast() puts the newest entry back and drops it.
// No widgets and no Prism instance: plain bytes and paths, so the tests run it on temp files.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace Zero {
namespace Content {
namespace Packs {

constexpr int kHistoryCap = 20;

/// "vanilla"
QString vanillaEntry();
/// "file/Faithful 32x.zip" -> true
bool isFileEntry(const QString& entry);
/// "file/Faithful 32x.zip" -> "Faithful 32x.zip" (the entry unchanged when it is not a file entry)
QString fileNameOf(const QString& entry);
/// "Faithful 32x.zip" -> "file/Faithful 32x.zip"
QString fileEntryFor(const QString& fileName);

/// What `options.txt` says about the packs.
struct PackList {
    enum class State {
        Missing,     ///< there is no options.txt
        NoKey,       ///< the file has no `resourcePacks` line
        Ok,          ///< `entries` was read
        Unreadable,  ///< the value is not a JSON array of strings (an old format, a damaged line): never rewritten
    };
    State state = State::Missing;
    QStringList entries;       ///< as stored, bottom to top (the last one wins)
    QStringList incompatible;  ///< `incompatibleResourcePacks`, as stored (informational)

    bool readable() const { return state != State::Unreadable; }
};

/// Reads the bytes of an options.txt (`fileExists` false = there is none).
PackList parseOptions(const QByteArray& raw, bool fileExists);
/// Reads the file (a missing file is State::Missing; a file that cannot be read is State::Unreadable).
PackList readOptions(const QString& optionsPath);

/// The names of the active `file/` packs, the one with the highest priority (top of the game's "Selected" list) first. Duplicates dropped.
QStringList activeFiles(const QStringList& entries);

/// The entries after the `file/` packs became `activeTopFirst` (names, priority order): see the rules at the top. `existing` = the file
/// names that are in the resourcepacks folder; a `file/` entry that is not among them is kept where it is. A name of `activeTopFirst`
/// that is not in `existing` is ignored. Pure.
QStringList mergeOrder(const QStringList& entries, const QStringList& activeTopFirst, const QSet<QString>& existing);

// ---- the bytes of options.txt ---------------------------------------------------------------------------------------------------

/// The content of the last line of `key` ("resourcePacks:[...]", without its line ending); nullopt = no such line.
std::optional<QByteArray> lineOf(const QByteArray& raw, const QByteArray& key);
/// `raw` with the line of `key` replaced by `content` (the whole line, "key:value", no line ending), removed when `content` is nullopt,
/// appended (in the newline style of the file, keeping a missing final newline) when the key has no line. Nothing else changes.
QByteArray setLine(const QByteArray& raw, const QByteArray& key, const std::optional<QByteArray>& content);
/// `["vanilla","file/A.zip"]`
QByteArray encodeEntries(const QStringList& entries);

// ---- history --------------------------------------------------------------------------------------------------------------------

struct HistoryEntry {
    QString id;                                ///< unique, grows with time
    QDateTime time;
    bool fileExisted = true;                   ///< options.txt existed before the change
    std::optional<QByteArray> packsLine;       ///< the `resourcePacks` line before the change; nullopt = there was none
    std::optional<QByteArray> incompatibleLine;
};

QString optionsPathFor(const QString& gameRoot);
QString historyPathFor(const QString& instanceRoot);

/// Newest first; a missing or damaged file is an empty history.
QList<HistoryEntry> readHistory(const QString& historyPath);

enum class Result {
    Ok,
    NoChange,       ///< the order is already that; nothing was written
    Running,        ///< the game runs
    Unreadable,     ///< options.txt has a value this code does not understand: untouched
    NothingToUndo,
    Stale,          ///< undoLast(): the newest history entry is not the one the caller meant
    IoError,        ///< a file could not be read or written; options.txt is untouched or whole
};

struct Outcome {
    Result result = Result::Ok;
    QString id;     ///< writeOrder(): the history entry of this change
    QString error;  ///< IoError: what failed
    bool ok() const { return result == Result::Ok || result == Result::NoChange; }
};

/// Makes the active `file/` packs `activeTopFirst` (see mergeOrder()). `isRunning` is asked before anything is read and again just
/// before the write (the caller's instance may start in between).
Outcome writeOrder(const QString& optionsPath, const QString& historyPath, const QStringList& activeTopFirst, const QSet<QString>& existing,
                   const std::function<bool()>& isRunning);

/// Puts back the lines of the newest history entry and drops it. `expectedId` non-empty: only when that is the newest entry.
Outcome undoLast(const QString& optionsPath, const QString& historyPath, const std::function<bool()>& isRunning,
                 const QString& expectedId = QString());

/// The names of the packs the game sees in a resourcepacks folder: the `.zip` files and the folders (not hidden ones, not a `.zip.disabled`).
QSet<QString> packFilesIn(const QString& resourcePacksDir);

/// Turns `fileName` on at the top (after a download): reads options.txt, keeps the packs that are already on and writes the new order with
/// writeOrder() (so history, the running refusal and the atomic write are the same). `existing` = packFilesIn() when empty.
Outcome turnOnAtTop(const QString& optionsPath, const QString& historyPath, const QString& resourcePacksDir, const QString& fileName,
                    const std::function<bool()>& isRunning);

/// The order `turnOnManyAtTop` writes: `files` (cart order: the first is the highest priority) on top, then the packs that are already
/// on and not among `files` and exist, in their order. Names that are not in `existing` and repeated names are dropped. Pure.
QStringList orderAfterTurningOn(const QStringList& files, const QStringList& activeTopFirst, const QSet<QString>& existing);

/// turnOnAtTop() for several packs (the install cart): ONE writeOrder() call, so one history entry and one Undo. `fileNames` in cart
/// order (the first ends up highest). Files that are not in the folder are ignored; none present = IoError and nothing is written.
Outcome turnOnManyAtTop(const QString& optionsPath, const QString& historyPath, const QString& resourcePacksDir, const QStringList& fileNames,
                        const std::function<bool()>& isRunning);

/// Can the Minecraft version of an instance use this? Minecraft 1.13 and newer (the `resourcePacks` JSON array); older versions (the
/// versions the format started at: 1.12.2 and below, b1.x, a1.x, ...) can not. A version that cannot be told counts as capable: the
/// value of the line is still checked before anything is written.
bool supportsOrdering(const QString& mcVersion);

}  // namespace Packs
}  // namespace Content
}  // namespace Zero
