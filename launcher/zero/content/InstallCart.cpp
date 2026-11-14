// SPDX-License-Identifier: GPL-3.0-only
#include "InstallCart.h"

#include <algorithm>

namespace Zero::Content {

namespace {

/// "1 mod" / "3 mods": the count decides the noun (no plural forms).
QString noun(int count, const QString& one, const QString& many)
{
    return (count == 1 ? one : many).arg(count);
}

QStringList parts(int mods, int resourcePacks, int shaders)
{
    QStringList out;
    if (mods > 0) {
        out << noun(mods, QObject::tr("%1 mod"), QObject::tr("%1 mods"));
    }
    if (resourcePacks > 0) {
        out << noun(resourcePacks, QObject::tr("%1 resource pack"), QObject::tr("%1 resource packs"));
    }
    if (shaders > 0) {
        out << noun(shaders, QObject::tr("%1 shader"), QObject::tr("%1 shaders"));
    }
    return out;
}

}  // namespace

QString InstallCart::kindId(Kind kind)
{
    switch (kind) {
        case Kind::Mod:
            return QStringLiteral("mod");
        case Kind::ResourcePack:
            return QStringLiteral("resourcepack");
        case Kind::Shader:
            return QStringLiteral("shader");
        case Kind::Modpack:
        case Kind::World:
            break;
    }
    return {};
}

bool InstallCart::takesKind(Kind kind)
{
    return kind == Kind::Mod || kind == Kind::ResourcePack || kind == Kind::Shader;
}

QString InstallCart::keyFor(Kind kind, const ContentItem& item)
{
    if (!takesKind(kind) || item.id.isEmpty()) {
        return {};
    }
    return kindId(kind) + QLatin1Char(':') + providerId(item.provider) + QLatin1Char(':') + item.id;
}

InstallCart::AddResult InstallCart::add(Kind kind, const ContentItemPtr& item)
{
    if (!item) {
        return AddResult::Invalid;
    }
    const QString key = keyFor(kind, *item);
    if (key.isEmpty()) {
        return AddResult::Invalid;
    }
    if (contains(key)) {
        return AddResult::AlreadyIn;
    }
    if (count() >= kCap) {
        return AddResult::Full;
    }
    m_entries.append({ kind, item, item->provider, key });
    return AddResult::Added;
}

bool InstallCart::remove(const QString& key)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).key == key) {
            m_entries.removeAt(i);
            return true;
        }
    }
    return false;
}

InstallCart::ToggleResult InstallCart::toggle(Kind kind, const ContentItemPtr& item)
{
    if (!item) {
        return ToggleResult::Invalid;
    }
    const QString key = keyFor(kind, *item);
    if (key.isEmpty()) {
        return ToggleResult::Invalid;
    }
    if (remove(key)) {
        return ToggleResult::Removed;
    }
    switch (add(kind, item)) {
        case AddResult::Added:
            return ToggleResult::Added;
        case AddResult::Full:
            return ToggleResult::Full;
        default:
            return ToggleResult::Invalid;
    }
}

bool InstallCart::contains(const QString& key) const
{
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&key](const CartEntry& e) { return e.key == key; });
}

int InstallCart::countOf(Kind kind) const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [kind](const CartEntry& e) { return e.kind == kind; }));
}

QList<CartEntry> InstallCart::entriesOf(Kind kind) const
{
    QList<CartEntry> out;
    for (const auto& entry : m_entries) {
        if (entry.kind == kind) {
            out.append(entry);
        }
    }
    return out;
}

QStringList InstallCart::countParts(int mods, int resourcePacks, int shaders)
{
    return parts(mods, resourcePacks, shaders);
}

QString InstallCart::countsText(int mods, int resourcePacks, int shaders)
{
    return parts(mods, resourcePacks, shaders).join(QStringLiteral(", "));
}

QString InstallCart::summaryText(int mods, int resourcePacks, int shaders)
{
    const QString counts = countsText(mods, resourcePacks, shaders);
    return counts.isEmpty() ? QString() : QObject::tr("%1 selected").arg(counts);
}

// ---- model ----

InstallCart::ToggleResult InstallCartModel::toggle(Kind kind, const ContentItemPtr& item)
{
    const auto result = m_cart.toggle(kind, item);
    if (result == InstallCart::ToggleResult::Added || result == InstallCart::ToggleResult::Removed) {
        emit changed();
    } else if (result == InstallCart::ToggleResult::Full) {
        emit limitReached();
    }
    return result;
}

InstallCart::AddResult InstallCartModel::add(Kind kind, const ContentItemPtr& item)
{
    const auto result = m_cart.add(kind, item);
    if (result == InstallCart::AddResult::Added) {
        emit changed();
    } else if (result == InstallCart::AddResult::Full) {
        emit limitReached();
    }
    return result;
}

bool InstallCartModel::remove(const QString& key)
{
    if (!m_cart.remove(key)) {
        return false;
    }
    emit changed();
    return true;
}

void InstallCartModel::clear()
{
    if (m_cart.isEmpty()) {
        return;
    }
    m_cart.clear();
    emit changed();
}

}  // namespace Zero::Content
