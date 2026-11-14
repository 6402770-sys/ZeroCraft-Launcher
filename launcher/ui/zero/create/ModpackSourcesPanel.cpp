// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance modpack sources (see ModpackSourcesPanel.h).
#include "ModpackSourcesPanel.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Create {

namespace {
constexpr int kTileHeight = 72;
constexpr int kLogoSize = 32;

/// Icon-theme name of a classic page's logo (the names the classic pages use).
QString themeIconFor(const QString& pageId)
{
    if (pageId == QLatin1String("atl"))
        return QStringLiteral("atlauncher");
    if (pageId == QLatin1String("ftb") || pageId == QLatin1String("import_ftb") || pageId == QLatin1String("legacy_ftb"))
        return QStringLiteral("ftb_logo");
    return pageId;  // modrinth, flame, technic
}

/// One source: logo, name, one line about it, chevron. Clickable ZeroCard (accessible button).
class SourceTile : public ZeroCard {
   public:
    SourceTile(const QString& pageId, const QString& name, const QString& description, QWidget* parent)
        : ZeroCard(parent), m_pageId(pageId), m_name(name), m_description(description)
    {
        setClickable(true);
        setFixedHeight(kTileHeight);
        setAccessibleName(name);
        setAccessibleDescription(description);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent* event) override
    {
        ZeroCard::paintEvent(event);
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect content = contentsRect();
        const int cy = content.center().y();
        const QRect logo(content.left(), cy - kLogoSize / 2, kLogoSize, kLogoSize);
        const QIcon themed = QIcon::fromTheme(themeIconFor(m_pageId));
        if (!themed.isNull())
            themed.paint(&p, logo);
        else
            Icons::icon(QStringLiteral("package"), t.textSecondary).paint(&p, logo.adjusted(4, 4, -4, -4));

        const int textLeft = logo.right() + 1 + Tokens::spaceMd;
        const int textWidth = content.right() - textLeft - 16 - Tokens::spaceSm;
        const QFont titleFont = Style::font(Role::Heading);
        const QFont captionFont = Style::font(Role::Caption);
        const QFontMetrics tm(titleFont), cm(captionFont);
        const int blockHeight = tm.height() + 2 + cm.height();
        const int top = cy - blockHeight / 2;
        p.setFont(titleFont);
        p.setPen(t.text);
        p.drawText(QRect(textLeft, top, textWidth, tm.height()), Qt::AlignLeft | Qt::AlignVCenter, tm.elidedText(m_name, Qt::ElideRight, textWidth));
        p.setFont(captionFont);
        p.setPen(t.textSecondary);
        p.drawText(QRect(textLeft, top + tm.height() + 2, textWidth, cm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   cm.elidedText(m_description, Qt::ElideRight, textWidth));
        Icons::icon(QStringLiteral("chevron-right"), t.textTertiary).paint(&p, QRect(content.right() - 15, cy - 8, 16, 16));
    }

   private:
    QString m_pageId;
    QString m_name;
    QString m_description;
};
}  // namespace

ModpackSourcesPanel::ModpackSourcesPanel(bool curseForge, QWidget* parent) : QWidget(parent)
{
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(Tokens::spaceSm);

    auto* overline = new QLabel(tr("Sources"), this);
    Style::applyRole(overline, Role::Overline);
    column->addWidget(overline);

    struct Entry {
        QString id, name, description;
    };
    QList<Entry> tiles{ { QStringLiteral("modrinth"), QStringLiteral("Modrinth"), tr("Browse and install modpacks from Modrinth") } };
    if (curseForge)
        tiles.append({ QStringLiteral("flame"), QStringLiteral("CurseForge"), tr("Browse and install modpacks from CurseForge") });
    tiles.append({ QStringLiteral("technic"), QStringLiteral("Technic"), tr("Browse and install modpacks from the Technic Platform") });
    QWidget* previous = nullptr;
    for (const Entry& e : tiles) {
        auto* tile = new SourceTile(e.id, e.name, e.description, this);
        connect(tile, &ZeroCard::clicked, this, [this, id = e.id]() { emit pageRequested(id); });
        column->addWidget(tile);
        m_tiles.append({ e.id, tile });
        if (previous)
            setTabOrder(previous, tile);
        previous = tile;
    }

    column->addSpacing(Tokens::spaceSm);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(Tokens::spaceSm);
    m_browseAll = new QPushButton(tr("Browse all modpacks"), this);
    Style::setProperty(m_browseAll, "zeroVariant", "secondary");
    m_browseAll->setAutoDefault(false);
    connect(m_browseAll, &QPushButton::clicked, this, &ModpackSourcesPanel::browseAllRequested);
    m_more = new QPushButton(tr("More sources"), this);
    Style::setProperty(m_more, "zeroVariant", "ghost");
    m_more->setAutoDefault(false);
    m_more->setAccessibleName(tr("More modpack sources"));
    m_menu = new QMenu(m_more);
    const QList<Entry> more{ { QStringLiteral("atl"), QStringLiteral("ATLauncher"), {} },
                             { QStringLiteral("ftb"), QStringLiteral("FTB"), {} },
                             { QStringLiteral("import_ftb"), tr("FTB App import"), {} },
                             { QStringLiteral("legacy_ftb"), tr("Legacy FTB"), {} } };
    for (const Entry& e : more) {
        QAction* action = m_menu->addAction(QIcon::fromTheme(themeIconFor(e.id)), e.name);
        connect(action, &QAction::triggered, this, [this, id = e.id]() { emit pageRequested(id); });
        m_moreIds.append(e.id);
    }
    m_more->setMenu(m_menu);
    actions->addWidget(m_browseAll);
    actions->addWidget(m_more);
    actions->addStretch(1);
    column->addLayout(actions);
    column->addStretch(1);
    if (previous) {
        setTabOrder(previous, m_browseAll);
        setTabOrder(m_browseAll, m_more);
    }

    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, &ModpackSourcesPanel::refreshIcons);
}

void ModpackSourcesPanel::refreshIcons()
{
    const auto& t = Theme::current();
    m_browseAll->setIcon(Icons::icon(QStringLiteral("search"), t.text));
}

QStringList ModpackSourcesPanel::tileIds() const
{
    QStringList ids;
    for (const auto& t : m_tiles)
        ids.append(t.first);
    return ids;
}

QWidget* ModpackSourcesPanel::tile(const QString& pageId) const
{
    for (const auto& t : m_tiles)
        if (t.first == pageId)
            return t.second;
    return nullptr;
}

}  // namespace Create
}  // namespace Zero
