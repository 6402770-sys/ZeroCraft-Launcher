// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the two empty states of the Discover pages (docs/zero/CONTENT.md, "ZERO Filter System" > Widgets): filters that
// match nothing, and a provider that could not be reached. Both are ordinary ZeroEmptyState widgets with the right icon,
// texts and buttons; the page shows them in place of the item view and reacts to the signals.
#pragma once

#include <QString>
#include "ui/zero/ZeroEmptyState.h"

class QPushButton;

namespace Zero {
namespace Content {

/// `funnel-x`, "Nothing matches these filters", a body that names the most restrictive filter when the page knows it, a primary
/// "Clear all" and - when the section has default filters - a secondary "Show everything".
class NoMatchState : public ZeroEmptyState {
    Q_OBJECT
   public:
    explicit NoMatchState(QWidget* parent = nullptr);

    /// `restrictiveChip` is the label of the filter that cuts the most ("Loader: Quilt"), empty when unknown.
    void setContext(const QString& restrictiveChip, bool defaultsExist);
    QPushButton* showEverythingButton() const { return m_showEverything; }
    QString bodyText() const { return m_bodyText; }

    /// The body text for `restrictiveChip` (empty = unknown).
    static QString bodyFor(const QString& restrictiveChip);

   signals:
    void clearAllRequested();
    void showEverythingRequested();

   private:
    QPushButton* m_showEverything;
    QString m_bodyText;
};

/// `triangle-alert`, "Couldn't reach <Provider>", the error text and a primary "Retry".
class NetworkErrorState : public ZeroEmptyState {
    Q_OBJECT
   public:
    explicit NetworkErrorState(QWidget* parent = nullptr);

    void setError(const QString& providerName, const QString& errorText);
    QString titleText() const { return m_titleText; }
    QString bodyText() const { return m_bodyText; }

    static QString titleFor(const QString& providerName);

   signals:
    void retryRequested();

   private:
    QString m_titleText;
    QString m_bodyText;
};

}  // namespace Content
}  // namespace Zero
