// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Technic Platform API parsing: search/trending results and per-pack details, as plain values (no network,
// no widgets). Also the text helpers the modpack preview needs (title rule, plain-text description, preview HTML).
//
// API shapes (build=multimc):
//  - `trending` / `search?q=` -> {"modpacks":[{"id","name","slug","url","iconUrl"}]}; `name` is the display name.
//  - `modpack/<slug>` -> {"id","name" (= the slug),"displayName","user","url" (zip) | "solder" (Solder API base),...};
//    an unknown slug answers {"error": "..."}.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "Result.h"

namespace Zero {
namespace Content {

/// One row of a Technic `trending` / `search` response.
struct TechnicPackSummary {
    QString id;
    QString slug;
    QString name;  ///< display name
    QString platformUrl;
    QString iconUrl;  ///< empty when the API has none
};

/// One entry of a pack's news feed (`feed`).
struct TechnicFeedEntry {
    QString user;
    qint64 date = 0;  ///< unix seconds, 0 when unknown
    QString content;
    QString url;
};

/// Everything useful in a `modpack/<slug>` response. Missing or null fields are empty / zero.
struct TechnicPackDetails {
    QString id;
    QString slug;
    QString displayName;
    QString author;
    QString description;  ///< as provided by the API, may contain HTML
    QString minecraftVersion;
    QString version;
    QString platformUrl;
    QString iconUrl;
    QString logoUrl;
    QString backgroundUrl;
    qint64 installs = 0;
    qint64 runs = 0;
    qint64 ratings = 0;
    QStringList tags;
    bool isOfficial = false;
    bool isServer = false;
    QString downloadUrl;  ///< `url`: the pack zip for single-zip packs
    QString solderUrl;    ///< `solder`: the Solder API base for Solder packs
    QList<TechnicFeedEntry> feed;  ///< newest first, at most kMaxFeedEntries

    /// A Solder pack (installed through its Solder API) rather than a single zip. A pack with a zip URL is never Solder.
    bool isSolder() const { return downloadUrl.isEmpty() && !solderUrl.isEmpty(); }
    /// The pack can be installed: it names a zip or a Solder API.
    bool isInstallable() const { return !downloadUrl.isEmpty() || !solderUrl.isEmpty(); }
    /// The URL the install task needs: the zip URL, or the Solder base without trailing slashes.
    QString installUrl() const;
};

/// The builds a pack's Solder API lists (`<solder>/modpack/<slug>`).
struct TechnicSolderVersions {
    QString recommended;
    QString latest;
    QStringList builds;  ///< oldest first, as the API lists them
};

inline constexpr int kMaxFeedEntries = 5;

/// Parses a `trending` / `search` response. The `vanilla` pseudo-pack is skipped. An entry without a string `name` or
/// `slug` fails the whole response (as the launcher always did).
Result<QList<TechnicPackSummary>> parseSearchResponse(const QByteArray& data);

/// Parses a `modpack/<slug>` response. An `error` response, malformed JSON or a missing `name` (the slug) is an error.
Result<TechnicPackDetails> parsePackDetails(const QByteArray& data);

/// Parses a Solder `modpack/<slug>` response (recommended, latest, builds).
Result<TechnicSolderVersions> parseSolderVersions(const QByteArray& data);

/// `<solderBase>/modpack/<slug>`, with trailing slashes stripped from the base and the slug percent-encoded.
QString solderPackUrl(const QString& solderBase, const QString& slug);

/// The title to show for a pack: the details' displayName, else the list name, else the slug.
QString previewTitle(const TechnicPackDetails* details, const QString& listName, const QString& slug);

/// The description as one line of plain text (tags dropped, line breaks as spaces, common entities decoded).
QString plainText(const QString& html);
/// plainText() cut to at most `maxChars` characters at a word boundary, with an ellipsis when cut.
QString plainTextSummary(const QString& html, int maxChars);

/// Preview HTML for loaded details: the title (linked to the platform page when known), author, Minecraft version, pack
/// version, then the description as provided.
QString previewHtml(const TechnicPackDetails& details, const QString& title);
/// Preview HTML while the details load.
QString previewLoadingHtml(const QString& title);
/// Preview HTML when the details could not be loaded.
QString previewFailedHtml(const QString& title, const QString& reason);

/// The pack the user has selected, by slug. Responses for any other slug are stale and must be ignored.
class TechnicSelection {
   public:
    void select(const QString& slug) { m_slug = slug; }
    void clear() { m_slug.clear(); }
    QString slug() const { return m_slug; }
    bool isEmpty() const { return m_slug.isEmpty(); }
    /// True when `slug` is the selected pack (never for an empty slug).
    bool isCurrent(const QString& slug) const { return !slug.isEmpty() && slug == m_slug; }

   private:
    QString m_slug;
};

}  // namespace Content
}  // namespace Zero
