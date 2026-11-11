// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - plain-language help for the pages of the settings and instance windows (Prism pages hosted in a
// PageContainer): a one-line summary for the side list, an explanation under the page title, and what each group of
// options does (shown in the help strip when the pointer or the keyboard focus is on an option).
#pragma once

#include <QString>

class QWidget;

namespace Zero {
namespace PageHelp {

struct Page {
    QString summary;  ///< one line under the page name in the side list
    QString about;    ///< two or three sentences under the page title
};

/// Help for a page id ("launcher-settings", "mods", ...). Empty strings for pages without help.
Page forPage(const QString& pageId);

/// What `widget` does, in plain language: its tooltip, else ZERO's text for its object name, else the text of the
/// nearest group of options it sits in. Empty when nothing is known. `title` receives a short heading (the option or
/// group name) when one is found.
QString forWidget(const QWidget* widget, QString* title = nullptr);

/// The tooltip / help text with markup removed (Prism tooltips are often rich text).
QString plainText(const QString& text);

}  // namespace PageHelp
}  // namespace Zero
