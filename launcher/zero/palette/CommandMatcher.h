// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - ranking logic of the command palette. Pure logic (no widgets): fuzzy subsequence matching with
// bonuses and penalties, keyword matches, instance recency and a deterministic ordering. See docs/zero/SHELL.md.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Zero {
namespace Palette {

/// What a command is. The enum order is the tie-break order (and the default section order).
enum class Kind { Instance, Navigation, Action, Setting, Appearance, Server };

/// Result of matching a query against one string.
struct TextMatch {
    bool matched = false;
    double score = 0.0;
    QList<int> positions;  ///< indices (UTF-16 units) of the matched characters in the text, ascending
};

/// Case-insensitive fuzzy match: every character of `query` (whitespace ignored) must occur in `text` in order. The
/// best alignment wins: +16 per character, +24 at a word start (+8 more at index 0), +18 for a character that directly
/// follows the previous match, -(4 + gap) for a gap between matches (gap capped at 10), -0.5 per skipped leading
/// character (capped at 15) and -0.2 per unmatched character of the text (capped at 10); an exact match adds 300 and a prefix match adds 150. An empty query matches with score 0.
TextMatch matchText(const QString& query, const QString& text);

/// One thing the palette can run, reduced to what ranking needs.
struct Candidate {
    QString title;
    QStringList keywords;  ///< aliases ("ram", "memory" for the Java settings); scored slightly below title matches
    Kind kind = Kind::Action;
    qint64 lastUsedMs = 0;  ///< instances: last launch (ms since epoch), 0 = never
    bool favorite = false;  ///< servers: a starred server ranks ahead of comparable matches (kFavoriteBoost)
};

struct Ranked {
    int index = 0;  ///< position in the candidate list that was ranked
    double score = 0.0;
    QList<int> positions;  ///< matched characters of the title (empty when only a keyword matched or the query is empty)
    bool viaKeyword = false;
};

/// Factor applied to a keyword match's score (keywords rank slightly below an equal title match).
constexpr double kKeywordFactor = 0.8;
/// Extra score for a favorite server: enough to lead comparable matches, not enough to beat a clearly better one.
constexpr double kFavoriteBoost = 40.0;

/// Extra score for an instance launched recently: 20 at "now", halved after 3 days, 0 for never launched.
double recencyBoost(qint64 lastUsedMs, qint64 nowMs);
/// Small constant per kind (instances 4 ... settings and appearance 1).
double kindWeight(Kind kind);

/// Candidates matching `query`, best first. Score = best of title match and keyword match * kKeywordFactor, plus the
/// recency boost and the kind weight. Ties: kind order, then title (case-insensitive), then original index, so the
/// result is deterministic. An empty query matches everything (score = boosts only), ordered the same way.
QList<Ranked> rank(const QString& query, const QList<Candidate>& candidates, qint64 nowMs);

}  // namespace Palette
}  // namespace Zero
