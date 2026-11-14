// SPDX-License-Identifier: GPL-3.0-only
// The scene behind every screen: night sky with twinkling stars, stone ground with a grass edge, the pixel ZERO mark
// built from blocks that drop in, the wordmark and the tagline. Pages (buttons, bar ...) are children of this widget.
#pragma once

#include <QPixmap>
#include <QVector>
#include <QWidget>

class QTimer;

namespace ZeroSetup {

class SetupCanvas : public QWidget
{
    Q_OBJECT
   public:
    explicit SetupCanvas(QWidget* parent = nullptr);

    void setWindowCaption(const QString& s);
    /// Cross-fades to a new tagline (instant with reduced motion).
    void setTagline(const QString& s);
    /// 0 = hero in place, 1 = drifted left and faded out (Options page in front).
    void setHeroShift(double t);
    void setBobbing(bool on);
    /// Restart the block-by-block drop of the mark at this time (Motion::now() clock).
    void setMarkStart(qint64 ms);
    void setMarkVisible(bool on);
    /// Screenshots: finish the tagline cross-fade.
    void settleTagline() { m_tagChangeMs = -100000; update(); }
    int markBlockCount() const { return m_blocks.size(); }

   signals:
    void dragRequested();

   protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

   private:
    struct Star
    {
        QPointF pos;
        double phase;
    };
    struct Block
    {
        QPoint cell;
        QRgb color;
    };
    void rebuildBackground();
    void tick();
    void paintHero(QPainter& p);
    void paintMark(QPainter& p, double baseOpacity);
    void paintWordmark(QPainter& p, double alpha);

    QPixmap m_bg;
    qreal m_bgDpr = 0;
    QVector<Star> m_stars;
    QVector<Block> m_blocks;
    QString m_caption, m_tagline, m_prevTagline;
    qint64 m_tagChangeMs = -100000;
    qint64 m_markStart = 0;
    double m_shift = 0;
    bool m_bob = false;
    bool m_markVisible = true;
    QTimer* m_timer;
    int m_tickCount = 0;
};

}  // namespace ZeroSetup
