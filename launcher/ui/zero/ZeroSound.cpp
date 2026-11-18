// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroSound.h"

#include <QAbstractButton>
#include <QAudioDevice>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <array>
#include <limits>

#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"
#include "zero/sound/MinecraftSoundCache.h"
#include "ui/zero/ZeroThemeState.h"
#include "zero/theme/CustomTheme.h"

namespace Zero {
namespace Sound {

namespace {

constexpr std::array<const char*, kEventCount> kIds = { "click", "toggle-on", "toggle-off", "navigate", "success", "error", "launch" };

struct Engine {
    Config config;
    bool armed = false;  // install() ran inside the Application: real audio is allowed
    std::function<void(Event)> sink;
    std::function<qint64()> clock;
    qint64 last[kEventCount];
    QString poolSignature;
    QSoundEffect* effects[kEventCount] = {};
    bool deviceKnown = false;
    bool deviceAvailable = false;
    QString minecraftDir;
    Minecraft::Cache* cache = nullptr;

    Engine() { resetThrottle(); }
    void resetThrottle()
    {
        for (qint64& l : last)
            l = std::numeric_limits<qint64>::min() / 2;
    }
};

Engine& engine()
{
    static Engine e;
    return e;
}

qint64 now()
{
    if (engine().clock)
        return engine().clock();
    static QElapsedTimer timer = []() {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

int index(Event e)
{
    return int(e);
}

QString signatureFor(const ThemeSpec& spec)
{
    QString sig = setForSpec(spec);
    for (int i = 0; i < kEventCount; ++i)
        sig += QLatin1Char('|') + sourceFor(spec, Event(i));
    return sig;
}

bool deviceAvailable()
{
    Engine& e = engine();
    if (!e.deviceKnown) {
        e.deviceAvailable = !QMediaDevices::audioOutputs().isEmpty();
        e.deviceKnown = true;
    }
    return e.deviceAvailable;
}

QUrl urlFor(const QString& source)
{
    return source.startsWith(QLatin1String(":/")) ? QUrl(QStringLiteral("qrc") + source) : QUrl::fromLocalFile(source);
}

/// (Re)creates the pool when the theme's sounds changed. Cheap when they did not.
void ensurePool()
{
    Engine& e = engine();
    const ThemeSpec& spec = Theme::current();
    const QString sig = signatureFor(spec);
    if (sig == e.poolSignature)
        return;
    e.poolSignature = sig;
    for (QSoundEffect*& fx : e.effects) {
        if (fx) {
            fx->stop();
            fx->deleteLater();
            fx = nullptr;
        }
    }
    if (!deviceAvailable())
        return;
    for (int i = 0; i < kEventCount; ++i) {
        const QString source = sourceFor(spec, Event(i));
        if (source.isEmpty())
            continue;
        auto* fx = new QSoundEffect(QCoreApplication::instance());
        fx->setSource(urlFor(source));  // loads asynchronously
        e.effects[i] = fx;
    }
}

int playingCount()
{
    int n = 0;
    for (QSoundEffect* fx : engine().effects)
        if (fx && fx->isPlaying())
            n++;
    return n;
}

void start(Event event, double volume)
{
    Engine& e = engine();
    if (!e.armed || !deviceAvailable())
        return;
    ensurePool();
    QSoundEffect* fx = e.effects[index(event)];
    if (!fx)
        return;
    fx->setVolume(float(std::clamp(volume * gain(event), 0.0, 1.0)));
    if (fx->status() == QSoundEffect::Loading) {  // just (re)created by a theme change: play as soon as it is loaded
        QObject::connect(
            fx, &QSoundEffect::statusChanged, fx,
            [fx]() {
                if (fx->status() == QSoundEffect::Ready)
                    fx->play();
            },
            Qt::SingleShotConnection);
        return;
    }
    if (fx->isPlaying())
        fx->stop();
    fx->play();
}

bool isOptedOut(const QWidget* w)
{
    return w->property(kOptOutProperty).toBool();
}

/// Is `w` inside an item view (editors and index widgets)? Those are not announced.
bool insideItemView(const QWidget* w)
{
    for (const QWidget* p = w->parentWidget(); p; p = p->parentWidget())
        if (qobject_cast<const QAbstractItemView*>(p))
            return true;
    return false;
}

std::optional<Event> classifyButton(QAbstractButton* b)
{
    if (!b->isEnabled() || isOptedOut(b) || insideItemView(b))
        return std::nullopt;
    const QString forced = b->property(kEventProperty).toString();
    if (forced == QLatin1String("none"))
        return std::nullopt;
    if (forced == QLatin1String("navigate"))
        return Event::Navigate;
    if (forced == QLatin1String("launch"))
        return Event::Launch;
    if (b->isCheckable()) {
        // Radio buttons and segmented controls: choosing the checked one changes nothing; choosing another is a plain press.
        if (b->autoExclusive() || (b->group() && b->group()->exclusive()))
            return b->isChecked() ? std::nullopt : std::optional<Event>(Event::Click);
        return b->isChecked() ? Event::ToggleOff : Event::ToggleOn;  // the state before this activation
    }
    return Event::Click;
}

std::optional<Event> classifyView(QAbstractItemView* view, const QPoint& viewportPos)
{
    if (view->property(kEventProperty).toString() != QLatin1String("navigate") || isOptedOut(view) || !view->isEnabled())
        return std::nullopt;
    const QModelIndex index = view->indexAt(viewportPos);
    if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled))
        return std::nullopt;
    return Event::Navigate;
}

}  // namespace

QString eventId(Event event)
{
    return QString::fromLatin1(kIds[size_t(index(event))]);
}

std::optional<Event> eventFromId(const QString& id)
{
    for (int i = 0; i < kEventCount; ++i)
        if (id == QLatin1String(kIds[size_t(i)]))
            return Event(i);
    return std::nullopt;
}

double gain(Event event)
{
    switch (event) {
        case Event::Click:
            return 0.7;
        case Event::ToggleOn:
        case Event::ToggleOff:
            return 0.8;
        case Event::Navigate:
            return 0.7;
        case Event::Success:
        case Event::Error:
        case Event::Launch:
            return 0.9;
    }
    return 1.0;
}

void setConfig(const Config& config)
{
    Config c = config;
    c.volume = std::clamp(c.volume, 0, 100);
    engine().config = c;
}

Config config()
{
    return engine().config;
}

QString setForSpec(const ThemeSpec& spec)
{
    if (Custom::soundSetIds().contains(spec.soundSet))
        return spec.soundSet;
    return spec.styleId() == QLatin1String("minecraft") ? QStringLiteral("retro") : QStringLiteral("soft");
}

QString resourcePath(const QString& set, Event event)
{
    if (set != QLatin1String("retro") && set != QLatin1String("soft"))
        return {};
    return QStringLiteral(":/zero/sounds/") + set + QLatin1Char('/') + eventId(event) + QStringLiteral(".wav");
}

QString sourceFor(const ThemeSpec& spec, Event event)
{
    const QString own = spec.soundFiles.value(eventId(event));
    if (!own.isEmpty() && QFileInfo(own).isFile())
        return own;
    const QString set = setForSpec(spec);
    const Engine& e = engine();
    if (set == QLatin1String("retro") && e.config.minecraftPack && !e.minecraftDir.isEmpty()) {
        const QString converted = e.minecraftDir + QLatin1Char('/') + eventId(event) + QStringLiteral(".wav");
        if (QFileInfo(converted).isFile())
            return converted;
    }
    return resourcePath(set, event);
}

void setMinecraftDir(const QString& dir)
{
    engine().minecraftDir = dir;
}

QString minecraftDir()
{
    return engine().minecraftDir;
}

Minecraft::Cache* minecraftCache()
{
    return engine().cache;
}

void preparePack()
{
    if (engine().cache && engine().config.minecraftPack)
        engine().cache->prepare();
}

void play(Event event)
{
    Engine& e = engine();
    if (!e.config.enabled || e.config.volume <= 0)
        return;
    const qint64 t = now();
    if (t - e.last[index(event)] < kThrottleMs)
        return;
    if (e.sink) {
        e.last[index(event)] = t;
        e.sink(event);
        return;
    }
    if (!e.armed || playingCount() >= kMaxOverlap)
        return;
    e.last[index(event)] = t;
    start(event, e.config.volume / 100.0);
}

void preview(Event event)
{
    Engine& e = engine();
    if (e.sink) {
        e.sink(event);
        return;
    }
    const int volume = e.config.enabled && e.config.volume > 0 ? e.config.volume : kDefaultVolume;
    start(event, volume / 100.0);
}

std::optional<Event> Filter::classify(QObject* watched, QEvent* ev)
{
    switch (ev->type()) {
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() != Qt::LeftButton)
                return std::nullopt;
            if (auto* b = qobject_cast<QAbstractButton*>(watched)) {
                // Only a press that started on the button and is released on it counts (isDown: the press was seen, it was not dragged off).
                if (!b->isDown() || !b->rect().contains(me->position().toPoint()))
                    return std::nullopt;
                return classifyButton(b);
            }
            if (auto* w = qobject_cast<QWidget*>(watched)) {
                if (auto* view = qobject_cast<QAbstractItemView*>(w->parentWidget()); view && view->viewport() == w)
                    return classifyView(view, me->position().toPoint());
            }
            return std::nullopt;
        }
        case QEvent::KeyRelease: {
            auto* ke = static_cast<QKeyEvent*>(ev);
            auto* b = qobject_cast<QAbstractButton*>(watched);
            if (b && !ke->isAutoRepeat() && ke->key() == Qt::Key_Space && b->isDown())
                return classifyButton(b);
            return std::nullopt;
        }
        case QEvent::KeyPress: {
            auto* ke = static_cast<QKeyEvent*>(ev);
            auto* pb = qobject_cast<QPushButton*>(watched);
            if (pb && !ke->isAutoRepeat() && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (pb->autoDefault() || pb->isDefault()))
                return classifyButton(pb);
            return std::nullopt;
        }
        default:
            return std::nullopt;
    }
}

bool Filter::eventFilter(QObject* watched, QEvent* ev)
{
    switch (ev->type()) {
        case QEvent::MouseButtonRelease:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            if (auto event = classify(watched, ev))
                play(*event);
            break;
        default:
            break;
    }
    return false;  // never consumes
}

QObject* install(QObject* app)
{
    static QPointer<Filter> existing;
    if (existing)
        return existing.data();
    existing = new Filter(app);
    app->installEventFilter(existing);

    auto* application = qobject_cast<Application*>(app);
    if (!application)
        return existing.data();

    // Settings -> config.
    const auto sync = [application]() {
        Config c;
        c.enabled = application->settings()->get(kEnabledSetting).toBool();
        c.volume = application->settings()->get(kVolumeSetting).toInt();
        c.minecraftPack = application->settings()->get(kPackSetting).toString() == QLatin1String(kPackMinecraft);
        setConfig(c);
    };
    sync();

    // The Minecraft pack: converts the sounds of the game's local assets into <data>/zero/sounds/minecraft (asynchronously).
    const QString dataRoot = application->dataRoot();
    auto* cache = new Minecraft::Cache(dataRoot + QStringLiteral("/assets"), dataRoot + QStringLiteral("/zero/sounds/minecraft"), existing.data());
    engine().cache = cache;
    QObject::connect(cache, &Minecraft::Cache::changed, existing.data(), [cache]() {
        setMinecraftDir(cache->state() == Minecraft::Cache::State::Ready ? cache->dir() : QString());
        if (engine().armed && !engine().sink)
            ensurePool();
    });
    if (auto setting = application->settings()->getSetting(kPackSetting))
        QObject::connect(setting.get(), &Setting::SettingChanged, existing.data(), [sync](const Setting&, const QVariant&) {
            sync();
            preparePack();
            // The converted files may already be ready (no Cache::changed then): rebuild the pool so the pick takes effect now.
            if (engine().armed && !engine().sink)
                ensurePool();
        });

    for (const char* key : { kEnabledSetting, kVolumeSetting })
        if (auto setting = application->settings()->getSetting(key))
            QObject::connect(setting.get(), &Setting::SettingChanged, existing.data(), [sync](const Setting&, const QVariant&) { sync(); });

    // Arm and preload once the event loop runs, so startup is never delayed by loading sounds; later theme changes reload the pool.
    QTimer::singleShot(0, existing.data(), []() {
        Engine& e = engine();
        e.armed = true;
        if (e.sink)
            return;
        ensurePool();
        preparePack();
        QObject::connect(&Theme::instance(), &Theme::changed, QCoreApplication::instance(), []() { ensurePool(); });
        QObject::connect(new QMediaDevices(QCoreApplication::instance()), &QMediaDevices::audioOutputsChanged, QCoreApplication::instance(),
                         []() {
                             engine().deviceKnown = false;
                             engine().poolSignature.clear();
                             ensurePool();
                         });
    });
    return existing.data();
}

void setSink(std::function<void(Event)> sink)
{
    engine().sink = std::move(sink);
}

void setClock(std::function<qint64()> clock)
{
    engine().clock = std::move(clock);
}

void resetThrottle()
{
    engine().resetThrottle();
}

}  // namespace Sound
}  // namespace Zero
