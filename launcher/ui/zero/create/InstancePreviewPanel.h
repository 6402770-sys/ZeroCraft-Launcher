// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - live preview of the instance Create Instance will make (300 px column): an artwork tile, the name and
// label/value rows. Only real values are shown: a row without a value is hidden, never "—".
#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

class QLabel;
class QGridLayout;

namespace Zero {
namespace Create {

class InstancePreviewPanel : public QWidget {
    Q_OBJECT
   public:
    static constexpr int kWidth = 300;

    explicit InstancePreviewPanel(QWidget* parent = nullptr);

    /// Lucide icon of the artwork tile and an optional line under it (the loader name for Modded).
    void setArtwork(const QString& icon, const QString& caption = {});
    void setName(const QString& name);
    /// (label, value) rows in order; rows with an empty value are left out.
    void setRows(const QList<QPair<QString, QString>>& rows);

    QString name() const;
    /// The rows on screen (label -> value), for tests.
    QList<QPair<QString, QString>> visibleRows() const;

   protected:
    void paintEvent(QPaintEvent* event) override;

   private:
    class Artwork;

    Artwork* m_artwork;
    QLabel* m_name;
    QGridLayout* m_grid;
    QList<QPair<QLabel*, QLabel*>> m_rowLabels;
    QList<QPair<QString, QString>> m_rows;
};

}  // namespace Create
}  // namespace Zero
