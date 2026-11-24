#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QTextBrowser>

#include <functional>

class GameSession;
namespace chess { struct Node; }

// Notation of the game: main line in bold, variations indented,
// comments and annotations. Click = go to the move, right-click = editing menu.
class NotationView : public QTextBrowser {
    Q_OBJECT
public:
    explicit NotationView(GameSession* session, QWidget* parent = nullptr);

    // Open Hydra64 databases the game can be added to: (name, path), the active database first.
    using DatabaseList = std::function<QList<QPair<QString, QString>>()>;
    void setDatabaseList(DatabaseList list) { m_databases = std::move(list); }

    void setSendToAnalysisVisible(bool visible) { m_sendToAnalysis = visible; } // right-click, outside the Analysis page

signals:
    void sendToAnalysisRequested();                    // "Send to analysis"
    void infoRequested();
    void exportHtmlRequested();                        // "Export as HTML page…"
    void exportPgnRequested();                         // "Export as PGN…"
    void addToDatabaseRequested(const QString& path); // "Add to database …"

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void rebuild();
    void highlightCurrent(); // highlights the current move without rebuilding the text
    QHash<int, QPair<int, int>> m_spans; // node → (position, length) in the document
    chess::Node* nodeAt(const QPoint& pos) const;
    QString moveHtml(const chess::Node* n, bool forceNumber, bool mainline) const;
    void appendInline(QString& html, const chess::Node* first) const;

    GameSession* m_session;
    DatabaseList m_databases;
    bool m_sendToAnalysis = false;
};

// Notation panel: the notation and a small bar (font size A− / A+).
class NotationPanel : public QWidget {
    Q_OBJECT
public:
    explicit NotationPanel(GameSession* session, QWidget* parent = nullptr);
    NotationView* view() const { return m_view; }

private:
    void refresh();
    NotationView* m_view;
    class QToolButton* m_smaller;
    class QToolButton* m_larger;
    class QToolButton* m_engineNotes; // engine notes (tournament games) shown or hidden
    class QLabel* m_size;
};
