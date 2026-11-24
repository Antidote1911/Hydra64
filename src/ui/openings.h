#pragma once

#include "chess/eco.h"

#include <QString>

// ECO codes and opening names (resources/eco-opening, embedded in the application), loaded on first use.
const chess::eco::Book& openingsBook();
// "C41 Défense Philidor" (the code alone if unknown), in the interface language
QString openingLabel(const QString& eco);
// Name of an opening (English in the data) in the interface language: translated into
// French if the interface is in French. Display only: PGN headers stay in English.
QString openingName(const std::string& english);
// "Sicilian Defense: Najdorf Variation, English Attack" → "Défense sicilienne : variante
// Najdorf, attaque anglaise" (established families, then rules: main word first, gender).
QString frenchOpeningName(const QString& english);
