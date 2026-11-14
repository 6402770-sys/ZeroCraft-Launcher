// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the install cart of the Discover page (docs/zero/SHELL.md, "Discover > Install cart"): the mods, resource packs
// and shaders the user ticked on the cards, in selection order, waiting for ONE review that adds them all to the target instance.
//
// Pure core: no widgets, no network. An entry keeps the whole ContentItemPtr (the review asks the provider for versions with it),
// its kind and its provider, so the review (CartReviewDialog) needs nothing else from the sections. The cart lives for the session
// (never saved), survives searches, filter changes, section switches and a change of the target (the review checks the entries
// again). Modpacks have no cart. At most kCap entries: the 51st is refused (limitReached()).
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "zero/content/ContentFilter.h"
#include "zero/content/ModpackSource.h"

namespace Zero::Content {

/// One ticked item.
struct CartEntry {
    Kind kind = Kind::Mod;  ///< Mod, ResourcePack or Shader
    ContentItemPtr item;
    Provider provider = Provider::Modrinth;
    QString key;  ///< "<kind>:<provider>:<project id>", e.g. "mod:modrinth:AANobbMI" (InstallCart::keyFor)
};

class InstallCart {
   public:
    static constexpr int kCap = 50;

    enum class AddResult { Added, AlreadyIn, Full, Invalid };
    enum class ToggleResult { Added, Removed, Full, Invalid };

    /// "<kind>:<provider>:<project id>"; empty for Kind::Modpack (no cart) or an item without an id.
    static QString keyFor(Kind kind, const ContentItem& item);
    /// "mod" / "resourcepack" / "shader"; empty for Kind::Modpack.
    static QString kindId(Kind kind);
    /// True for the three kinds the cart takes.
    static bool takesKind(Kind kind);

    AddResult add(Kind kind, const ContentItemPtr& item);
    /// True when an entry with this key was removed.
    bool remove(const QString& key);
    ToggleResult toggle(Kind kind, const ContentItemPtr& item);
    void clear() { m_entries.clear(); }

    bool contains(const QString& key) const;
    bool contains(Kind kind, const ContentItem& item) const { return contains(keyFor(kind, item)); }
    bool isEmpty() const { return m_entries.isEmpty(); }
    int count() const { return int(m_entries.size()); }
    int countOf(Kind kind) const;
    /// In selection order.
    const QList<CartEntry>& entries() const { return m_entries; }
    QList<CartEntry> entriesOf(Kind kind) const;

    /// "1 mod selected", "3 mods selected", "1 shader selected", "2 resource packs selected", "2 mods, 1 shader selected" (kind order:
    /// mods, resource packs, shaders); empty for an empty cart. English, no plural forms: the counts decide the noun.
    QString summaryText() const { return summaryText(countOf(Kind::Mod), countOf(Kind::ResourcePack), countOf(Kind::Shader)); }
    static QString summaryText(int mods, int resourcePacks, int shaders);
    /// The same nouns without "selected": "3 mods", "2 mods, 1 shader" (the toast of the combined install uses it with "and").
    static QString countsText(int mods, int resourcePacks, int shaders);
    /// The pieces of countsText(): {"2 mods", "1 shader"} (kind order; kinds with a zero count are left out).
    static QStringList countParts(int mods, int resourcePacks, int shaders);

   private:
    QList<CartEntry> m_entries;
};

/// The cart as a QObject: what the sections, the selection bar and the review share. `changed()` fires once per real change.
class InstallCartModel : public QObject {
    Q_OBJECT
   public:
    explicit InstallCartModel(QObject* parent = nullptr) : QObject(parent) {}

    InstallCart::ToggleResult toggle(Kind kind, const ContentItemPtr& item);
    InstallCart::AddResult add(Kind kind, const ContentItemPtr& item);
    bool remove(const QString& key);
    void clear();

    bool contains(Kind kind, const ContentItem& item) const { return m_cart.contains(kind, item); }
    bool contains(const QString& key) const { return m_cart.contains(key); }
    bool isEmpty() const { return m_cart.isEmpty(); }
    int count() const { return m_cart.count(); }
    int countOf(Kind kind) const { return m_cart.countOf(kind); }
    const QList<CartEntry>& entries() const { return m_cart.entries(); }
    QList<CartEntry> entriesOf(Kind kind) const { return m_cart.entriesOf(kind); }
    QString summaryText() const { return m_cart.summaryText(); }
    const InstallCart& cart() const { return m_cart; }

   signals:
    void changed();
    /// An add was refused because the cart is full (the page toasts "The cart holds up to 50 items").
    void limitReached();

   private:
    InstallCart m_cart;
};

}  // namespace Zero::Content
