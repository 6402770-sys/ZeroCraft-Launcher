// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance Vanilla / Modded body (see VersionPanel.h).
#include "VersionPanel.h"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "ChoiceChips.h"
#include "CreateMetaSource.h"
#include "VersionListView.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Create {

namespace {
constexpr int kSearchWidth = 200;  // search beside the chips (Vanilla)

QLabel* overline(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, Role::Overline);
    return label;
}

QString filterId(VersionFilter filter)
{
    return QString::number(static_cast<int>(filter));
}
}  // namespace

VersionPanel::VersionPanel(MetaSource* source, QWidget* parent) : QWidget(parent), m_source(source)
{
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(Tokens::spaceSm);

    // Header of the Minecraft column: overline, filter chips and search. Vanilla keeps chips and search on one row; Modded
    // (a narrow column) puts the search full-width under the chips.
    m_mcHeader = new QWidget(this);
    m_mcHeaderLayout = new QVBoxLayout(m_mcHeader);
    m_mcHeaderLayout->setContentsMargins(0, 0, 0, 0);
    m_mcHeaderLayout->setSpacing(Tokens::spaceSm);
    m_toolbar = new QHBoxLayout;
    m_toolbar->setSpacing(Tokens::spaceMd);
    m_filterChips = new ChoiceChipRow(tr("Minecraft version filter"), m_mcHeader);
    QList<QPair<QString, QString>> filters;
    for (VersionFilter f : versionFilters())
        filters.append({ filterId(f), versionFilterLabel(f) });
    m_filterChips->setOptions(filters);
    m_filterChips->setCurrent(filterId(m_filter));
    m_search = new QLineEdit(m_mcHeader);
    m_search->setPlaceholderText(tr("Search versions"));
    m_search->setAccessibleName(tr("Search Minecraft versions"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(140);
    m_search->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_mcHeaderLayout->addWidget(overline(tr("Minecraft version"), m_mcHeader));
    m_mcHeaderLayout->addLayout(m_toolbar);
    m_toolbar->addWidget(m_filterChips, 1, Qt::AlignVCenter);
    placeSearch();
    m_mcList = new VersionListView(tr("Minecraft versions"), this);

    // Loader column header: overline, loader chips, captions, "Loader version" overline.
    m_loaderColumn = new QWidget(this);
    auto* loader = new QVBoxLayout(m_loaderColumn);
    loader->setContentsMargins(0, 0, 0, 0);
    loader->setSpacing(Tokens::spaceSm);
    loader->addWidget(overline(tr("Mod loader"), m_loaderColumn));
    m_loaderChips = new ChoiceChipRow(tr("Mod loader"), m_loaderColumn);
    loader->addWidget(m_loaderChips);
    m_unavailable = new QLabel(m_loaderColumn);
    Style::applyRole(m_unavailable, Role::Caption);
    m_unavailable->setWordWrap(true);
    loader->addWidget(m_unavailable);
    m_failureRow = new QWidget(m_loaderColumn);
    auto* failure = new QHBoxLayout(m_failureRow);
    failure->setContentsMargins(0, 0, 0, 0);
    failure->setSpacing(Tokens::spaceSm);
    m_failureText = new QLabel(m_failureRow);
    Style::applyRole(m_failureText, Role::Caption);
    Style::setProperty(m_failureText, "zeroTone", "danger");
    m_failureText->setWordWrap(true);
    m_failureRetry = new QPushButton(tr("Retry"), m_failureRow);
    m_failureRetry->setAccessibleName(tr("Retry loading the mod loaders"));
    Style::setProperty(m_failureRetry, "zeroVariant", "ghost");
    m_failureRetry->setAutoDefault(false);
    failure->addWidget(m_failureText, 1);
    failure->addWidget(m_failureRetry);
    loader->addWidget(m_failureRow);
    loader->addWidget(overline(tr("Loader version"), m_loaderColumn));
    m_loaderList = new VersionListView(tr("Loader versions"), this);

    // One grid, so both lists start on the same line whatever the headers hold: headers in row 0, lists in row 1.
    m_columns = new QGridLayout;
    auto* columns = m_columns;
    columns->setHorizontalSpacing(Tokens::spaceLg);
    columns->setVerticalSpacing(Tokens::spaceSm);
    columns->addWidget(m_mcHeader, 0, 0, Qt::AlignTop);
    columns->addWidget(m_loaderColumn, 0, 1, Qt::AlignTop);
    columns->addWidget(m_mcList, 1, 0);
    columns->addWidget(m_loaderList, 1, 1);
    columns->setColumnStretch(0, 1);
    columns->setColumnStretch(1, 0);  // Modded gives the loader column its half
    columns->setRowStretch(1, 1);
    m_loaderColumn->hide();
    m_loaderList->hide();

    column->addLayout(columns, 1);

    setTabOrder(m_filterChips->chip(filterId(VersionFilter::Experiments)), m_search);
    setTabOrder(m_search, m_mcList->listView());

    connect(m_filterChips, &ChoiceChipRow::currentChanged, this, [this](const QString& id) {
        m_filter = static_cast<VersionFilter>(id.toInt());
        applyMinecraftRows(true);
    });
    connect(m_search, &QLineEdit::textChanged, this, [this]() { applyMinecraftRows(true); });
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        // Enter selects the first match.
        if (m_visibleRows.isEmpty())
            return;
        const QString first = m_visibleRows.constFirst().id;
        m_mcList->setCurrentKey(first);
        chooseMinecraft(first);
    });
    connect(m_mcList, &VersionListView::currentKeyChanged, this, [this](const QString& key) { chooseMinecraft(key); });
    connect(m_mcList, &VersionListView::retryRequested, this, [this]() { m_source->request(kMinecraftUid); });
    connect(m_loaderChips, &ChoiceChipRow::currentChanged, this, [this](const QString& name) {
        m_wantedLoader = loaderFromName(name);
        chooseLoader(m_wantedLoader);
    });
    connect(m_loaderList, &VersionListView::currentKeyChanged, this, [this](const QString& key) {
        m_loaderVersion = key;
        emit selectionChanged();
    });
    connect(m_loaderList, &VersionListView::retryRequested, this, &VersionPanel::retryFailed);
    connect(m_failureRetry, &QPushButton::clicked, this, &VersionPanel::retryFailed);
    connect(m_source, &MetaSource::statusChanged, this, &VersionPanel::onSourceStatus);

    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, &VersionPanel::refreshIcons);

    // Lists that are already there (cached by Prism) are used at once; the rest load in the background.
    for (const QString& uid : moddedMetaUids())
        if (uid != kMinecraftUid && m_source->status(uid) == MetaSource::Status::Ready)
            onSourceStatus(uid);
    m_source->request(kMinecraftUid);
    rebuildMinecraftRows();
    refreshLoaders();
}

void VersionPanel::placeSearch()
{
    m_toolbar->removeWidget(m_search);
    m_mcHeaderLayout->removeWidget(m_search);
    if (m_modded) {
        m_search->setMaximumWidth(QWIDGETSIZE_MAX);
        m_mcHeaderLayout->addWidget(m_search);
    } else {
        m_search->setMaximumWidth(kSearchWidth);
        m_toolbar->addWidget(m_search, 0, Qt::AlignTop);
    }
}

void VersionPanel::refreshIcons()
{
    const QIcon icon = Icons::icon(QStringLiteral("search"), Theme::current().textTertiary);
    if (m_searchIcon)
        m_searchIcon->setIcon(icon);
    else
        m_searchIcon = m_search->addAction(icon, QLineEdit::LeadingPosition);
    m_failureRetry->setIcon(Icons::icon(QStringLiteral("refresh-cw"), Theme::current().textSecondary));
}

QWidget* VersionPanel::firstFocusWidget() const
{
    return m_filterChips->chip(filterId(VersionFilter::Releases));
}

void VersionPanel::setModded(bool modded)
{
    if (m_modded == modded)
        return;
    m_modded = modded;
    m_loaderColumn->setVisible(modded);
    m_loaderList->setVisible(modded);
    m_columns->setColumnStretch(1, modded ? 1 : 0);
    placeSearch();
    if (modded) {
        for (const QString& uid : moddedMetaUids())
            m_source->request(uid);
        // Lists that are already Ready (cached, or loaded earlier) emit nothing: tell the catalog about them.
        for (const QString& uid : moddedMetaUids())
            if (uid != kMinecraftUid && m_source->status(uid) == MetaSource::Status::Ready)
                onSourceStatus(uid);
    }
    refreshLoaders();
    emit selectionChanged();
    emit settledChanged();
}

bool VersionPanel::hasSelection() const
{
    if (m_minecraft.isEmpty())
        return false;
    return !m_modded || (m_loader.has_value() && !m_loaderVersion.isEmpty());
}

bool VersionPanel::isSettled() const
{
    const QStringList uids = m_modded ? moddedMetaUids() : QStringList{ kMinecraftUid };
    for (const QString& uid : uids) {
        const auto s = m_source->status(uid);
        if (s != MetaSource::Status::Ready && s != MetaSource::Status::Failed)
            return false;
    }
    return true;
}

void VersionPanel::preselect(const QString& minecraft, std::optional<Loader> loader)
{
    m_pendingMinecraft = minecraft;
    if (loader)
        m_wantedLoader = loader;
    if (m_source->status(kMinecraftUid) == MetaSource::Status::Ready)
        applyMinecraftRows(true);
    refreshLoaders();
}

void VersionPanel::setFilter(VersionFilter filter)
{
    m_filter = filter;
    m_filterChips->setCurrent(filterId(filter));
    applyMinecraftRows(true);
}

void VersionPanel::setSearchText(const QString& text)
{
    m_search->setText(text);
}

QString VersionPanel::unavailableText() const
{
    return m_unavailable->isVisibleTo(this) ? m_unavailable->text() : QString();
}

void VersionPanel::onSourceStatus(const QString& uid)
{
    const bool ready = m_source->status(uid) == MetaSource::Status::Ready;
    if (uid == kMinecraftUid) {
        rebuildMinecraftRows();
    } else if (uid == kIntermediaryUid) {
        if (ready)
            m_catalog.setIntermediaryVersions(intermediaryMinecraftIds(m_source->versions(uid)));
        else
            m_catalog.clearIntermediary();
        refreshLoaders();
    } else if (const auto l = loaderFromUid(uid)) {
        if (ready)
            m_catalog.setLoaderVersions(*l, loaderFacts(m_source->versions(uid)));
        else
            m_catalog.clearLoader(*l);
        refreshLoaders();
    }
    emit settledChanged();
}

void VersionPanel::rebuildMinecraftRows()
{
    switch (m_source->status(kMinecraftUid)) {
        case MetaSource::Status::Ready:
            m_allRows = buildVersionRows(minecraftFacts(m_source->versions(kMinecraftUid)));
            applyMinecraftRows(true);
            return;
        case MetaSource::Status::Failed:
            m_allRows.clear();
            m_visibleRows.clear();
            m_mcList->showError(tr("Couldn't load the Minecraft versions.\n%1").arg(m_source->errorText(kMinecraftUid)));
            break;
        case MetaSource::Status::Idle:
        case MetaSource::Status::Loading:
            m_allRows.clear();
            m_visibleRows.clear();
            m_mcList->showLoading(tr("Loading Minecraft versions…"));
            break;
    }
    chooseMinecraft({});
}

void VersionPanel::applyMinecraftRows(bool preferCurrent)
{
    if (m_source->status(kMinecraftUid) != MetaSource::Status::Ready)
        return;
    // A preselected version switches the filter to its kind (and clears a search that would hide it).
    if (!m_pendingMinecraft.isEmpty()) {
        const int i = indexOfVersion(m_allRows, m_pendingMinecraft);
        if (i >= 0) {
            m_filter = filterFor(m_allRows.at(i).type);
            m_filterChips->setCurrent(filterId(m_filter));
            if (!m_search->text().isEmpty()) {
                const QSignalBlocker block(m_search);
                m_search->clear();
            }
        }
    }
    m_visibleRows = filterVersionRows(m_allRows, m_filter, m_search->text());
    QList<ListRow> rows;
    rows.reserve(m_visibleRows.size());
    for (const VersionRow& r : m_visibleRows)
        rows.append({ r.id, r.id, versionTypeLabel(r.type), r.latest ? tr("Latest") : QString(), r.released });
    const QString search = m_search->text().trimmed();
    m_mcList->setRows(rows, search.isEmpty() ? tr("No versions of this kind.") : tr("No versions match \"%1\".").arg(search));

    QString pick;
    if (!m_pendingMinecraft.isEmpty() && indexOfVersion(m_visibleRows, m_pendingMinecraft) >= 0) {
        pick = m_pendingMinecraft;
        m_pendingMinecraft.clear();
    } else if (preferCurrent && indexOfVersion(m_visibleRows, m_minecraft) >= 0) {
        pick = m_minecraft;
    } else if (const int i = preferredVersionRow(m_visibleRows); i >= 0) {
        pick = m_visibleRows.at(i).id;
    }
    m_mcList->setCurrentKey(pick);
    chooseMinecraft(pick);
}

void VersionPanel::chooseMinecraft(const QString& id)
{
    if (id == m_minecraft)
        return;
    m_minecraft = id;
    refreshLoaders();
    emit selectionChanged();
}

void VersionPanel::refreshLoaders()
{
    if (!m_modded)
        return;
    auto statusOf = [this](const QString& uid) { return m_source->status(uid); };

    if (m_minecraft.isEmpty()) {
        m_loaderChips->setOptions({});
        m_loaderChips->hide();
        m_unavailable->hide();
        m_failureRow->hide();
        if (statusOf(kMinecraftUid) == MetaSource::Status::Failed)
            m_loaderList->setRows({}, tr("Minecraft versions couldn't load."));
        else
            m_loaderList->showLoading(tr("Loading mod loaders…"));
        chooseLoader(std::nullopt);
        return;
    }

    const LoaderCatalog::Availability a = m_catalog.availability(m_minecraft);
    bool loading = false;
    QStringList failedNames;
    QString firstError;
    for (Loader l : a.unknown) {
        const QStringList uids = usesIntermediary(l) ? QStringList{ loaderUid(l), kIntermediaryUid } : QStringList{ loaderUid(l) };
        bool failed = false;
        for (const QString& uid : uids) {
            const auto s = statusOf(uid);
            if (s == MetaSource::Status::Failed) {
                failed = true;
                if (firstError.isEmpty())
                    firstError = m_source->errorText(uid);
            } else if (s != MetaSource::Status::Ready) {
                loading = true;
            }
        }
        if (failed)
            failedNames.append(loaderName(l));
    }

    QList<QPair<QString, QString>> options;
    for (Loader l : a.available)
        options.append({ loaderName(l), loaderName(l) });
    m_loaderChips->setOptions(options);
    m_loaderChips->setVisible(!options.isEmpty());

    const QString caption = unavailableCaption(m_minecraft, a.unavailable);
    m_unavailable->setText(caption);
    m_unavailable->setVisible(!caption.isEmpty());

    m_failureText->setText(tr("Couldn't load: %1.").arg(failedNames.join(QStringLiteral(", "))));
    m_failureRow->setVisible(!failedNames.isEmpty() && !a.available.isEmpty());

    if (a.available.isEmpty()) {
        if (loading)
            m_loaderList->showLoading(tr("Loading mod loaders…"));
        else if (!failedNames.isEmpty())
            m_loaderList->showError(tr("Couldn't load the mod loaders.\n%1").arg(firstError));
        else
            m_loaderList->setRows({}, tr("No mod loader has versions for Minecraft %1.").arg(m_minecraft));
        chooseLoader(std::nullopt);
        return;
    }
    std::optional<Loader> pick;
    if (m_wantedLoader && a.available.contains(*m_wantedLoader))
        pick = m_wantedLoader;
    else if (m_loader && a.available.contains(*m_loader))
        pick = m_loader;
    else
        pick = a.available.constFirst();
    chooseLoader(pick);
}

void VersionPanel::chooseLoader(std::optional<Loader> loader)
{
    if (loader != m_loader && loader && m_loader)
        m_loaderVersion.clear();  // another loader: its recommended version, even if a version string happens to match
    m_loader = loader;
    m_loaderChips->setCurrent(loader ? loaderName(*loader) : QString());
    refreshLoaderVersions();
}

void VersionPanel::refreshLoaderVersions()
{
    if (!m_loader) {
        if (!m_loaderVersion.isEmpty()) {
            m_loaderVersion.clear();
            emit selectionChanged();
        }
        return;
    }
    const QList<LoaderVersionRow> versions = m_catalog.versionsFor(*m_loader, m_minecraft);
    QList<ListRow> rows;
    rows.reserve(versions.size());
    for (const LoaderVersionRow& v : versions)
        rows.append({ v.version, v.version, {}, v.recommended ? tr("Recommended") : QString(), v.released });
    m_loaderList->setRows(rows, tr("No %1 versions for Minecraft %2.").arg(loaderName(*m_loader), m_minecraft));
    QString pick;
    bool kept = false;
    for (const LoaderVersionRow& v : versions)
        if (v.version == m_loaderVersion)
            kept = true;
    if (kept)
        pick = m_loaderVersion;
    else if (const int i = preferredLoaderRow(versions); i >= 0)
        pick = versions.at(i).version;
    m_loaderList->setCurrentKey(pick);
    m_loaderVersion = pick;
    emit selectionChanged();
}

void VersionPanel::retryFailed()
{
    const QStringList uids = m_modded ? moddedMetaUids() : QStringList{ kMinecraftUid };
    for (const QString& uid : uids)
        if (m_source->status(uid) == MetaSource::Status::Failed)
            m_source->request(uid);
}

}  // namespace Create
}  // namespace Zero
