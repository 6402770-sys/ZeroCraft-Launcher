// SPDX-License-Identifier: GPL-3.0-only
#include "HomeUpdatesSection.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

#include "Application.h"
#include "ElidedLabel.h"
#include "ui/zero/ZeroCard.h"
#include "ui/zero/ZeroCompactButton.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroSpinner.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "InstanceList.h"

namespace Zero {

namespace {

constexpr int kRowHeight = 56;
constexpr int kIconSize = 28;
constexpr int kButtonHeight = CompactButton::kCompactButtonHeight;
constexpr int kIdleIconSize = 20;

/// One clickable row of the card: `surfaceHover` background while hovered, a 1 px `border` divider at the bottom (not on the last row).
/// Same look as the rows of "Recent activity".
class UpdateRow : public QWidget {
   public:
    UpdateRow(bool first, bool last, QWidget* parent) : QWidget(parent), m_first(first), m_last(last)
    {
        setFixedHeight(kRowHeight);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

   protected:
    void enterEvent(QEnterEvent* event) override
    {
        QWidget::enterEvent(event);
        m_hover = true;
        update();
    }
    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        m_hover = false;
        update();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        m_pressed = event->button() == Qt::LeftButton;
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_pressed && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick) {
            onClick();
        }
        m_pressed = false;
    }
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_hover) {
            // follows the card's rounded corners: only the first / last row round their outer corners
            const qreal r = std::max(0, t.radiusLg - 1);
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()), r, r);
            if (!m_first) {
                path.addRect(QRectF(0, 0, width(), height() / 2.0));
            }
            if (!m_last) {
                path.addRect(QRectF(0, height() / 2.0, width(), height() / 2.0));
            }
            p.fillPath(path.simplified(), t.surfaceHover);
        }
        if (!m_last) {
            p.fillRect(QRect(0, height() - 1, width(), 1), t.border);
        }
    }

   private:
    bool m_first;
    bool m_last;
    bool m_hover = false;
    bool m_pressed = false;
};

QString lowerFirst(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text.at(0).toLower();
    }
    return text;
}

QString statusText(Updates::UpdateSweep::State state)
{
    switch (state) {
        case Updates::UpdateSweep::State::Queued:
            return QObject::tr("Queued");
        case Updates::UpdateSweep::State::Checking:
            return QObject::tr("Checking…");
        case Updates::UpdateSweep::State::Updating:
            return QObject::tr("Downloading…");
        case Updates::UpdateSweep::State::Idle:
            break;
    }
    return QObject::tr("Update");
}

QIcon applicationIcon(const QString& instanceId)
{
    auto* app = APPLICATION_DYN;
    if (!app || !app->instances() || !app->icons()) {
        return {};
    }
    auto* inst = app->instances()->getInstanceById(instanceId);
    return inst ? app->icons()->getIcon(inst->iconKey()) : QIcon();
}

}  // namespace

HomeUpdatesSection::HomeUpdatesSection(Updates::UpdateSweep* sweep, IconProvider icons, QWidget* parent)
    : QWidget(parent), m_sweep(sweep), m_icons(icons ? std::move(icons) : IconProvider(&applicationIcon))
{
    setObjectName(QStringLiteral("zeroHomeUpdates"));
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &HomeUpdatesSection::rebuild);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(Tokens::spaceMd);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(Tokens::spaceSm);
    m_title = new QLabel(tr("Updates"), this);
    Style::applyRole(m_title, Role::Heading);
    head->addWidget(m_title);
    head->addStretch(1);
    m_checkAgain = new QPushButton(tr("Check again"), this);
    m_checkAgain->setObjectName(QStringLiteral("zeroUpdatesCheckAgain"));
    Style::setProperty(m_checkAgain, "zeroVariant", "ghost");
    m_checkAgain->setCursor(Qt::PointingHandCursor);
    connect(m_checkAgain, &QPushButton::clicked, this, [this]() {
        if (auto* e = engine()) {
            e->checkNow();
        }
    });
    head->addWidget(m_checkAgain);
    m_updateAll = new CompactButton(tr("Update all"), this);
    m_updateAll->setObjectName(QStringLiteral("zeroUpdatesUpdateAll"));
    Style::setProperty(m_updateAll, "zeroVariant", "primary");
    Style::setProperty(m_updateAll, "zeroSize", "sm");
    m_updateAll->setFixedHeight(kButtonHeight);
    m_updateAll->setCursor(Qt::PointingHandCursor);
    connect(m_updateAll, &QPushButton::clicked, this, [this]() {
        if (auto* e = engine()) {
            e->updateAll();
        }
    });
    head->addWidget(m_updateAll);
    col->addLayout(head);

    m_checkingLine = new QWidget(this);
    auto* checking = new QHBoxLayout(m_checkingLine);
    checking->setContentsMargins(0, 0, 0, 0);
    checking->setSpacing(Tokens::spaceSm);
    m_checkingSpinner = new ZeroSpinner(ZeroSpinner::Small, m_checkingLine);
    checking->addWidget(m_checkingSpinner);
    m_checkingLabel = new QLabel(m_checkingLine);
    Style::applyRole(m_checkingLabel, Role::Caption);
    Style::setProperty(m_checkingLabel, "zeroTone", "secondary");
    checking->addWidget(m_checkingLabel);
    checking->addStretch(1);
    m_checkingLine->hide();
    col->addWidget(m_checkingLine);

    m_card = new ZeroCard(this);
    m_card->setHoverEnabled(false);
    m_rows = new QVBoxLayout(m_card);
    m_rows->setContentsMargins(1, 1, 1, 1);  // inside the card's 1 px border
    m_rows->setSpacing(0);
    col->addWidget(m_card);

    // the single row of "nothing to update" (sits in the card; hidden while there are rows)
    m_idleRow = new QWidget(m_card);
    m_idleRow->setObjectName(QStringLiteral("zeroUpdatesIdleRow"));
    m_idleRow->setFixedHeight(kRowHeight);
    auto* idle = new QHBoxLayout(m_idleRow);
    idle->setContentsMargins(Tokens::spaceLg, 0, Tokens::spaceLg, 0);
    idle->setSpacing(Tokens::spaceMd);
    m_idleIcon = new QLabel(m_idleRow);
    m_idleIcon->setFixedSize(kIdleIconSize, kIdleIconSize);
    idle->addWidget(m_idleIcon);
    auto* idleTexts = new QVBoxLayout;
    idleTexts->setContentsMargins(0, 0, 0, 0);
    idleTexts->setSpacing(0);
    idleTexts->addStretch(1);
    m_idleTitle = new QLabel(m_idleRow);
    Style::applyRole(m_idleTitle, Role::Label);
    idleTexts->addWidget(m_idleTitle);
    m_idleCaption = new QLabel(m_idleRow);
    Style::applyRole(m_idleCaption, Role::Caption);
    Style::setProperty(m_idleCaption, "zeroTone", "secondary");
    idleTexts->addWidget(m_idleCaption);
    idleTexts->addStretch(1);
    idle->addLayout(idleTexts, 1);
    m_idleButton = new CompactButton(tr("Check now"), m_idleRow);
    m_idleButton->setObjectName(QStringLiteral("zeroUpdatesCheckNow"));
    Style::setProperty(m_idleButton, "zeroVariant", "ghost");
    Style::setProperty(m_idleButton, "zeroSize", "sm");
    m_idleButton->setFixedHeight(kButtonHeight);
    m_idleButton->setCursor(Qt::PointingHandCursor);
    m_idleButton->setAutoDefault(false);
    connect(m_idleButton, &QPushButton::clicked, this, [this]() {
        if (auto* e = engine()) {
            e->checkNow();
        }
    });
    idle->addWidget(m_idleButton);
    m_idleRow->hide();
    m_rows->addWidget(m_idleRow);

    m_checked = new QLabel(this);
    Style::applyRole(m_checked, Role::Caption);
    Style::setProperty(m_checked, "zeroTone", "tertiary");
    m_checked->hide();
    col->addWidget(m_checked);

    hide();

    if (!m_sweep) {
        auto* app = APPLICATION_DYN;
        m_sweep = app ? app->zeroUpdates() : nullptr;
    }
    if (auto* e = engine()) {
        connect(e, &Updates::UpdateSweep::started, this, &HomeUpdatesSection::schedule);
        connect(e, &Updates::UpdateSweep::progress, this, &HomeUpdatesSection::schedule);
        connect(e, &Updates::UpdateSweep::finished, this, &HomeUpdatesSection::schedule);
        connect(e, &Updates::UpdateSweep::resultsChanged, this, &HomeUpdatesSection::schedule);
        connect(e, &Updates::UpdateSweep::stateChanged, this, &HomeUpdatesSection::schedule);
        connect(e, &Updates::UpdateSweep::problem, this, [this](const QString&, const QString& message) {
            ZeroToast::show(window(), message, ZeroToast::Kind::Warning);
        });
    }
    connect(&Theme::instance(), &Theme::changed, this, &HomeUpdatesSection::schedule);
    rebuild();
}

Updates::UpdateSweep* HomeUpdatesSection::engine() const
{
    return m_sweep.data();
}

void HomeUpdatesSection::schedule()
{
    m_timer.start();
}

void HomeUpdatesSection::refresh()
{
    m_timer.stop();
    rebuild();
}

void HomeUpdatesSection::rebuild()
{
    m_timer.stop();
    // The rows are rebuilt from scratch: a click handler never gets here synchronously (the engine's signals only schedule).
    for (QWidget* row : std::as_const(m_rowWidgets)) {
        m_rows->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    m_rowWidgets.clear();
    m_rowButtons.clear();
    m_rowIds.clear();
    m_rowNames.clear();
    m_rowCaptions.clear();
    m_rowStatuses.clear();

    auto* sweep = engine();
    const QList<Updates::InstanceUpdates> results = sweep ? sweep->results() : QList<Updates::InstanceUpdates>();
    const bool checking = sweep && sweep->checking();
    // Visible whenever an instance can be checked (or there is something to show); the engine is the single source of truth.
    const bool visible = !results.isEmpty() || checking || (sweep && sweep->hasEligibleInstances());
    setVisible(visible);
    if (!visible) {
        return;
    }

    // the line of a running check
    m_checkingLine->setVisible(checking);
    if (checking) {
        const QString current = sweep->currentName();
        const int total = sweep->checkTotal();
        const int index = std::min(sweep->checkedCount() + 1, std::max(total, 1));
        m_checkingLabel->setText(current.isEmpty() ? tr("Checking for updates…") : tr("Checking %1… (%2 of %3)").arg(current).arg(index).arg(total));
    }
    m_checkAgain->setEnabled(!checking);
    m_checkAgain->setVisible(!results.isEmpty());

    bool anyIdle = false;
    for (int i = 0; i < results.size(); ++i) {
        const Updates::InstanceUpdates& item = results.at(i);
        const Updates::UpdateSweep::State state = sweep->stateOf(item.instanceId);
        anyIdle = anyIdle || state == Updates::UpdateSweep::State::Idle;
        const QString id = item.instanceId;

        auto* row = new UpdateRow(i == 0, i == results.size() - 1, m_card);
        row->onClick = [this, id]() { emit openInstanceRequested(id); };
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(Tokens::spaceLg, 0, Tokens::spaceLg, 0);
        lay->setSpacing(Tokens::spaceMd);

        auto* icon = new QLabel(row);
        icon->setFixedSize(kIconSize, kIconSize);
        icon->setPixmap(m_icons(id).pixmap(QSize(kIconSize, kIconSize), devicePixelRatioF()));
        icon->setScaledContents(true);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(icon);

        auto* texts = new QVBoxLayout;
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        texts->addStretch(1);
        auto* name = new ElidedLabel(row);
        Style::applyRole(name, Role::Label);
        QFont f = name->font();
        f.setWeight(QFont::DemiBold);
        name->setFont(f);
        name->setFullText(item.instanceName);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        texts->addWidget(name);
        auto* caption = new ElidedLabel(row);
        Style::applyRole(caption, Role::Caption);
        Style::setProperty(caption, "zeroTone", "secondary");
        const QString captionText = Updates::captionFor(item.entries);
        caption->setFullText(captionText);
        caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        texts->addWidget(caption);
        texts->addStretch(1);
        lay->addLayout(texts, 1);

        const QString status = statusText(state);
        auto* button = new CompactButton(tr("Update"), row);
        button->setObjectName(QStringLiteral("zeroUpdatesRowButton"));
        Style::setProperty(button, "zeroVariant", "secondary");
        Style::setProperty(button, "zeroSize", "sm");
        button->setFixedHeight(kButtonHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tr("Update the mods of %1").arg(item.instanceName));
        connect(button, &QPushButton::clicked, this, [this, id]() {
            if (auto* e = engine()) {
                e->updateInstance(id);
            }
        });
        lay->addWidget(button);
        button->setVisible(state == Updates::UpdateSweep::State::Idle);

        auto* busy = new QWidget(row);
        busy->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* busyLay = new QHBoxLayout(busy);
        busyLay->setContentsMargins(0, 0, 0, 0);
        busyLay->setSpacing(Tokens::spaceSm);
        busyLay->addWidget(new ZeroSpinner(ZeroSpinner::Small, busy));
        auto* busyText = new QLabel(status, busy);
        Style::applyRole(busyText, Role::Caption);
        Style::setProperty(busyText, "zeroTone", "secondary");
        busyLay->addWidget(busyText);
        busy->setVisible(state != Updates::UpdateSweep::State::Idle);
        lay->addWidget(busy);

        m_rows->addWidget(row);
        m_rowWidgets.append(row);
        m_rowButtons.append(button);
        m_rowIds.append(id);
        m_rowNames.append(item.instanceName);
        m_rowCaptions.append(captionText);
        m_rowStatuses.append(status);
    }
    // "nothing to update": one row in the card, only while no check runs
    const bool showIdle = results.isEmpty() && !checking;
    m_idleRow->setVisible(showIdle);
    if (showIdle) {
        const qint64 at = sweep->checkedAt();
        const auto& theme = Theme::current();
        m_idleIcon->setPixmap(Icons::pixmap(at > 0 ? QStringLiteral("circle-check") : QStringLiteral("refresh-cw"), theme.textSecondary,
                                            kIdleIconSize, devicePixelRatioF()));
        m_idleTitle->setText(at > 0 ? tr("Your mods are up to date") : tr("Check your instances for mod updates"));
        m_idleCaption->setText(at > 0 ? tr("Checked %1").arg(lowerFirst(Format::relativeTime(at))) : tr("Not checked yet"));
    }
    m_card->setVisible(!results.isEmpty() || showIdle);
    m_updateAll->setVisible(!results.isEmpty());
    m_updateAll->setEnabled(anyIdle);

    const qint64 checkedAt = sweep->checkedAt();
    m_checked->setVisible(checkedAt > 0 && !results.isEmpty());
    if (checkedAt > 0) {
        m_checked->setText(tr("Checked %1").arg(lowerFirst(Format::relativeTime(checkedAt))));
    }
}

QString HomeUpdatesSection::rowName(int row) const
{
    return m_rowNames.value(row);
}

QString HomeUpdatesSection::rowCaption(int row) const
{
    return m_rowCaptions.value(row);
}

QString HomeUpdatesSection::rowStatus(int row) const
{
    return m_rowStatuses.value(row);
}

QPushButton* HomeUpdatesSection::rowButton(int row) const
{
    return m_rowButtons.value(row, nullptr);
}

void HomeUpdatesSection::clickRow(int row)
{
    if (row >= 0 && row < m_rowIds.size()) {
        emit openInstanceRequested(m_rowIds.at(row));
    }
}

QString HomeUpdatesSection::checkingText() const
{
    return m_checkingLine->isHidden() ? QString() : m_checkingLabel->text();
}

QString HomeUpdatesSection::idleTitle() const
{
    return m_idleRow->isHidden() ? QString() : m_idleTitle->text();
}

QString HomeUpdatesSection::idleCaption() const
{
    return m_idleRow->isHidden() ? QString() : m_idleCaption->text();
}

QString HomeUpdatesSection::checkedText() const
{
    return m_checked->isHidden() ? QString() : m_checked->text();
}

}  // namespace Zero
