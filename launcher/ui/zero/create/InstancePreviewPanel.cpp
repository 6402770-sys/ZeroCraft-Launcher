// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance live preview (see InstancePreviewPanel.h).
#include "InstancePreviewPanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Create {

namespace {
constexpr int kArtworkHeight = 132;
constexpr int kArtworkIcon = 48;
}  // namespace

/// The artwork tile: `elevated` stone (texture in textured themes), the kind's icon and an optional caption.
class InstancePreviewPanel::Artwork : public QWidget {
   public:
    explicit Artwork(QWidget* parent) : QWidget(parent)
    {
        setFixedHeight(kArtworkHeight);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }
    void set(const QString& icon, const QString& caption)
    {
        m_icon = icon;
        m_caption = caption;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPainterPath shape = Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg);
        p.save();
        p.setClipPath(shape);
        Paint::paintBackground(p, rect(), t.elevated);
        p.restore();
        p.setPen(QPen(t.border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);

        const QFont font = Style::font(Role::Label);
        const QFontMetrics fm(font);
        const int captionHeight = m_caption.isEmpty() ? 0 : fm.height() + Tokens::spaceSm;
        const int top = (height() - kArtworkIcon - captionHeight) / 2;
        if (!m_icon.isEmpty())
            Icons::icon(m_icon, t.accent).paint(&p, QRect((width() - kArtworkIcon) / 2, top, kArtworkIcon, kArtworkIcon));
        if (!m_caption.isEmpty()) {
            p.setFont(font);
            p.setPen(t.text);
            p.drawText(QRect(0, top + kArtworkIcon + Tokens::spaceSm, width(), fm.height()), Qt::AlignCenter,
                       fm.elidedText(m_caption, Qt::ElideRight, width() - 2 * Tokens::spaceMd));
        }
    }

   private:
    QString m_icon;
    QString m_caption;
};

InstancePreviewPanel::InstancePreviewPanel(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(kWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAccessibleName(tr("Preview"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceMd);

    m_artwork = new Artwork(this);
    column->addWidget(m_artwork);

    m_name = new QLabel(this);
    Style::applyRole(m_name, Role::Heading);
    m_name->setWordWrap(true);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setAccessibleName(tr("Instance name"));
    column->addWidget(m_name);

    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(Tokens::spaceMd);
    m_grid->setVerticalSpacing(Tokens::spaceSm);
    m_grid->setColumnStretch(1, 1);
    column->addLayout(m_grid);
    column->addStretch(1);

    connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
}

void InstancePreviewPanel::setArtwork(const QString& icon, const QString& caption)
{
    m_artwork->set(icon, caption);
}

void InstancePreviewPanel::setName(const QString& name)
{
    m_name->setText(name.trimmed());
    m_name->setVisible(!name.trimmed().isEmpty());
}

QString InstancePreviewPanel::name() const
{
    return m_name->isVisibleTo(this) ? m_name->text() : QString();
}

void InstancePreviewPanel::setRows(const QList<QPair<QString, QString>>& rows)
{
    QList<QPair<QString, QString>> shown;
    for (const auto& r : rows)
        if (!r.second.trimmed().isEmpty())
            shown.append(r);
    if (shown == m_rows)
        return;
    m_rows = shown;
    // Reuse the label pairs; create or hide as needed.
    while (m_rowLabels.size() < m_rows.size()) {
        auto* label = new QLabel(this);
        Style::applyRole(label, Role::Caption);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        auto* value = new QLabel(this);
        Style::applyRole(value, Role::Body);
        value->setWordWrap(true);
        value->setTextFormat(Qt::PlainText);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const int row = int(m_rowLabels.size());
        m_grid->addWidget(label, row, 0, Qt::AlignLeft | Qt::AlignTop);
        m_grid->addWidget(value, row, 1);
        m_rowLabels.append(qMakePair(label, value));
    }
    for (int i = 0; i < m_rowLabels.size(); ++i) {
        const bool used = i < m_rows.size();
        m_rowLabels.at(i).first->setVisible(used);
        m_rowLabels.at(i).second->setVisible(used);
        if (used) {
            m_rowLabels.at(i).first->setText(m_rows.at(i).first);
            m_rowLabels.at(i).second->setText(m_rows.at(i).second);
            m_rowLabels.at(i).second->setAccessibleName(m_rows.at(i).first);
        }
    }
}

QList<QPair<QString, QString>> InstancePreviewPanel::visibleRows() const
{
    return m_rows;
}

void InstancePreviewPanel::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(t.border, 1));
    p.setBrush(t.surface);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), t.radiusLg));
}

}  // namespace Create
}  // namespace Zero
