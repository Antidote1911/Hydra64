#pragma once

#include "chess/game.h"

#include <QDialog>
#include <QMap>

class QComboBox;
class QLineEdit;
class QSpinBox;

// Information of a game (PGN headers): players, Elo, result, date, tournament…
class GameInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit GameInfoDialog(const chess::Game::Headers& headers, QWidget* parent = nullptr);
    // Edited headers; empty fields are removed, the other headers kept.
    chess::Game::Headers headers() const;

    static bool isUnknown(const std::string& v); // "", "?", "????.??.??"

private:
    chess::Game::Headers m_original;
    QMap<QString, QLineEdit*> m_fields;
    QComboBox* m_result;
};
