// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the pure half of the install cart's review (docs/zero/SHELL.md, "Discover > Install cart"): what the combined install
// downloads (`CartInstallPlan`: the chosen mods, their dependencies, the resource packs, the shaders, grouped by kind), how the
// dependencies of SEVERAL mods are merged (one entry per project, "Required by" of all, a dependency that is itself in the cart is
// the cart's row), and every text of the dialog, the footer and the toast. No widgets, no APPLICATION, no network.
#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "zero/content/ContentFilter.h"
#include "zero/content/ModInstall.h"

namespace Zero::Content {

// ---- the plan ----

/// One chosen cart entry with the version the review settled on.
struct CartPlanRow {
    Kind kind = Kind::Mod;
    QString cartKey;  ///< InstallCart key
    ModPlatform::IndexedPack::Ptr pack;
    ModPlatform::IndexedVersion version;
    bool update = false;  ///< the target has an older file of it
};

/// One download of the combined install.
struct CartDownload {
    Kind kind = Kind::Mod;
    PlannedMod mod;
    QString cartKey;      ///< empty for a dependency
    bool turnOn = false;  ///< resource pack: goes on top of the game's pack list afterwards
};

struct CartInstallPlan {
    /// Grouped by kind: the mods (cart order, then their dependencies), the resource packs (cart order), the shaders (cart order).
    QList<CartDownload> downloads;

    bool isEmpty() const { return downloads.isEmpty(); }
    int count() const { return int(downloads.size()); }
    int countOf(Kind kind) const;
    int dependencyCount() const;
    /// Every download is an update of an installed file (the toast says "Updated").
    bool allUpdates() const;
    /// The cart keys whose download this plan holds.
    QStringList cartKeys() const;
    /// The page of the instance window the toast's Show opens: "mods", else "resourcepacks", else "shaderpacks"; empty when empty.
    QString firstPageId() const;
    /// The resource packs to put on top, in cart order.
    QList<CartDownload> packsToTurnOn() const;
};

/// "<provider>:<project id>" of a pack: what two entries of the same project share.
QString projectKeyOf(const ModPlatform::IndexedPack::Ptr& pack);

/// The plan of `rows` plus `dependencies`. A row without pack or download URL is dropped; `turnPacksOn` marks the resource pack rows that
/// are not updates (an update keeps its place in the list). Dependencies get the reason `reasonDependency()`, one per project.
CartInstallPlan buildCartPlan(const QList<CartPlanRow>& rows, const QList<PlannedMod>& dependencies, bool turnPacksOn);

struct MergedDependencies {
    QList<PlannedMod> toAdd;       ///< to download, one per project
    QStringList alreadyInstalled;  ///< names of the ones the folder probably has (`maybeInstalled`), once each
};

/// Merges the answer of ONE dependency run over several mods: one entry per project (the "Required by" names of the repeats are united
/// in order, `dependentOn` keeps the first), entries without a pack are dropped, entries whose project is in `projectsInCart` are
/// dropped (the cart's own row stands for them), and `maybeInstalled` ones move to `alreadyInstalled`.
MergedDependencies mergeDependencies(const QList<PlannedMod>& found, const QSet<QString>& projectsInCart);

// ---- texts ----

/// "Add 3 mods" / "Add 2 resource packs" / "Add 1 shader" for one kind, "Add 5 items" for several.
QString cartAddText(int mods, int resourcePacks, int shaders);
/// The title of the dialog: cartAddText() of the cart's counts.
inline QString cartTitleText(int mods, int resourcePacks, int shaders)
{
    return cartAddText(mods, resourcePacks, shaders);
}
/// The primary action: cartAddText() of what will download; "Add" while nothing will.
QString cartPrimaryText(int mods, int resourcePacks, int shaders);
/// "Checking…" (the primary action while versions or dependencies load).
QString cartCheckingText();
/// "To Pack Test · 1.21.1 Fabric".
QString cartSubtitleText(const QString& instanceName, const QString& summary);
/// "Added 3 mods and 1 shader to Pack Test" / "Added 2 mods, 1 resource pack and 1 shader to Pack Test"; `updates` = every download
/// was an update: "Updated 2 mods in Pack Test".
QString cartToastText(int mods, int resourcePacks, int shaders, const QString& instanceName, bool updates = false);
/// "Close Pack Test to add content".
QString cartCloseText(const QString& instanceName);

QString cartFindingVersionsText();                  ///< "Finding versions…"
QString cartVersionsFailedText();                   ///< "Couldn't load versions"
QString cartNoVersionText(const QString& summary);  ///< "No version for 1.21.1 Fabric · will be skipped"
QString cartNeedsModdedText();                      ///< "Needs a modded instance · will be skipped"
/// "Update → 2.0" (`version` = the chosen version's name).
QString cartUpdateText(const QString& version);

}  // namespace Zero::Content
