// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the malware scan of a mods folder ("Before you play"). No widgets, no Application; read-only: jars are only read.
//
//  - `scanJar` matches one jar against a SignatureList: the hash of the file, the entry names, the bytes of every `.class`, and the same
//    for the jars nested inside it, ONE level deep (jar-in-jar of Fabric / Forge / NeoForge). Jars nested deeper are not opened.
//  - `ScanCache` remembers the verdict per jar by (file name, size, modification time) and the signature list version, in
//    `<instance root>/.zero/scan-cache.json`; a changed list (or an unreadable cache file) throws everything away.
//  - `scanModsFolder` lists the enabled jars (`*.jar` directly in the folder, links skipped), answers from the cache where it can and scans
//    the rest. `cachedOnly` never opens a jar: used on the UI thread to see whether the answer is already known.
#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>

#include "zero/security/MalwareSignatures.h"

namespace Zero {
namespace Security {

struct Hit {
    QString signatureId;
    QString name;     ///< the signature's name ("Fractureiser (injected loader in DungeonZ)")
    QString infoUrl;
    QString where;    ///< "classes/Foo.class", "(the jar file)", "inner.jar > x.class"
};

struct JarScan {
    bool readable = true;   ///< false when the archive could not be opened (never a finding by itself)
    QString error;
    QList<Hit> hits;
};

/// Opens `path` (read-only) and matches it against `list`. Never throws; an unreadable archive gives `readable = false`.
JarScan scanJar(const QString& path, const SignatureList& list);
/// Same for jar bytes already in memory (what a nested jar is).
JarScan scanJarBytes(const QByteArray& bytes, const SignatureList& list);

struct CachedVerdict {
    qint64 size = -1;
    qint64 mtimeMs = 0;
    bool readable = true;
    QList<QPair<QString, QString>> hits;  ///< (signature id, where)
};

class ScanCache {
   public:
    /// `<instance root>/.zero/scan-cache.json`
    static QString pathFor(const QString& instanceRoot);

    /// Reads the file; nothing stays when it is missing, unreadable, or was written for another list version (`listKey`).
    void load(const QString& path, const QString& listKey);
    /// Writes the file (through a temporary file); false when that failed. Does nothing (true) when nothing changed since `load`.
    bool save();

    /// The verdict when the jar has not changed (size and mtime equal) since it was stored.
    bool lookup(const QString& fileName, qint64 size, qint64 mtimeMs, CachedVerdict* out) const;
    void store(const QString& fileName, const CachedVerdict& verdict);
    /// Drops every jar that is not in `liveFileNames`.
    void keepOnly(const QStringList& liveFileNames);

    /// The mods-check verdict ("these jars, with this instance, were fine"): a fingerprint of the folder and the instance components.
    bool modsCheckClean(const QString& fingerprint) const;
    void setModsCheck(const QString& fingerprint, bool clean);

    int size() const { return int(m_jars.size()); }

   private:
    QString m_path;
    QString m_listKey;
    QHash<QString, CachedVerdict> m_jars;
    QString m_modsFingerprint;
    bool m_modsClean = false;
    bool m_dirty = false;
};

struct InfectedJar {
    QString fileName;
    QString path;
    QList<Hit> hits;
};

struct FolderScan {
    QList<InfectedJar> infected;  ///< by file name
    QStringList unreadable;       ///< jars that could not be opened
    int jars = 0;                 ///< enabled jars looked at
    int fromCache = 0;
    int scanned = 0;              ///< jars that were opened and read
    bool complete = true;         ///< false: `cachedOnly` and at least one jar had no cached verdict
};

struct FolderScanOptions {
    bool cachedOnly = false;
    std::function<void(int done, int total)> progress;  ///< after every jar (cached ones included), may be empty
};

/// `cache` is updated (and pruned to the jars that are there) but not saved.
FolderScan scanModsFolder(const QString& modsDir, const SignatureList& list, ScanCache& cache, const FolderScanOptions& options = {});

}  // namespace Security
}  // namespace Zero
