// SPDX-License-Identifier: GPL-3.0-only
#include "Motion.h"

#include <QtGlobal>
#include <algorithm>

namespace ZeroSetup::Motion {

namespace {
QElapsedTimer& clock()
{
    static QElapsedTimer t = [] {
        QElapsedTimer e;
        e.start();
        return e;
    }();
    return t;
}
qint64 g_frozen = -1;
int g_enabled = -1;  // -1: not decided yet
}  // namespace

qint64 now()
{
    return g_frozen >= 0 ? g_frozen : clock().elapsed();
}

void freezeAt(qint64 ms)
{
    g_frozen = ms;
}

bool frozen()
{
    return g_frozen >= 0;
}

bool enabled()
{
    return g_enabled != 0;
}

void setEnabled(bool on)
{
    g_enabled = on ? 1 : 0;
}

double progress(qint64 startMs, int durMs, QEasingCurve::Type curve, double overshoot)
{
    if (!enabled() || durMs <= 0)
        return 1.0;
    const double t = std::clamp(double(now() - startMs) / double(durMs), 0.0, 1.0);
    QEasingCurve e(curve);
    if (curve == QEasingCurve::OutBack)
        e.setOvershoot(overshoot);
    return e.valueForProgress(t);
}

}  // namespace ZeroSetup::Motion
