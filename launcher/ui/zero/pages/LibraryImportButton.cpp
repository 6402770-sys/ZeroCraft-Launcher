// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryImportButton.h"

#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"

namespace Zero {

LibraryImportButton::LibraryImportButton(QWidget* parent) : QPushButton(tr("Import"), parent)
{
    Style::setProperty(this, "zeroVariant", "secondary");
    setIconSize(QSize(16, 16));
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Open a .zip or .mrpack you already have"));
    setAccessibleName(tr("Import"));
    refreshIcon();
    connect(&Theme::instance(), &Theme::changed, this, [this]() { refreshIcon(); });
}

void LibraryImportButton::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    setText(compact ? QString() : tr("Import"));
    refreshIcon();
}

void LibraryImportButton::refreshIcon()
{
    setIcon(Icons::icon(QStringLiteral("file-archive"), Theme::current().text));
}

}  // namespace Zero
