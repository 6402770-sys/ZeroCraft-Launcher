// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the four "what to start from" cards of Create Instance (Vanilla, Modded, Modpack, Import). Equal cards,
// 96 px tall, painted on ZeroCard; the chosen card is `elevated` with its icon in `accent`, and a 2 px `accent2` indicator
// at its bottom edge glides to the next choice (200 ms OutQuint, instant with motion off). Keyboard: Left/Right on a card
// moves to and chooses the neighbour; the dialog maps 1-4.
#pragma once

#include <QList>
#include <QVariantAnimation>
#include <QWidget>

#include "ui/zero/ZeroCard.h"

namespace Zero {
namespace Create {

class TypeCard : public ZeroCard {
    Q_OBJECT
   public:
    static constexpr int kHeight = 96;

    TypeCard(const QString& icon, const QString& title, const QString& description, QWidget* parent = nullptr);
    void setChosen(bool chosen);
    bool isChosen() const { return m_chosen; }
    QString title() const { return m_title; }
    /// The focus ring is showing: focus arrived from the keyboard (Tab, Backtab, arrows, shortcuts), not from a click.
    bool isFocusVisible() const { return m_focusVisible; }

   protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

   private:
    QString m_icon;
    QString m_title;
    QString m_description;
    bool m_chosen = false;
    bool m_focusVisible = false;
    Motion::StateAnimator* m_chosenAnim;
};

class TypeCardRow : public QWidget {
    Q_OBJECT
   public:
    explicit TypeCardRow(QWidget* parent = nullptr);

    /// Adds a card; cards are indexed in the order they were added.
    TypeCard* addCard(const QString& icon, const QString& title, const QString& description);
    /// Chooses card `index` (animated indicator) without emitting chosen(). When another card has focus, focus moves to the
    /// chosen one (keeping a keyboard focus ring).
    void setCurrent(int index);
    int current() const { return m_current; }
    int count() const { return int(m_cards.size()); }
    TypeCard* card(int index) const { return m_cards.value(index, nullptr); }
    /// Geometry of the indicator (tests: it ends under the chosen card).
    QRect indicatorGeometry() const;

   signals:
    /// The user chose card `index` (click, Space/Enter, arrows).
    void chosen(int index);

   protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    QRect indicatorTarget(int index) const;
    void moveIndicator(bool animate);

    QList<TypeCard*> m_cards;
    QWidget* m_indicator;
    QVariantAnimation m_glide;
    int m_current = -1;
};

}  // namespace Create
}  // namespace Zero
