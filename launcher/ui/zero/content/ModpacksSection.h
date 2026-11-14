// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Modpacks section of the Discover page (docs/zero/CONTENT.md, "Discover page" > "Modpacks").
//
// Everything kind-independent (toolbar, chips, filter panel, paging, list, details panel, generations, states) is ContentSection.
// What is Modpacks here: the painted card delegate, the featured strip above the list (default view only), Technic's background
// details, the installed packs (instances that manage a pack) and the Install / Update action. The install action only emits
// installRequested(): the install dialog belongs to the page that hosts this one.
#pragma once

#include <QList>

#include <functional>

#include "ui/zero/content/ContentSection.h"

namespace Zero::Content {

class FeaturedStrip;
class ModpackDelegate;

/// An instance that manages a modpack (BaseInstance::isManagedPack()).
struct InstalledPack {
    QString key;  ///< managedKey(type, id)
    QString instanceId;
    QString instanceName;
    QString versionId;
    QString versionName;
};

/// Everything the section needs from the outside.
struct ModpacksEnvironment : ContentEnvironment {
    std::function<QList<InstalledPack>()> installedPacks;
};

class ModpacksSection : public ContentSection {
    Q_OBJECT
   public:
    static constexpr int kPrefetchMax = 12;          ///< Technic rows whose details are fetched in the background to fill their cards
    static constexpr int kPrefetchParallel = 4;
    static constexpr quint64 kPrefetchGeneration = ~quint64(0);  ///< answers for these requests only repaint the card

    explicit ModpacksSection(const ModpacksEnvironment& env, QWidget* parent = nullptr);

    // ---- featured strip ----
    FeaturedStrip* featuredStrip() const { return m_featured; }
    bool isFeaturedLoading() const { return m_featuredLoading; }

    ModpackDelegate* delegate() const;

   signals:
    /// The Install / Update button of the details panel or of a hero card: `updateInstanceId` is the instance to update (empty = a
    /// new instance). The hosting page shows the install dialog.
    void installRequested(const Zero::Content::ModpackItemPtr& item, const QString& updateInstanceId);

   protected:
    FilterState sectionDefaults() const override;
    QList<Facet> sectionFacets(const FacetContext& ctx) const override;
    QSet<QString> readInstalledKeys() override;
    DetailsHeader headerFor(const ContentItem& item) const override;
    void updatePrimaryAction() override;
    void onPrimaryAction() override;

    bool takesSearchAnswer(quint64 generation, bool ok, const QList<ContentItemPtr>& items) override;
    bool takesDetailsAnswer(quint64 generation, const ContentItemPtr& item) override;
    bool isDialogGeneration(quint64 generation) const override;
    void onSearchStarted() override;
    void onPageLoaded(const QList<ContentItemPtr>& items) override;
    void onViewStateChanged() override;
    void onReload() override;
    void onInstalledChanged() override;
    void onSelectionChanged(const ContentItemPtr& item) override;
    void refreshAccessoryImages() override;
    bool hasAccessoryWork() const override;

   private:
    static ContentSectionSpec makeSpec();
    std::optional<InstalledPack> installedFor(const ModpackItem& item) const;
    void updateFeatured();
    void requestFeatured();
    void prefetchDetails();

    ModpacksEnvironment m_modpacksEnv;
    FeaturedStrip* m_featured;
    bool m_featuredRequested = false;
    bool m_featuredLoading = false;
    quint64 m_featuredGeneration = 0;
    QList<ModpackItemPtr> m_prefetchQueue;
    int m_prefetching = 0;
    QList<InstalledPack> m_installed;
};

}  // namespace Zero::Content
