// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Mod Intelligence: version comparison and the two version-requirement grammars mods use (Fabric / Quilt predicates,
// Forge / NeoForge Maven ranges). Pure functions, no I/O.
// Rule: every matcher answers Yes / No / Unknown. Unknown (version or predicate not understood, snapshot builds, ambiguous
// qualifiers) must never be turned into a definite claim by the caller.
#pragma once

#include <QString>
#include <optional>

namespace Zero {
namespace Mods {

enum class Tri { Yes, No, Unknown };

/// Three-way comparison (-1 / 0 / 1), nullopt when the two versions cannot be ordered reliably:
///  - either one does not parse (snapshots like `23w13a`, `b1.7.3`, `${placeholder}`, free text);
///  - the order would depend on whether the qualifier is read the semver way or the Maven way (`1.20.1-47.1.0`, `1.0-foo` vs `1.0`).
/// Numeric segments compare as numbers, missing segments count as 0 (`1.20` == `1.20.0`), build metadata (`+...`) is ignored,
/// known pre-release tags (alpha, beta, rc, pre, snapshot, ...) sort below the release.
std::optional<int> compareVersions(const QString& a, const QString& b);

/// Fabric / Quilt predicate: `*`, `=x`, `>x`, `>=x`, `<x`, `<=x`, `~x` (same minor), `^x` (same major), `1.2.x` / `1.2.*` wildcards,
/// space separated terms are AND, `||` separated alternatives are OR (an array of predicates is stored joined with `||`).
/// An empty predicate means "any version" and gives Yes.
Tri matchesFabricPredicate(const QString& version, const QString& predicate);

/// Maven range as used by Forge / NeoForge: `[a,b)`, `(a,]`, `[a]`, comma-separated union of ranges, and a bare `a` which
/// (Forge soft requirement) means "at least a". `*` and an empty range mean "any version".
Tri matchesMavenRange(const QString& version, const QString& range);

/// Plain-language rendering of a requirement for messages ("1.20 or newer", "older than 1.21"). Falls back to the raw text.
QString describePredicate(const QString& predicate, bool mavenSyntax);

}  // namespace Mods
}  // namespace Zero
