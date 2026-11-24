#pragma once

#include "chess/game.h"

#include <QObject>
#include <QString>

#include <array>
#include <functional>

// Where the displayed game comes from (to save it in the right place).
struct GameOrigin {
    QString path;           // PGN file
    int row = -1;           // index of the game in that file
    bool savedHere = false; // saved by us: it is updated without asking
    bool isValid() const { return row >= 0 && !path.isEmpty(); }
};

// Game being edited, shared by all the views.
class GameSession : public QObject {
    Q_OBJECT
public:
    explicit GameSession(QObject* parent = nullptr);

    // One independent game per page (same numbering as MainWindow::Page).
    enum Slot { DatabaseSlot = 0, AnalysisSlot = 1, PlaySlot = 2, TournamentSlot = 3, SlotCount = 4 };
    Slot slot() const { return m_slot; }
    void switchSlot(Slot slot); // changes the displayed game; none is lost
    bool slotHasMoves(Slot s) const { return gameIn(s).root()->mainChild() != nullptr; }
    const chess::Game& gameIn(Slot s) const { return s == m_slot ? m_game : m_stash[s]; }
    const GameOrigin& originIn(Slot s) const { return s == m_slot ? m_origin : m_stashOrigin[s]; }
    // Game of a slot modified without displaying it (tournament playing in the background).
    // If the slot is displayed, the views are notified as for a move played.
    void replaceGameIn(Slot s, chess::Game&& game);  // shows the end of the game
    void appendMoveIn(Slot s, chess::Move m, int clockMs = -1); // move appended to the end of the main line
    void setHeaderIn(Slot s, const std::string& key, const std::string& value);
    // Restoration at startup: content of a slot, displayed move included (path
    // of child indices from the root).
    void restoreSlot(Slot s, chess::Game&& game, const GameOrigin& origin, const std::vector<int>& path);
    static std::vector<int> pathTo(const chess::Node* node);

    chess::Game& game() { return m_game; }
    const chess::Game& game() const { return m_game; }
    chess::Node* current() const { return m_game.current(); }

    void newGame(const chess::Position& start = chess::Position::startPos());
    void loadGame(chess::Game&& game);

    void playMove(chess::Move m);
    void goTo(chess::Node* node);
    void forward();
    void back();
    void toStart();
    void toEnd();

    void deleteNode(chess::Node* node);
    void promoteVariation(chess::Node* node);
    void demoteVariation(chess::Node* node);
    // Passes the turn at the displayed move (null move, "--"); false when the side to move is in check.
    bool playNullMove();
    // Replacing a move: the board goes back to the position before `node`, and the next move played
    // there takes its place — what followed is dropped, or kept as long as it is still legal
    // (`keepFollowing`). Going elsewhere, or any other change to the game, gives it up.
    void armReplace(chess::Node* node, bool keepFollowing);
    void cancelReplace();
    bool replaceArmed() const { return m_replaceId >= 0; }
    bool replaceKeepsFollowing() const { return m_replaceKeep; }
    void setComment(chess::Node* node, const QString& text);
    void setPreComment(chess::Node* node, const QString& text); // pre-move comment
    void setGraphics(chess::Node* node, const std::vector<chess::Graphic>& graphics); // arrows and marked squares
    void setEvaluation(chess::Node* node, const chess::Evaluation& eval);
    // Grouped modification of the displayed game (annotation…): the views are notified afterwards.
    void editGame(const std::function<void(chess::Game&)>& edit);
    void setHeader(const std::string& key, const std::string& value);
    void setHeaders(const chess::Game::Headers& headers);
    // Undo / redo the changes to the displayed game (moves added, variations
    // deleted or promoted, comments, symbols, arrows, information). One history per
    // page, for the Databases and Analysis pages; it starts from scratch when another game is loaded.
    // checkpoint(): to be called just before a change made directly in the tree.
    void checkpoint();
    bool canUndo() const { return !m_undo[m_slot].empty(); }
    bool canRedo() const { return !m_redo[m_slot].empty(); }
    bool undo();
    bool redo();
    const GameOrigin& origin() const { return m_origin; }
    void setOrigin(const GameOrigin& o) { m_origin = o; }

signals:
    // `animate` is true when advancing by a single move (piece animation).
    void currentChanged(bool animate);
    void treeChanged();
    void graphicsChanged(); // arrows or marked squares of the displayed move changed (the game has changed)
    void evaluationsChanged();
    void headersChanged();
    // Emitted when the whole game is replaced (new game, loading).
    void gameReplaced();
    void slotSwitched();
    void undoStateChanged();
    void replaceModeChanged(bool armed);
    void replaceRefused(); // the move played is already another line of that position

private:
    void moveTo(chess::Node* node);
    chess::Game m_game;
    GameOrigin m_origin;
    Slot m_slot = AnalysisSlot;
    std::array<chess::Game, SlotCount> m_stash; // games of the slots not displayed
    std::array<GameOrigin, SlotCount> m_stashOrigin;
    struct Snapshot {
        std::string pgn;       // the whole game
        std::vector<int> path; // the displayed move
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& s);
    void forgetHistory(Slot s);
    std::array<std::vector<Snapshot>, SlotCount> m_undo, m_redo;
    int m_replaceId = -1; // move waiting to be replaced (node id), -1: none
    bool m_replaceKeep = false;
};
