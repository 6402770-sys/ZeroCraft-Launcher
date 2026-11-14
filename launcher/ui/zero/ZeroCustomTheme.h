// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - a user-made ZERO theme loaded from <themes dir>/<slug>/zero-theme.json.
#pragma once

#include <memory>

#include "ui/zero/ZeroTheme.h"
#include "zero/theme/CustomTheme.h"

namespace Zero {

/// A ZERO theme whose spec is derived from a zero-theme.json (see Custom::deriveSpec). Registered by ThemeManager for every
/// theme folder holding that file; the id is "zero-custom-<folder name>".
class ZeroCustomTheme : public ZeroTheme {
   public:
    /// Null (with a warning logged) when the folder holds no valid definition: unreadable, unparsable or a newer format.
    static std::unique_ptr<ZeroCustomTheme> load(const QString& folder);

    ~ZeroCustomTheme() override = default;

    QString id() override;
    QString name() override;
    QString tooltip() override;

    const QString& folder() const { return m_folder; }
    const Custom::Definition& definition() const { return m_def; }
    /// The derived spec (what apply() uses, before the motion setting is merged in): for previews such as the Appearance cards.
    ThemeSpec derivedSpec() { return baseSpec(); }

   protected:
    ThemeSpec baseSpec() override;

   private:
    ZeroCustomTheme(QString folder, Custom::Definition def);

    QString m_folder;
    Custom::Definition m_def;
    QString m_id;
};

}  // namespace Zero
