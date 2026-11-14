// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance import body (see ImportPanel.h).
#include "ImportPanel.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {
namespace Create {

namespace {
constexpr int kZoneIcon = 32;

/// The one local file a drag carries, or empty (several files, remote URLs, folders' contents are not accepted).
QString singleLocalFile(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls() || mime->urls().size() != 1)
        return {};
    const QUrl url = mime->urls().constFirst();
    return url.isLocalFile() ? url.toLocalFile() : QString();
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Drop zone
// ---------------------------------------------------------------------------------------------------------------------

ImportDropZone::ImportDropZone(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(tr("Drop a .zip or .mrpack here"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceLg, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceSm);
    column->addStretch(1);
    m_icon = new QLabel(this);
    m_icon->setAlignment(Qt::AlignCenter);
    m_title = new QLabel(this);
    Style::applyRole(m_title, Role::Label);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    m_caption = new QLabel(this);
    Style::applyRole(m_caption, Role::Caption);
    m_caption->setAlignment(Qt::AlignCenter);
    m_browse = new QPushButton(this);
    Style::setProperty(m_browse, "zeroVariant", "secondary");
    m_browse->setAutoDefault(false);
    connect(m_browse, &QPushButton::clicked, this, &ImportDropZone::browseRequested);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_browse);
    buttonRow->addStretch(1);
    column->addWidget(m_icon);
    column->addWidget(m_title);
    column->addWidget(m_caption);
    column->addSpacing(Tokens::spaceXs);
    column->addLayout(buttonRow);
    column->addStretch(1);

    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refresh();
        update();
    });
    refresh();
}

void ImportDropZone::setFile(const QString& fileName, const QString& caption)
{
    m_fileName = fileName;
    m_caption->setText(caption);
    refresh();
}

void ImportDropZone::refresh()
{
    const auto& t = Theme::current();
    const bool chosen = !m_fileName.isEmpty();
    m_icon->setPixmap(Icons::pixmap(chosen ? QStringLiteral("file-archive") : QStringLiteral("upload"), chosen ? t.accent : t.textSecondary,
                                    kZoneIcon, devicePixelRatioF()));
    m_title->setText(chosen ? m_fileName : tr("Drop a .zip or .mrpack here"));
    m_caption->setVisible(chosen && !m_caption->text().isEmpty());
    m_browse->setText(chosen ? tr("Choose another file…") : tr("Browse…"));
    m_browse->setAccessibleName(chosen ? tr("Choose another file") : tr("Browse for a .zip or .mrpack file"));
}

void ImportDropZone::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(m_dragging ? t.accent2 : t.borderStrong, 1.5);
    pen.setDashPattern({ 4.0, 4.0 });
    p.setPen(pen);
    p.setBrush(m_dragging ? t.surfaceHover : t.surface);
    p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusLg));
}

void ImportDropZone::dragEnterEvent(QDragEnterEvent* event)
{
    if (singleLocalFile(event->mimeData()).isEmpty())
        return;  // not accepted: the cursor says no
    event->acceptProposedAction();
    m_dragging = true;
    update();
}

void ImportDropZone::dragLeaveEvent(QDragLeaveEvent* event)
{
    QWidget::dragLeaveEvent(event);
    m_dragging = false;
    update();
}

void ImportDropZone::dropEvent(QDropEvent* event)
{
    m_dragging = false;
    update();
    const QString path = singleLocalFile(event->mimeData());
    if (path.isEmpty())
        return;
    event->acceptProposedAction();
    emit fileDropped(path);
}

// ---------------------------------------------------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------------------------------------------------

ImportPanel::ImportPanel(std::function<QString(QWidget*)> chooseFile, QWidget* parent) : QWidget(parent), m_chooseFile(std::move(chooseFile))
{
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(Tokens::spaceSm);

    m_zone = new ImportDropZone(this);
    column->addWidget(m_zone, 1);
    m_fileError = new QLabel(this);
    Style::applyRole(m_fileError, Role::Caption);
    Style::setProperty(m_fileError, "zeroTone", "danger");
    m_fileError->setWordWrap(true);
    m_fileError->hide();
    column->addWidget(m_fileError);

    column->addSpacing(Tokens::spaceMd);
    auto* overline = new QLabel(tr("Or import from a link"), this);
    Style::applyRole(overline, Role::Overline);
    column->addWidget(overline);
    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(tr("https://…"));
    m_url->setAccessibleName(tr("Modpack link"));
    m_url->setClearButtonEnabled(true);
    overline->setBuddy(m_url);
    column->addWidget(m_url);

    auto* captionRow = new QHBoxLayout;
    captionRow->setSpacing(Tokens::spaceSm);
    m_urlCaption = new QLabel(this);
    Style::applyRole(m_urlCaption, Role::Caption);
    m_urlCaption->setWordWrap(true);
    m_urlCaption->setTextFormat(Qt::PlainText);
    m_classic = new QPushButton(tr("Open the classic importer"), this);
    Style::setProperty(m_classic, "zeroVariant", "secondary");
    m_classic->setAutoDefault(false);
    m_classic->hide();
    captionRow->addWidget(m_urlCaption, 1);
    captionRow->addWidget(m_classic);
    column->addLayout(captionRow);

    connect(m_zone, &ImportDropZone::browseRequested, this, [this]() {
        if (!m_chooseFile)
            return;
        const QString path = m_chooseFile(this);
        if (!path.isEmpty())
            setLocalFile(path);
    });
    connect(m_zone, &ImportDropZone::fileDropped, this, &ImportPanel::setLocalFile);
    connect(m_url, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_link = resolveUrl(text);
        if (m_link.kind != ImportSource::Kind::None)
            m_file = {};  // a link replaces the chosen file
        refresh();
    });
    connect(m_classic, &QPushButton::clicked, this, [this]() {
        if (m_link.kind == ImportSource::Kind::CurseForgeLink)
            emit classicImportRequested(m_url->text().trimmed());
    });

    refreshIcons();
    connect(&Theme::instance(), &Theme::changed, this, &ImportPanel::refreshIcons);
    refresh();
}

void ImportPanel::refreshIcons()
{
    const QIcon icon = Icons::icon(QStringLiteral("link"), Theme::current().textTertiary);
    if (m_urlIcon)
        m_urlIcon->setIcon(icon);
    else
        m_urlIcon = m_url->addAction(icon, QLineEdit::LeadingPosition);
}

void ImportPanel::setLocalFile(const QString& path)
{
    m_file = resolveLocalFile(path);
    if (m_file.kind != ImportSource::Kind::None && !m_url->text().isEmpty()) {
        const QSignalBlocker block(m_url);
        m_url->clear();  // a file replaces the link
        m_link = {};
    }
    refresh();
}

void ImportPanel::setUrlText(const QString& text)
{
    m_url->setText(text);
}

void ImportPanel::refresh()
{
    // file
    const bool fileChosen = m_file.kind == ImportSource::Kind::LocalFile;
    const QString fileFormat = detectedFormat(m_file.fileName);
    m_zone->setFile(fileChosen ? m_file.fileName : QString(), fileFormat.isEmpty() ? tr("Will be detected when importing") : fileFormat);
    m_fileError->setText(m_file.kind == ImportSource::Kind::Invalid ? QStringLiteral("%1 %2").arg(m_file.fileName, m_file.error).trimmed()
                                                                     : QString());
    m_fileError->setVisible(m_file.kind == ImportSource::Kind::Invalid);

    // link
    QString caption;
    bool danger = false;
    switch (m_link.kind) {
        case ImportSource::Kind::None:
            break;
        case ImportSource::Kind::RemoteUrl: {
            const QString format = detectedFormat(m_link.fileName);
            caption = format.isEmpty() ? tr("Will be detected when importing") : format;
            break;
        }
        case ImportSource::Kind::CurseForgeLink:
            caption = tr("CurseForge links open in the classic importer");
            break;
        case ImportSource::Kind::Invalid:
        case ImportSource::Kind::LocalFile:
            caption = m_link.error;
            danger = true;
            break;
    }
    m_urlCaption->setText(caption);
    m_urlCaption->setVisible(!caption.isEmpty());
    Style::setProperty(m_urlCaption, "zeroTone", danger ? QStringLiteral("danger") : QString());
    m_classic->setVisible(m_link.kind == ImportSource::Kind::CurseForgeLink);

    // the active source: a file wins (choosing one clears the link), else the link
    m_source = m_file.kind != ImportSource::Kind::None ? m_file : m_link;
    emit sourceChanged();
}

QString ImportPanel::urlCaption() const
{
    return m_urlCaption->isVisibleTo(this) ? m_urlCaption->text() : QString();
}

QString ImportPanel::fileError() const
{
    return m_fileError->isVisibleTo(this) ? m_fileError->text() : QString();
}

}  // namespace Create
}  // namespace Zero
