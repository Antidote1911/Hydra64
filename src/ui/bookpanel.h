#pragma once

#include <functional>
#include <vector>

#include "chess/aquarium.h"
#include "chess/ctg.h"
#include "chess/polyglot.h"
#include "chess/treebook.h"

#include <QList>
#include <QStringList>
#include <QWidget>

class GameSession;
class QLabel;
class QToolButton;

// List of the book's moves for the displayed position; double-click: play the move.
class BookMovesView : public QWidget {
    Q_OBJECT
public:
    struct Row {
        chess::Move move;
        QString san;
        double share = 0; // share of the move in the position (weight or games), from 0 to 1
        int weight = 0;   // Polyglot

        // ChessBase (CTG): statistics from the point of view of the side playing the move
        bool hasStats = false;
        bool whiteMoves = true;
        QString annotation; // « ! », « ?! »…
        int recommendation = 0; // chess::ctg::Recommendation
        int games = 0, wins = 0, draws = 0, losses = 0;
        int averageElo = 0, performance = 0;
        int maxElo = 0, year = 0; // Aquarium tree: highest Elo, last year played
        // Tree of a deep analysis: evaluation of the move (side playing it) and depth
        bool hasEval = false;
        int eval = 0, evalDepth = 0;
        double score() const; // in %, -1 without result
    };
    static constexpr int RowHeight = 26;
    static QString evalText(const Row& row); // "+0.35", "#3" (White's point of view)

    explicit BookMovesView(QWidget* parent = nullptr);
    // `withStats`: ChessBase rows (games, results); otherwise Polyglot (weight).
    void setRows(const QList<Row>& rows, bool withStats = false);
    void setPlaceholder(const QString& text);
    int rowCount() const { return int(m_rows.size()); }

signals:
    void movePicked(chess::Move m);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void paintWeightRow(QPainter& p, const Row& row, const QRectF& r, bool first);
    void paintStatsRow(QPainter& p, const Row& row, const QRectF& r, bool first);
    QString toolTipFor(const Row& row) const;

    QList<Row> m_rows;
    bool m_withStats = false;
    QString m_placeholder;
    int m_hover = -1;
};

// "Book" panel: Polyglot (.bin) or ChessBase (.ctg) opening book.
class BookPanel : public QWidget {
    Q_OBJECT
public:
    enum Format { NoBook, Polyglot, Tree, Database };
    using TreeProvider = std::function<std::vector<chess::treebook::BookMove>(const chess::Position&)>;

    explicit BookPanel(GameSession* session, QWidget* parent = nullptr);

    // .hy64book: Hydra64 tree; .bin: Polyglot. Proprietary formats (ChessBase .ctg,
    // ChessOK / Aquarium .hsh) are refused: they go through File → Convert….
    bool openBook(const QString& path, QString* error = nullptr);
    void closeBook();
    QString bookPath() const { return m_path; }
    Format format() const { return m_format; }
    QList<BookMovesView::Row> rows() const { return m_rows; } // moves of the displayed position
    void refresh();
    // Move drawn at random from the book for `pos` (in proportion to the games or the weight;
    // without the moves to avoid or annotated "?" / "??"); null move outside the book.
    chess::Move pickMove(const chess::Position& pos);
    // All the moves the open book knows for `pos` (empty outside the book).
    std::vector<chess::Move> movesAt(const chess::Position& pos);
    QString bookName() const; // displayed name of the open book (empty: none)
    static QStringList recentBooks();

    // Tree of the database open in the Databases page (Hydra64 database with positions): offered in
    // the panel's menu; empty `provider` = no database allows it.
    void setDatabaseTree(const QString& name, TreeProvider provider);
    void showDatabaseTree(); // shows the database tree, if there is one

signals:
    void movePicked(chess::Move m);
    void convertRequested(const QString& path); // proprietary file chosen: to be converted
    void statusMessage(const QString& msg);

private:
    void chooseBook();
    void updateHeader();
    void fillMenu();
    void remember(const QString& path);

    GameSession* m_session;
    Format m_format = NoBook;
    chess::polyglot::Book m_polyglot;
    chess::treebook::Book m_tree;
    QString m_path;
    QString m_dbName;
    TreeProvider m_dbTree;
    QList<BookMovesView::Row> m_rows;
    QToolButton* m_name;
    QToolButton* m_open;
    QLabel* m_info;
    BookMovesView* m_view;
};
