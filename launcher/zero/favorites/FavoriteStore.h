// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - favorite worlds and servers. Kept in ZERO's own data (`<data>/zero/favorites.json`), never in the
// worlds or in servers.dat: starring something only changes this file.
//
// Keys: a world is `<instanceId>/<world folder>` (Worlds::WorldEntry::key()), a server its normalized address
// (Servers::VaultServer::address). The file is written with QSaveFile on every change; a file that cannot be parsed is
// moved aside to `favorites.json.corrupt` and the store starts empty.
#pragma once

#include <QObject>
#include <QSet>
#include <QString>

namespace Zero {
namespace Favorites {

enum class Kind { World, Server };

class FavoriteStore : public QObject {
    Q_OBJECT
   public:
    /// Loads `filePath` (missing = empty).
    explicit FavoriteStore(const QString& filePath, QObject* parent = nullptr);

    /// The application's store (`<data>/zero/favorites.json`), created on first use. Null without an Application.
    static FavoriteStore* instance();

    bool contains(Kind kind, const QString& key) const;
    QSet<QString> keys(Kind kind) const { return kind == Kind::World ? m_worlds : m_servers; }
    int count(Kind kind) const { return int(keys(kind).size()); }

    /// Adds or removes `key`; saves and emits changed() when something changed. Returns false when saving failed (the
    /// change is kept in memory either way).
    bool set(Kind kind, const QString& key, bool favorite);
    /// Flips `key` and returns its new state.
    bool toggle(Kind kind, const QString& key);

    QString filePath() const { return m_path; }
    QString lastError() const { return m_error; }

   signals:
    void changed(Zero::Favorites::Kind kind, const QString& key, bool favorite);

   private:
    void load();
    bool save();
    QSet<QString>& setFor(Kind kind) { return kind == Kind::World ? m_worlds : m_servers; }

    QString m_path;
    QString m_error;
    QSet<QString> m_worlds;
    QSet<QString> m_servers;
};

}  // namespace Favorites
}  // namespace Zero

Q_DECLARE_METATYPE(Zero::Favorites::Kind)
