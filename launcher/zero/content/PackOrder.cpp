// SPDX-License-Identifier: GPL-3.0-only
#include "PackOrder.h"

#include "zero/content/PackRollback.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace Zero {
namespace Content {
namespace Packs {

namespace {

const QByteArray kPacksKey = QByteArrayLiteral("resourcePacks");
const QByteArray kIncompatibleKey = QByteArrayLiteral("incompatibleResourcePacks");
const QByteArray kBom = QByteArrayLiteral("\xEF\xBB\xBF");
constexpr int kHistoryVersion = 1;

/// One line of the file: [start, contentEnd) is the text, [contentEnd, end) its line ending ("" for an unterminated last line).
struct Span {
    qsizetype start = 0;
    qsizetype contentEnd = 0;
    qsizetype end = 0;
};

QList<Span> spansOf(const QByteArray& raw)
{
    QList<Span> out;
    qsizetype start = 0;
    while (start < raw.size()) {
        const qsizetype nl = raw.indexOf('\n', start);
        Span span;
        span.start = start;
        if (nl < 0) {
            span.contentEnd = span.end = raw.size();
            out.append(span);
            break;
        }
        span.end = nl + 1;
        span.contentEnd = (nl > start && raw.at(nl - 1) == '\r') ? nl - 1 : nl;
        out.append(span);
        start = nl + 1;
    }
    return out;
}

/// Where the text of a line starts (past a byte order mark on the very first line).
qsizetype textStart(const QByteArray& raw, const Span& span)
{
    return span.start == 0 && raw.startsWith(kBom) ? 3 : span.start;
}

/// The index into `spans` of the last line that starts with `key` and a colon; -1 = none.
int findLine(const QByteArray& raw, const QList<Span>& spans, const QByteArray& key)
{
    const QByteArray prefix = key + ':';
    for (int i = int(spans.size()) - 1; i >= 0; --i) {
        const qsizetype from = textStart(raw, spans.at(i));
        if (spans.at(i).contentEnd - from >= prefix.size() && raw.mid(from, prefix.size()) == prefix) {
            return i;
        }
    }
    return -1;
}

QByteArray eolOf(const QByteArray& raw, const QList<Span>& spans)
{
    for (const Span& span : spans) {
        if (span.end > span.contentEnd) {
            return raw.mid(span.contentEnd, span.end - span.contentEnd);
        }
    }
    return QByteArrayLiteral("\n");
}

bool readFile(const QString& path, QByteArray* out, bool* exists, QString* error)
{
    QFile file(path);
    *exists = file.exists();
    if (!*exists) {
        out->clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Couldn't read %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    *out = file.readAll();
    return true;
}

bool writeAtomic(const QString& path, const QByteArray& bytes, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QObject::tr("Couldn't write %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

PackList::State stateOfValue(const QByteArray& value, QStringList* entries)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(value.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return PackList::State::Unreadable;
    }
    QStringList out;
    for (const QJsonValue& item : doc.array()) {
        if (!item.isString()) {
            return PackList::State::Unreadable;
        }
        out.append(item.toString());
    }
    *entries = out;
    return PackList::State::Ok;
}

// ---- history file ----------------------------------------------------------------------------------------------------------------

void putLine(QJsonObject& object, const QString& key, const std::optional<QByteArray>& line)
{
    if (!line) {
        object.insert(key, QJsonValue::Null);
        return;
    }
    const QString text = QString::fromUtf8(*line);
    if (text.toUtf8() == *line) {
        object.insert(key, text);
    } else {
        object.insert(key + QStringLiteral("B64"), QString::fromLatin1(line->toBase64()));  // bytes that are not UTF-8 survive
    }
}

std::optional<QByteArray> getLine(const QJsonObject& object, const QString& key)
{
    const QJsonValue plain = object.value(key);
    if (plain.isString()) {
        return plain.toString().toUtf8();
    }
    const QJsonValue encoded = object.value(key + QStringLiteral("B64"));
    if (encoded.isString()) {
        return QByteArray::fromBase64(encoded.toString().toLatin1());
    }
    return std::nullopt;
}

bool writeHistory(const QString& path, const QList<HistoryEntry>& entries, QString* error)
{
    QJsonArray array;
    for (const HistoryEntry& entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), entry.id);
        object.insert(QStringLiteral("time"), entry.time.toUTC().toString(Qt::ISODate));
        object.insert(QStringLiteral("fileExisted"), entry.fileExisted);
        putLine(object, QStringLiteral("packs"), entry.packsLine);
        putLine(object, QStringLiteral("incompatible"), entry.incompatibleLine);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kHistoryVersion);
    root.insert(QStringLiteral("entries"), array);
    return writeAtomic(path, QJsonDocument(root).toJson(QJsonDocument::Indented), error);
}

}  // namespace

QString vanillaEntry()
{
    return QStringLiteral("vanilla");
}

bool isFileEntry(const QString& entry)
{
    return entry.startsWith(QLatin1String("file/"));
}

QString fileNameOf(const QString& entry)
{
    return isFileEntry(entry) ? entry.mid(5) : entry;
}

QString fileEntryFor(const QString& fileName)
{
    return QStringLiteral("file/") + fileName;
}

PackList parseOptions(const QByteArray& raw, bool fileExists)
{
    PackList list;
    if (!fileExists) {
        list.state = PackList::State::Missing;
        return list;
    }
    const QList<Span> spans = spansOf(raw);
    const int index = findLine(raw, spans, kPacksKey);
    if (index < 0) {
        list.state = PackList::State::NoKey;
    } else {
        const Span& span = spans.at(index);
        const qsizetype valueStart = textStart(raw, span) + kPacksKey.size() + 1;
        list.state = stateOfValue(raw.mid(valueStart, span.contentEnd - valueStart), &list.entries);
    }
    const int other = findLine(raw, spans, kIncompatibleKey);
    if (other >= 0) {
        const Span& span = spans.at(other);
        const qsizetype valueStart = textStart(raw, span) + kIncompatibleKey.size() + 1;
        QStringList ignored;
        if (stateOfValue(raw.mid(valueStart, span.contentEnd - valueStart), &ignored) == PackList::State::Ok) {
            list.incompatible = ignored;
        }
    }
    return list;
}

PackList readOptions(const QString& optionsPath)
{
    QByteArray raw;
    bool exists = false;
    QString error;
    if (!readFile(optionsPath, &raw, &exists, &error)) {
        PackList list;
        list.state = PackList::State::Unreadable;
        return list;
    }
    return parseOptions(raw, exists);
}

QStringList activeFiles(const QStringList& entries)
{
    QStringList out;
    for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
        if (isFileEntry(*it) && !out.contains(fileNameOf(*it))) {
            out.append(fileNameOf(*it));
        }
    }
    return out;
}

QStringList mergeOrder(const QStringList& entries, const QStringList& activeTopFirst, const QSet<QString>& existing)
{
    // bottom to top, only the packs the folder has
    QStringList wanted;
    for (auto it = activeTopFirst.crbegin(); it != activeTopFirst.crend(); ++it) {
        if (existing.contains(*it) && !wanted.contains(*it)) {
            wanted.append(*it);
        }
    }
    const QSet<QString> wantedSet(wanted.begin(), wanted.end());

    QStringList result;
    QList<int> slotIndexes;
    QSet<QString> seen;
    for (const QString& entry : entries) {
        if (!isFileEntry(entry)) {
            result.append(entry);  // vanilla, fabric, mod packs: where they are
            continue;
        }
        const QString name = fileNameOf(entry);
        if (!existing.contains(name)) {
            result.append(entry);  // the file is gone: the game drops the entry itself, this code leaves it alone
        } else if (!wantedSet.contains(name) || seen.contains(name)) {
            continue;  // turned off (or a duplicate)
        } else {
            seen.insert(name);
            slotIndexes.append(int(result.size()));
            result.append(entry);
        }
    }
    QStringList carried;
    for (const QString& name : wanted) {
        if (seen.contains(name)) {
            carried.append(name);
        }
    }
    for (int i = 0; i < slotIndexes.size(); ++i) {
        result[slotIndexes.at(i)] = fileEntryFor(carried.at(i));
    }
    for (const QString& name : wanted) {
        if (!seen.contains(name)) {
            result.append(fileEntryFor(name));  // new: the top
        }
    }
    return result;
}

std::optional<QByteArray> lineOf(const QByteArray& raw, const QByteArray& key)
{
    const QList<Span> spans = spansOf(raw);
    const int index = findLine(raw, spans, key);
    if (index < 0) {
        return std::nullopt;
    }
    const Span& span = spans.at(index);
    const qsizetype from = textStart(raw, span);
    return raw.mid(from, span.contentEnd - from);
}

QByteArray setLine(const QByteArray& raw, const QByteArray& key, const std::optional<QByteArray>& content)
{
    const QList<Span> spans = spansOf(raw);
    const int index = findLine(raw, spans, key);
    if (index >= 0) {
        const Span& span = spans.at(index);
        if (content) {
            QByteArray out = raw;
            const qsizetype from = textStart(raw, span);
            out.replace(from, span.contentEnd - from, *content);
            return out;
        }
        QByteArray out = raw;
        if (span.end == span.contentEnd && index > 0) {
            // the last line has no line ending: the one before it must not keep its own, or the file would end with a newline it did not have
            out.remove(spans.at(index - 1).contentEnd, span.end - spans.at(index - 1).contentEnd);
        } else {
            out.remove(span.start, span.end - span.start);
            if (span.start == 0 && raw.startsWith(kBom) && !out.startsWith(kBom)) {
                out.prepend(kBom);  // removing the first line must not lose the byte order mark of the file
            }
        }
        return out;
    }
    if (!content) {
        return raw;
    }
    if (raw.isEmpty()) {
        return *content + QByteArrayLiteral("\n");
    }
    const QByteArray eol = eolOf(raw, spans);
    if (raw.endsWith('\n')) {
        return raw + *content + eol;
    }
    return raw + eol + *content;  // the file had no final newline: neither has it now
}

QByteArray encodeEntries(const QStringList& entries)
{
    return QJsonDocument(QJsonArray::fromStringList(entries)).toJson(QJsonDocument::Compact);
}

QString optionsPathFor(const QString& gameRoot)
{
    return QDir(gameRoot).filePath(QStringLiteral("options.txt"));
}

QString historyPathFor(const QString& instanceRoot)
{
    return QDir(instanceRoot).filePath(QStringLiteral(".zero/pack-order.json"));
}

QList<HistoryEntry> readHistory(const QString& historyPath)
{
    QList<HistoryEntry> out;
    QFile file(historyPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return out;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return out;
    }
    for (const QJsonValue& value : doc.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        HistoryEntry entry;
        entry.id = object.value(QStringLiteral("id")).toString();
        if (entry.id.isEmpty()) {
            continue;
        }
        entry.time = QDateTime::fromString(object.value(QStringLiteral("time")).toString(), Qt::ISODate);
        entry.fileExisted = object.value(QStringLiteral("fileExisted")).toBool(true);
        entry.packsLine = getLine(object, QStringLiteral("packs"));
        entry.incompatibleLine = getLine(object, QStringLiteral("incompatible"));
        out.append(entry);
        if (out.size() >= kHistoryCap) {
            break;
        }
    }
    return out;
}

Outcome writeOrder(const QString& optionsPath, const QString& historyPath, const QStringList& activeTopFirst, const QSet<QString>& existing,
                   const std::function<bool()>& isRunning)
{
    Outcome outcome;
    if (isRunning && isRunning()) {
        outcome.result = Result::Running;
        return outcome;
    }
    QByteArray raw;
    bool exists = false;
    if (!readFile(optionsPath, &raw, &exists, &outcome.error)) {
        outcome.result = Result::IoError;
        return outcome;
    }
    const PackList list = parseOptions(raw, exists);
    if (list.state == PackList::State::Unreadable) {
        outcome.result = Result::Unreadable;
        return outcome;
    }
    QStringList base = list.entries;
    if (list.state != PackList::State::Ok) {
        base = { vanillaEntry() };  // no file or no line: the game's own default plus what was asked for
    }
    const QStringList merged = mergeOrder(base, activeTopFirst, existing);
    if (list.state == PackList::State::Ok && merged == list.entries) {
        outcome.result = Result::NoChange;
        return outcome;
    }
    if (list.state != PackList::State::Ok && merged == base) {
        outcome.result = Result::NoChange;  // nothing to turn on in a file that has no line: do not invent one
        return outcome;
    }

    HistoryEntry entry;
    entry.fileExisted = exists;
    entry.packsLine = lineOf(raw, kPacksKey);
    entry.incompatibleLine = lineOf(raw, kIncompatibleKey);
    entry.time = QDateTime::currentDateTimeUtc();
    QList<HistoryEntry> history = readHistory(historyPath);
    qint64 id = entry.time.toMSecsSinceEpoch();
    if (!history.isEmpty()) {
        id = std::max(id, history.first().id.toLongLong() + 1);
    }
    entry.id = QString::number(id);
    const QList<HistoryEntry> before = history;
    history.prepend(entry);
    while (history.size() > kHistoryCap) {
        history.removeLast();
    }

    // The game may have started while this was prepared: the last look, right before the first byte is written.
    if (isRunning && isRunning()) {
        outcome.result = Result::Running;
        return outcome;
    }
    // The way back is on disk before the file changes. When it cannot be written the file stays as it is.
    if (!writeHistory(historyPath, history, &outcome.error)) {
        outcome.result = Result::IoError;
        return outcome;
    }
    const QByteArray updated = setLine(raw, kPacksKey, kPacksKey + ':' + encodeEntries(merged));
    if (!writeAtomic(optionsPath, updated, &outcome.error)) {
        QString ignored;
        writeHistory(historyPath, before, &ignored);  // nothing changed: no way back to remember
        outcome.result = Result::IoError;
        return outcome;
    }
    // a new pack order answers the question "the game turned the packs off" (zero/content/PackRollback): the marker is obsolete
    Rollback::clearMarker(Rollback::markerPathForHistory(historyPath));
    outcome.result = Result::Ok;
    outcome.id = entry.id;
    return outcome;
}

Outcome undoLast(const QString& optionsPath, const QString& historyPath, const std::function<bool()>& isRunning, const QString& expectedId)
{
    Outcome outcome;
    if (isRunning && isRunning()) {
        outcome.result = Result::Running;
        return outcome;
    }
    QList<HistoryEntry> history = readHistory(historyPath);
    if (history.isEmpty()) {
        outcome.result = Result::NothingToUndo;
        return outcome;
    }
    const HistoryEntry entry = history.first();
    if (!expectedId.isEmpty() && entry.id != expectedId) {
        outcome.result = Result::Stale;
        return outcome;
    }
    QByteArray raw;
    bool exists = false;
    if (!readFile(optionsPath, &raw, &exists, &outcome.error)) {
        outcome.result = Result::IoError;
        return outcome;
    }
    QByteArray restored = setLine(raw, kPacksKey, entry.packsLine);
    restored = setLine(restored, kIncompatibleKey, entry.incompatibleLine);
    if (isRunning && isRunning()) {
        outcome.result = Result::Running;
        return outcome;
    }
    if (restored != raw) {
        if (!entry.fileExisted && restored.trimmed().isEmpty()) {
            QFile::remove(optionsPath);  // the file was made by the write that is undone and holds nothing else
        } else if (!writeAtomic(optionsPath, restored, &outcome.error)) {
            outcome.result = Result::IoError;
            return outcome;
        }
    } else {
        outcome.result = Result::NoChange;
    }
    history.removeFirst();
    QString ignored;
    writeHistory(historyPath, history, &ignored);  // the lines are back; a history that could not be shortened only offers an undo of a no-op
    if (outcome.result != Result::NoChange) {
        outcome.result = Result::Ok;
    }
    outcome.id = entry.id;
    return outcome;
}

QSet<QString> packFilesIn(const QString& resourcePacksDir)
{
    QSet<QString> names;
    const QDir dir(resourcePacksDir);
    for (const QFileInfo& info : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = info.fileName();
        if (name.startsWith(QLatin1Char('.'))) {
            continue;  // ".index" (packwiz) and the like
        }
        if (info.isDir() || name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
            names.insert(name);
        }
    }
    return names;
}

Outcome turnOnAtTop(const QString& optionsPath, const QString& historyPath, const QString& resourcePacksDir, const QString& fileName,
                    const std::function<bool()>& isRunning)
{
    const QSet<QString> existing = packFilesIn(resourcePacksDir);
    Outcome outcome;
    if (!existing.contains(fileName)) {
        outcome.result = Result::IoError;
        outcome.error = QObject::tr("%1 is not in the resource packs folder").arg(fileName);
        return outcome;
    }
    QStringList active;
    for (const QString& name : activeFiles(readOptions(optionsPath).entries)) {
        if (name != fileName && existing.contains(name)) {
            active.append(name);
        }
    }
    active.prepend(fileName);
    return writeOrder(optionsPath, historyPath, active, existing, isRunning);
}

QStringList orderAfterTurningOn(const QStringList& files, const QStringList& activeTopFirst, const QSet<QString>& existing)
{
    QStringList order;
    for (const QString& name : files) {
        if (existing.contains(name) && !order.contains(name)) {
            order.append(name);
        }
    }
    for (const QString& name : activeTopFirst) {
        if (existing.contains(name) && !order.contains(name)) {
            order.append(name);
        }
    }
    return order;
}

Outcome turnOnManyAtTop(const QString& optionsPath, const QString& historyPath, const QString& resourcePacksDir, const QStringList& fileNames,
                        const std::function<bool()>& isRunning)
{
    const QSet<QString> existing = packFilesIn(resourcePacksDir);
    const bool anyThere = std::any_of(fileNames.cbegin(), fileNames.cend(), [&existing](const QString& name) { return existing.contains(name); });
    if (!anyThere) {
        Outcome outcome;
        outcome.result = Result::IoError;
        outcome.error = QObject::tr("The packs are not in the resource packs folder");
        return outcome;
    }
    const QStringList order = orderAfterTurningOn(fileNames, activeFiles(readOptions(optionsPath).entries), existing);
    return writeOrder(optionsPath, historyPath, order, existing, isRunning);
}

bool supportsOrdering(const QString& mcVersion)
{
    const QString version = mcVersion.trimmed();
    if (version.isEmpty()) {
        return true;
    }
    static const QRegularExpression release(QStringLiteral("^(\\d+)\\.(\\d+)"));
    if (const auto match = release.match(version); match.hasMatch()) {
        const int major = match.captured(1).toInt();
        const int minor = match.captured(2).toInt();
        return major > 1 || (major == 1 && minor >= 13);
    }
    static const QRegularExpression weekly(QStringLiteral("^(\\d{2})w(\\d{2})"));
    if (const auto match = weekly.match(version); match.hasMatch()) {
        const int year = match.captured(1).toInt();
        const int week = match.captured(2).toInt();
        return year > 17 || (year == 17 && week >= 43);  // 17w43a is the first snapshot of 1.13
    }
    static const QRegularExpression old(QStringLiteral("^(?:[abc]\\d|rd-|inf-|in-|1\\.rv-)"), QRegularExpression::CaseInsensitiveOption);
    return !old.match(version).hasMatch();
}

}  // namespace Packs
}  // namespace Content
}  // namespace Zero
