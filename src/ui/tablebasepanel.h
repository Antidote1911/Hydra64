#pragma once

#include "chess/tablebase.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QTableWidget;
class QToolButton;

// "Tablebases" panel: exact result of the position and of each move according to the Syzygy
// tables (local folder, Fathom) or, optionally, the Lichess online service
// (tablebase.lichess.ovh: only the position, as FEN, is sent).
class TablebasePanel : public QWidget {
    Q_OBJECT
public:
    explicit TablebasePanel(QWidget* parent = nullptr);

    void setPosition(const chess::Position& pos);
    // Displayed result (for the tests): source "local" / "en ligne", otherwise empty
    struct Shown {
        QString source;
        int wdl = 0, dtz = 0;
        QList<chess::tb::MoveResult> moves;
        QList<int> dtm; // online: distance to mate of each move (0: unknown)
    };
    const Shown& shown() const { return m_shown; }
    static QString defaultPath();
    bool setTablesPath(const QString& path); // a single source, saved; false if no table
    void reload() { refresh(); }             // after a change in the Preferences

signals:
    void movePicked(chess::Move m);
    void settingsRequested(); // pencil button: open the Preferences on the Tablebases page

private:
    void refresh();
    void showLocal(const chess::tb::Probe& p);
    void showOnline(const QByteArray& json);
    void fill();
    void editSettings();

    chess::Position m_pos;
    QLabel* m_header;
    QTableWidget* m_table;
    QToolButton* m_settings;
    Shown m_shown;
    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QHash<QString, QByteArray> m_cache; // online answers per position
};
