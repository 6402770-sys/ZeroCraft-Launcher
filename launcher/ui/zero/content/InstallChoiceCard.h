// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - a one-of-N choice drawn as a card: a radio dot, a title and a caption under it. Used by the modpack install dialog
// for "New instance / Update an instance" (side by side) and for the list of instances an update can target (stacked rows).
//
// A checkable button; the owner puts the cards of one choice in an exclusive QButtonGroup (radio behaviour: arrows move between them,
// a disabled one is skipped, clicking the checked one keeps it). Disabled = 40 % opacity like every ZERO control.
#pragma once

#include <QAbstractButton>
#include <QString>

namespace Zero::Content {

class InstallChoiceCard : public QAbstractButton {
    Q_OBJECT
   public:
    /// `captionLines`: lines reserved for the caption (it wraps inside them); 0 = no caption.
    InstallChoiceCard(const QString& title, const QString& caption, int captionLines, QWidget* parent = nullptr);

    QString title() const { return text(); }
    QString caption() const { return m_caption; }
    void setCaption(const QString& caption);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

   protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

   private:
    int textLeft() const;

    QString m_caption;
    int m_captionLines;
};

}  // namespace Zero::Content
