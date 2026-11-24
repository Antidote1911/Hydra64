#pragma once

#include "enginegame.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

// Opponent's strength on a single stepped slider: three "beginner" levels below the engine's
// lowest Elo, Elo in steps of 50 (1320, 1350… 3150), then "maximum strength" without and with
// the endgame tablebases. The text centred under the slider says what the level is worth.
class StrengthSlider : public QWidget {
    Q_OBJECT
public:
    explicit StrengthSlider(QWidget* parent = nullptr);
    void setStrength(bool fullStrength, int elo, int beginnerLevel, bool tablebases); // Elo brought to the nearest level
    int step() const; // current level (0: beginner 1)
    bool fullStrength() const;
    bool tablebases() const;   // last level: maximum strength with the endgame tablebases
    int beginnerLevel() const; // 0: none, 1 (the weakest) to 3
    int elo() const;           // Elo of the level; the last one chosen when the slider is on a beginner or maximum level
    QString text() const;      // "Elo 1500", "Force maximale", "Débutant 3 — …"
    static constexpr int MinElo = 1320, MaxElo = 3190;

signals:
    void changed();

private:
    static constexpr int EloStep = 50, EloSteps = 38; // 1320, then 1350 to 3150
    static int eloAt(int index) { return index == 0 ? MinElo : 1300 + index * EloStep; }
    void refresh();
    QSlider* m_slider;
    QLabel* m_label;
    int m_elo = 1800;
};

// Settings of a game against the engine: side, strength, thinking time.
class PlayDialog : public QDialog {
    Q_OBJECT
public:
    // `preferredId`: preselected engine (otherwise the last used, otherwise the built-in Stockfish).
    PlayDialog(const QString& preferredId, QWidget* parent = nullptr);
    PlaySettings settings() const;
    // Last settings used (or default values).
    static PlaySettings savedSettings();

    // Time controls offered: code "seconds+increment" ("0+0" = no clock, "custom") and label.
    static QList<QPair<QString, QString>> timeControls();
    static QString savedTimeControl();               // stored code
    static void saveTimeControl(const QString& code);
    static QString shortLabel(const PlaySettings& p); // "5+3", "10+5", "Sans pendule"

    void accept() override;
    // Imposed opening: ECO code ("C41": its reference line) or SAN moves; returns the
    // SAN moves, empty if the text is not a valid line from the initial position.
    static QString openingLine(const QString& text);

private:
    void updateTimeControl();
    void updateStrengthAvailability();

    QButtonGroup* m_side;
    StrengthSlider* m_strength;
    QLabel* m_noStrength;
    QComboBox* m_time;
    QComboBox* m_engineBox;
    QComboBox* m_tc;
    class QWidget* m_customRow;
    class QSpinBox* m_customMin;
    class QSpinBox* m_customInc;
    QCheckBox* m_engineSeparate;
    class QWidget* m_engineRow;
    class QSpinBox* m_engineMin;
    class QSpinBox* m_engineInc;
    class QWidget* m_moveTimeBox;
    class QLineEdit* m_name;
    QCheckBox* m_fromCurrent;
    QCheckBox* m_opening;
    class QLineEdit* m_openingText;
    class QPushButton* m_openingPick;
    QLabel* m_openingName;
    QCheckBox* m_coach;
    QComboBox* m_coachLevel;
    QCheckBox* m_ponder;
};
