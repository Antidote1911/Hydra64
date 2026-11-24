#pragma once

#include "annotator.h"
#include "db/database.h"

#include <QObject>
#include <QString>

#include <map>
#include <vector>

// Error report of a player (ChessBase: "Rapport d'erreur"): an engine goes through his lost games
// and picks, in each, the move that cost him the most winning chances — when in the game, and in
// what kind of position. Results are kept in a file next to the database.
namespace errorreport {

enum class Phase { Opening, Middlegame, Endgame };
enum class Standing { Better, Equal, Worse }; // the player's position before his mistake

struct Mistake {
    int ply = 0;        // half-move number of the player's move
    QString san, best;  // the move played and the engine's (notation of the sources, English letters)
    double drop = 0;    // winning chances lost (0 to 2)
    QString before, after; // evaluations from White's point of view ("+0.35", "#-3")
    Phase phase = Phase::Middlegame;
    Standing standing = Standing::Equal;
};

struct GameErrors {
    uint32_t game = 0;
    QString fingerprint; // players, date and length: a game that changed place is not mistaken for another
    QString engine;
    int movetimeMs = 0;
    int mistakes = 0, blunders = 0; // the player's moves losing 0.2 / 0.3 of winning chances or more (blunders included in mistakes)
    bool hasTurning = false;        // false: no clear mistake found (lost little by little, or on time)
    Mistake turning;                // his costliest move, the earliest on ties
};

// Opening up to move 15, endgame once the pieces left (pawns and kings aside) are worth 26 points
// or fewer for both sides together, middlegame otherwise.
Phase phaseOf(const chess::Position& before, int ply);

// `evals[k]`: position after `firstPly + k` half-moves of the main line; positions not analysed
// (invalid) are skipped.
GameErrors analyse(const chess::Game& game, const QList<annotation::PositionEval>& evals, int firstPly, chess::Color player);

QString fingerprint(const hy64db::Database& db, uint32_t id);
QString cachePath(const QString& databasePath); // "<database>.errors.json", next to it
// Results kept for this database; those whose game no longer matches are dropped.
std::map<uint32_t, GameErrors> loadCache(const QString& path, const hy64db::Database& db);
bool saveCache(const QString& path, const std::map<uint32_t, GameErrors>& results);

} // namespace errorreport

// Analyses games one after the other, in the background (engine process; the interface stays free).
class ErrorScan : public QObject {
    Q_OBJECT
public:
    explicit ErrorScan(const hy64db::Database& db, QObject* parent = nullptr);
    bool start(const EngineConfig& engine, std::vector<uint32_t> games, chess::Color player, int movetimeMs, QString* error);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void gameDone(const errorreport::GameErrors& result);
    void progress(int doneGames, int totalGames);
    void finished(bool completed);

private:
    void nextGame();
    void gameFinished(bool completed);

    const hy64db::Database& m_db;
    Annotator* m_annotator;
    EngineConfig m_engine;
    std::vector<uint32_t> m_games;
    size_t m_at = 0;
    chess::Color m_player = chess::White;
    int m_movetime = 200;
    bool m_running = false;
    bool m_decided = false; // the game in hand is lost beyond doubt: its analysis was cut short
    chess::Game m_game;
    int m_firstPly = 0;
};
