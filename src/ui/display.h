#pragma once

#include "preferences.h"

#include <QString>

#include <string>

// Presentation in French. Storage (PGN) stays in the standard format:
// pieces in English (KQRBN) and dates YYYY.MM.DD.
namespace display {

// SAN move (stored in English) displayed in the chosen notation:
// French "Cf6", English "Nf6" or figurines "♘f6".
QString san(const std::string& san);
QString san(const std::string& san, prefs::Notation notation);
// Comment whose moves quoted in English notation ("Qh5", "Nbd7", "exd8=Q") are
// rewritten in the chosen notation. Only words with the exact form of a move are touched.
QString comment(const QString& text);
QString comment(const QString& text, prefs::Notation notation);

// End-of-game reason, translated.
QString endReason(int gameEndKind);

// PGN date → display. French: "21/06/1851", "11/2026", "1858";
// English: "1851.06.21", "2026.11", "1858"; unknown: "?".
QString date(const std::string& pgnDate);
// PGN date → editable text (DD/MM/YYYY in French, YYYY.MM.DD in English).
QString dateForEditing(const std::string& pgnDate);
QString datePlaceholder();
// Typed text → PGN date, empty if unknown. French: "21/06/1851", "06/1851", "1851";
// English: "1851.06.21", "1851.06", "1851".
std::string pgnDate(const QString& text);

} // namespace display
