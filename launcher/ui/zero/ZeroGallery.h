// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - component gallery (developer aid and visual regression target).
#pragma once

#include <QPointer>
#include <QWidget>

class QAbstractButton;

namespace Zero {

class ZeroNavRail;

/// Shows every ZERO component and styled Qt control in one window: type roles, button variants/sizes/states,
/// inputs, check/radio, tabs, lists/tables, progress, spinner, cards, empty state, badges and toasts.
/// Some samples are forced into their hover/pressed state so a single screenshot shows the state set.
class ZeroGallery : public QWidget {
    Q_OBJECT
   public:
    explicit ZeroGallery(QWidget* parent = nullptr);

    ZeroNavRail* navRail() const { return m_rail; }

   /// The "Hover" sample buttons (one per variant). They are not forced into hover (Qt only styles :hover for a real
   /// pointer); tests move the pointer over them to verify and capture the hover state.
   QList<QAbstractButton*> hoverSamples() const;

   private:

    ZeroNavRail* m_rail;
    QList<QPointer<QAbstractButton>> m_hoverSamples;
};

}  // namespace Zero
