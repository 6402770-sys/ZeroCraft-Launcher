// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - a row of single-choice chips (Create Instance: the Minecraft version filter and the loader choice).
// Chips look like the Library filter chips: 28 px, `surface` + `border`; the chosen one is `elevated` + `text` with a 6 px
// `accent2` dot. The row wraps when it runs out of width. Keyboard: Tab reaches each chip, Space/Enter chooses,
// Left/Right move to and choose the neighbour.
#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QAbstractButton;

namespace Zero {

class FlowLayout;

namespace Create {

class ChoiceChip;

class ChoiceChipRow : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kChipHeight = 28;

    explicit ChoiceChipRow(const QString& accessibleName, QWidget* parent = nullptr);

    /// Shows exactly these (id, label) options. The current choice is kept when its id is still there, otherwise none.
    void setOptions(const QList<QPair<QString, QString>>& options);
    /// Chooses `id` (or nothing for an unknown/empty id) without emitting currentChanged.
    void setCurrent(const QString& id);
    QString current() const { return m_current; }

    QStringList ids() const;
    QStringList labels() const;
    /// The chip for `id`, or nullptr.
    QAbstractButton* chip(const QString& id) const;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

   signals:
    /// The user chose another chip.
    void currentChanged(const QString& id);

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

   private:
    void choose(const QString& id, bool notify);

    FlowLayout* m_flow;
    QList<ChoiceChip*> m_chips;
    QString m_current;
};

}  // namespace Create
}  // namespace Zero
