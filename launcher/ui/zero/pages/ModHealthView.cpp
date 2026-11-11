// SPDX-License-Identifier: GPL-3.0-only
#include "ModHealthView.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include "Application.h"
#include "ElidedLabel.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"
#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/mods/ModHealthScanner.h"

namespace Zero {

using namespace Mods;

namespace {

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr, bool wrap = false)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone)
        Style::setProperty(label, "zeroTone", tone);
    label->setWordWrap(wrap);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& icon, const char* variant, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    Style::setProperty(button, "zeroVariant", variant);
    button->setIconSize(QSize(16, 16));
    if (!icon.isEmpty()) {
        auto apply = [button, icon]() { button->setIcon(Icons::icon(icon, Theme::current().text)); };
        apply();
        QObject::connect(&Theme::instance(), &Theme::changed, button, apply);
    }
    return button;
}

ZeroCard* makeSection(const QString& title, QWidget* parent, QVBoxLayout** layoutOut)
{
    auto* card = new ZeroCard(parent);
    card->setHoverEnabled(false);
    auto* lay = new QVBoxLayout(card);
    lay->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    lay->setSpacing(Tokens::spaceMd);
    lay->addWidget(makeLabel(title, Role::Heading, card));
    *layoutOut = lay;
    return card;
}

QString loaderName(InstanceLoader loader)
{
    switch (loader) {
        case InstanceLoader::Fabric: return QStringLiteral("Fabric");
        case InstanceLoader::Quilt: return QStringLiteral("Quilt");
        case InstanceLoader::Forge: return QStringLiteral("Forge");
        case InstanceLoader::NeoForge: return QStringLiteral("NeoForge");
        case InstanceLoader::None: break;
    }
    return {};
}

PillTag::Tone toneFor(Confidence c)
{
    switch (c) {
        case Confidence::Confirmed: return PillTag::Tone::Danger;
        case Confidence::Likely: return PillTag::Tone::Warning;
        case Confidence::Possible: return PillTag::Tone::Info;
        case Confidence::Unknown: break;
    }
    return PillTag::Tone::Plain;
}

QString confidenceText(Confidence c)
{
    switch (c) {
        case Confidence::Confirmed: return QObject::tr("Confirmed");
        case Confidence::Likely: return QObject::tr("Likely");
        case Confidence::Possible: return QObject::tr("Possible");
        case Confidence::Unknown: break;
    }
    return QObject::tr("Unknown");
}

}  // namespace

ModHealthView::ModHealthView(QWidget* parent) : QWidget(parent)
{
    m_scanner = new ModHealthScanner(this);
    connect(m_scanner, &ModHealthScanner::reportReady, this, &ModHealthView::onReport);

    m_controls = new QWidget(this);
    auto* controlsRow = new QHBoxLayout(m_controls);
    controlsRow->setContentsMargins(0, 0, 0, 0);
    m_recheck = makeButton(tr("Check again"), QStringLiteral("refresh-cw"), "ghost", m_controls);
    m_recheck->setToolTip(tr("Read the mods of this instance again"));
    controlsRow->addWidget(m_recheck);
    m_controls->hide();
    connect(m_recheck, &QPushButton::clicked, this, &ModHealthView::scan);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(Tokens::spaceMd);

    // Instance row (outside the cards, like Crash Detective's picker)
    auto* pickRow = new QHBoxLayout;
    pickRow->setContentsMargins(0, 0, Tokens::spaceSm, 0);
    pickRow->setSpacing(Tokens::spaceMd);
    pickRow->addWidget(makeLabel(tr("Instance"), Role::Label, this));
    m_instances = new QComboBox(this);
    m_instances->setMinimumWidth(260);
    m_instances->setToolTip(tr("Instance to check"));
    pickRow->addWidget(m_instances);
    pickRow->addStretch(1);
    outer->addLayout(pickRow);

    m_stack = new QStackedLayout;
    outer->addLayout(m_stack, 1);

    m_loading = new ZeroEmptyState(QStringLiteral("package"), tr("Reading mods…"), tr("Large modpacks can take a few seconds."), {}, this);
    m_stack->addWidget(m_loading);  // 0
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stack->addWidget(m_scroll);  // 1
    m_noLoader = new ZeroEmptyState(QStringLiteral("package"), tr("No mod loader"),
                                    tr("This instance runs vanilla Minecraft, so there are no mods to check."), {}, this);
    m_stack->addWidget(m_noLoader);  // 2
    m_noMods = new ZeroEmptyState(QStringLiteral("package"), tr("No mods installed"), tr("There is nothing to check yet."), {}, this);
    connect(m_noMods->addSecondaryAction(tr("Open mods folder")), &QPushButton::clicked, this, &ModHealthView::openModsFolder);
    m_stack->addWidget(m_noMods);  // 3
    m_noInstances = new ZeroEmptyState(QStringLiteral("package"), tr("No instances yet"), tr("Create an instance first to check its mods."), {}, this);
    m_stack->addWidget(m_noInstances);  // 4

    connect(m_instances, &QComboBox::currentIndexChanged, this, [this]() {
        if (const QString id = currentInstanceId(); !id.isEmpty())
            APPLICATION->settings()->set("ZeroModHealthInstance", id);
        scan();
    });
    refreshInstances(APPLICATION->settings()->get("ZeroModHealthInstance").toString());
}

QString ModHealthView::description()
{
    return tr("Checks the mods of an instance using their own metadata");
}

QString ModHealthView::currentInstanceId() const
{
    return m_instances->currentData().toString();
}

void ModHealthView::refreshInstances(const QString& preferred)
{
    const QString keep = preferred.isEmpty() ? currentInstanceId() : preferred;
    auto* list = APPLICATION->instances();
    QList<QPair<QString, QString>> items;
    for (int i = 0; i < list->count(); ++i)
        items.append({ list->at(i)->name(), list->at(i)->id() });
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first.compare(b.first, Qt::CaseInsensitive) < 0; });

    QSignalBlocker block(m_instances);
    m_instances->clear();
    for (const auto& item : items)
        m_instances->addItem(item.first, item.second);
    const int index = m_instances->findData(keep);
    m_instances->setCurrentIndex(index >= 0 ? index : (items.isEmpty() ? -1 : 0));
    m_instances->setEnabled(!items.isEmpty());
}

void ModHealthView::open(const QString& instanceId)
{
    refreshInstances(instanceId);
    if (const QString id = currentInstanceId(); !id.isEmpty())
        APPLICATION->settings()->set("ZeroModHealthInstance", id);
    scan();
}

void ModHealthView::updateControls()
{
    m_recheck->setEnabled(!m_scanning && m_instances->count() > 0);
}

void ModHealthView::scan()
{
    m_scanning = false;
    m_hasReport = false;
    auto* instance = dynamic_cast<MinecraftInstance*>(APPLICATION->instances()->getInstanceById(currentInstanceId()));
    if (!instance) {
        m_stack->setCurrentWidget(m_noInstances);
        updateControls();
        emit reportShown();
        return;
    }
    m_context = readInstanceContext(instance->instanceRoot());
    if (m_context.loader == InstanceLoader::None) {
        m_stack->setCurrentWidget(m_noLoader);
        updateControls();
        emit reportShown();
        return;
    }
    m_scanning = true;
    m_stack->setCurrentWidget(m_loading);
    updateControls();
    m_scanner->analyze(instance->id(), instance->modsRoot(), instance->instanceRoot());
}

void ModHealthView::onReport(const QString& instanceId, const Report& report)
{
    if (!m_scanning || instanceId != currentInstanceId())
        return;
    m_scanning = false;
    m_hasReport = true;
    m_report = report;
    if (report.modsScanned == 0 && report.findings.isEmpty()) {
        m_stack->setCurrentWidget(m_noMods);
    } else {
        rebuild();
        m_stack->setCurrentWidget(m_scroll);
    }
    updateControls();
    emit reportShown();
}

void ModHealthView::openModsFolder()
{
    if (auto* instance = APPLICATION->instances()->getInstanceById(currentInstanceId())) {
        const QString dir = instance->modsRoot();
        QDir().mkpath(dir);  // the folder of a loader instance without mods may not exist yet
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
}

void ModHealthView::rebuild()
{
    m_host = new QWidget;
    auto* page = new QVBoxLayout(m_host);
    page->setContentsMargins(0, 0, Tokens::spaceSm, Tokens::spaceXl);
    page->setSpacing(Tokens::spaceLg);
    QWidget* host = m_host;
    const Report& r = m_report;

    // ----- summary -----
    {
        auto* card = new ZeroCard(host);
        card->setHoverEnabled(false);
        auto* v = new QVBoxLayout(card);
        v->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
        v->setSpacing(Tokens::spaceMd);
        auto* top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 0, 0);
        top->setSpacing(Tokens::spaceMd);

        QString iconName, headline;
        int toneIndex = 2;  // 0 danger, 1 warning, 2 success
        if (r.confirmed > 0) {
            iconName = QStringLiteral("circle-x");
            headline = r.confirmed == 1 ? tr("1 problem will stop the game") : tr("%1 problems will stop the game").arg(r.confirmed);
            toneIndex = 0;
        } else if (r.likely + r.possible > 0) {
            iconName = QStringLiteral("triangle-alert");
            const int n = r.likely + r.possible;
            headline = n == 1 ? tr("1 thing to check") : tr("%1 things to check").arg(n);
            toneIndex = 1;
        } else {
            iconName = QStringLiteral("circle-check");
            headline = tr("No problems found");
        }
        auto* icon = new QLabel(card);
        icon->setFixedSize(32, 32);
        icon->setAlignment(Qt::AlignCenter);
        auto applyIcon = [icon, iconName, toneIndex]() {
            const auto& t = Theme::current();
            const QColor color = toneIndex == 0 ? t.danger : toneIndex == 1 ? t.warning : t.success;
            icon->setPixmap(Icons::pixmap(iconName, color, 28, icon->devicePixelRatioF()));
        };
        applyIcon();
        connect(&Theme::instance(), &Theme::changed, icon, applyIcon);
        top->addWidget(icon, 0, Qt::AlignTop);

        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(2);
        texts->addWidget(makeLabel(headline, Role::Title, card));
        QStringList parts;
        parts << (r.modsScanned == 1 ? tr("Checked 1 mod") : tr("Checked %1 mods").arg(r.modsScanned));
        const QString loader = (loaderName(m_context.loader) + QLatin1Char(' ') + m_context.loaderVersion).trimmed();
        if (!loader.isEmpty())
            parts << loader;
        if (!m_context.mcVersion.isEmpty())
            parts << tr("Minecraft %1").arg(m_context.mcVersion);
        texts->addWidget(makeLabel(parts.join(QStringLiteral(" · ")), Role::Caption, card, "secondary"));
        top->addLayout(texts, 1);

        auto* open = makeButton(tr("Open mods folder"), QStringLiteral("folder-open"), "ghost", card);
        connect(open, &QPushButton::clicked, this, &ModHealthView::openModsFolder);
        top->addWidget(open, 0, Qt::AlignTop);
        v->addLayout(top);

        auto* pills = new QHBoxLayout;
        pills->setContentsMargins(0, 0, 0, 0);
        pills->setSpacing(Tokens::spaceSm);
        int shown = 0;
        for (Confidence c : { Confidence::Confirmed, Confidence::Likely, Confidence::Possible, Confidence::Unknown }) {
            const int n = r.count(c);
            if (n <= 0)
                continue;
            pills->addWidget(new PillTag(QStringLiteral("%1 %2").arg(n).arg(confidenceText(c)), toneFor(c), card));
            ++shown;
        }
        pills->addStretch(1);
        if (shown > 0)
            v->addLayout(pills);
        page->addWidget(card);
    }

    // ----- findings -----
    int pillWidth = 0;
    for (Confidence c : { Confidence::Confirmed, Confidence::Likely, Confidence::Possible, Confidence::Unknown }) {
        PillTag probe(confidenceText(c), toneFor(c));
        pillWidth = std::max(pillWidth, probe.sizeHint().width());
    }
    auto addFinding = [&](QVBoxLayout* lay, QWidget* card, const Finding& f) {
        auto* row = new QWidget(card);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, Tokens::spaceSm, 0, Tokens::spaceSm);
        h->setSpacing(Tokens::spaceMd);
        auto* pillCol = new QWidget(row);
        pillCol->setFixedWidth(pillWidth);
        auto* pv = new QVBoxLayout(pillCol);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->addWidget(new PillTag(confidenceText(f.confidence), toneFor(f.confidence), pillCol), 0, Qt::AlignLeft | Qt::AlignTop);
        pv->addStretch(1);
        h->addWidget(pillCol, 0, Qt::AlignTop);

        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(2);
        texts->addWidget(makeLabel(f.title, Role::Label, row, nullptr, true));
        if (!f.detail.isEmpty())
            texts->addWidget(makeLabel(f.detail, Role::Body, row, "secondary", true));
        if (!f.suggestion.isEmpty())
            texts->addWidget(makeLabel(tr("Fix: %1").arg(f.suggestion), Role::Caption, row, "secondary", true));
        if (!f.fileNames.isEmpty()) {
            auto* files = new ElidedLabel(row);
            Style::applyRole(files, Role::Caption);
            Style::setProperty(files, "zeroTone", "tertiary");
            files->setFullText(f.fileNames.join(QStringLiteral(", ")));
            texts->addWidget(files);
        }
        h->addLayout(texts, 1);
        lay->addWidget(row);
    };

    QList<Finding> problems, unreadable;
    for (const Finding& f : r.findings)
        (f.confidence == Confidence::Unknown ? unreadable : problems).append(f);

    if (!problems.isEmpty()) {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Problems"), host, &lay);
        lay->setSpacing(Tokens::spaceXs);
        lay->addWidget(makeLabel(tr("Confirmed: the game will refuse to start. Likely: the mod's author says they don't work together. "
                                    "Possible: may cause problems, ZERO isn't sure."),
                                 Role::Caption, card, "secondary", true));
        for (const Finding& f : std::as_const(problems))
            addFinding(lay, card, f);
        page->addWidget(card);
    }
    if (!unreadable.isEmpty()) {
        QVBoxLayout* lay = nullptr;
        auto* card = makeSection(tr("Couldn't check"), host, &lay);
        lay->setSpacing(Tokens::spaceXs);
        for (const Finding& f : std::as_const(unreadable))
            addFinding(lay, card, f);
        page->addWidget(card);
    }

    page->addStretch(1);
    if (QWidget* old = m_scroll->takeWidget())
        old->deleteLater();
    m_scroll->setWidget(m_host);
}

}  // namespace Zero
