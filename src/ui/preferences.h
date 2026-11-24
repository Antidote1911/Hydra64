#pragma once

#include <QColor>
#include <QDialog>
#include <QObject>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;

// General preferences, saved in QSettings ("prefs" group).
namespace prefs {

enum class Notation { French, English, Figurine };

// Values read are kept in memory; to be called if QSettings is changed otherwise
// (reset, tests).
void invalidateCache();

QString language();             // "fr" ou "en"
void setLanguage(const QString& lang);
QString systemLanguage();        // default language at first launch

Notation notation();
void setNotation(Notation n);

bool wheelNavigation();
void setWheelNavigation(bool on);
bool wheelInverted();
void setWheelInverted(bool on);

bool restoreSession(); // restore each page's game at startup
void setRestoreSession(bool on);

bool showEvalBar(); // evaluation bar next to the board
void setShowEvalBar(bool on);
bool showEvalBarOnBases(); // the same on the Databases page (browsing board): hidden by default
void setShowEvalBarOnBases(bool on);
// Assisted analysis (Databases and Analysis pages), off by default: target squares coloured by the
// quality of the move; the opponent's threat as an arrow.
bool assistedAnalysis();
void setAssistedAnalysis(bool on);
bool threatArrow();
void setThreatArrow(bool on);
// Piece set (see PieceRenderer::availableSets) and board theme (boardtheme::available).
QString pieceSet();
void setPieceSet(const QString& id);
bool pieceShadow(); // light drop shadow under the pieces
void setPieceShadow(bool on);
QString boardTheme();
void setBoardTheme(const QString& id);
// Colours of the squares of the "plain colours" board (boardtheme::ColorTheme).
QColor boardLightColor();
QColor boardDarkColor();
void setBoardColors(const QColor& light, const QColor& dark);

bool hideMenuBar();
void setHideMenuBar(bool on);

// Automatic evaluation (embedded Stockfish, in the background)
int evalThreads();   // processors (default 2)
int evalHashMb();    // hash table in MB (default 64)
int evalDepth();     // target depth per position (default 16)
int evalTimeMs();    // time cap per position (default 300 ms)
void setEvaluation(int threads, int hashMb, int depth, int timeMs);

// Font size of the notation (pixels), set by the panel's A− / A+ buttons.
int notationFontSize();
// Notation: display the engine notes of tournament games ("+0.23/31 2.6s", "livre").
// Hidden, they stay in the game and in the PGN.
bool showEngineComments();
void setShowEngineComments(bool on);
void setNotationFontSize(int px);
constexpr int MinNotationFontSize = 9;
constexpr int MaxNotationFontSize = 28;

// Emits changed() when a display preference changes (notation…).
class Notifier : public QObject {
    Q_OBJECT
public:
    static Notifier& instance();
signals:
    void changed();
};

} // namespace prefs

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);
    void accept() override;
    void reject() override;

    bool languageChanged() const { return m_languageChanged; }
    static void openOnTablebases(); // the next opening shows the Tablebases page
    class TablebasePage* tablebasePage() const { return m_tablebasePage; }

private:
    // Settings editable through the dialog (except the language, which needs a restart).
    struct Settings {
        bool wheel, wheelInverted, restoreSession, evalBar, hideMenu, pieceShadow;
        QString pieceSet, boardTheme;
        QColor boardLight, boardDark;
        prefs::Notation notation;
        int evalThreads, evalHashMb, evalDepth, evalTimeMs;
    };
    Settings current() const;
    static void apply(const Settings& s);
    Settings m_initial;

    void updatePreview();
    void updatePiecePreview();

    class TablebasePage* m_tablebasePage = nullptr;
    class QLineEdit* m_encyclopediaDir; // folder of the players encyclopedia (applied on validation)
    QLabel* m_encyclopediaStatus;
    void checkEncyclopedia();           // says whether the chosen folder is readable
    QComboBox* m_language;
    QComboBox* m_notation;
    QLabel* m_preview;
    QCheckBox* m_wheel;
    QCheckBox* m_wheelInvert;
    QCheckBox* m_restoreSession;
    QCheckBox* m_evalBar;
    QCheckBox* m_hideMenu;
    QComboBox* m_pieceSet;
    QCheckBox* m_pieceShadow;
    QComboBox* m_boardTheme;
    // Colours of the "plain colours" board: a row shown only when that board is chosen
    QColor m_boardLight, m_boardDark;
    QLabel* m_boardColorsLabel;
    QWidget* m_boardColors;
    class QPushButton* m_boardLightButton;
    class QPushButton* m_boardDarkButton;
    void pickBoardColor(bool light);
    void updateBoardColors();
    QLabel* m_piecePreview;
    QLabel* m_pieceCredit;
    QLabel* m_userSetsHint;
    class QPushButton* m_restoreSets;
    void fillSetLists(const QString& pieceSet, const QString& boardTheme);
    class QSpinBox* m_evalThreads;
    class QComboBox* m_evalHash;
    class QSpinBox* m_evalDepth;
    class QSpinBox* m_evalTime;
    QLabel* m_restartNote;
    bool m_languageChanged = false;
    class QStackedWidget* m_pages;
    static int s_lastPage; // last category opened, during the session
};
