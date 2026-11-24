#pragma once

#include "db/database.h"
#include "db/playerbase.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;
class QPushButton;

// Database → Player profile… (like "Player information" / "Player report" of SCID vs PC):
// score with each colour, Elo per year (chart), favourite openings, opponents.
class PlayerReportDialog : public QDialog {
    Q_OBJECT
public:
    PlayerReportDialog(const hy64db::Database& db, const QString& baseName, const QString& player = {}, QWidget* parent = nullptr);
    bool showPlayer(const QString& name); // false if the player is not in the database
    QString html() const;                 // displayed profile (HTML)
    const hy64db::PlayerStats& stats() const { return m_stats; }

signals:
    void gamesRequested(const std::vector<uint32_t>& ids, const QString& player);
    void preparationRequested(const QString& player); // "Prepare against him…"

private:
    void search();
    QImage eloChart() const;
    QImage fideChart() const;      // Elo from the FIDE lists (players encyclopedia)
    void loadEncyclopedia();       // record, ratings and photos of the displayed player

    const hy64db::Database& m_db;
    QString m_baseName;
    QLineEdit* m_name;
    QListWidget* m_matches;
    QTextBrowser* m_report;
    QPushButton* m_games;
    QPushButton* m_export;
    QPushButton* m_prepare;
    QLabel* m_encyclopedia;          // bottom left: players encyclopedia loaded or not
    void updateEncyclopediaStatus();
    QString welcomeHtml() const;     // text displayed as long as no player is chosen
    hy64db::PlayerStats m_stats;
    QString m_player;
    std::vector<uint32_t> m_usage;
    // Players encyclopedia (optional)
    bool m_known = false;
    hy64db::PlayerBase::Player m_info;
    std::vector<hy64db::PlayerBase::Rating> m_ratings;
    std::vector<hy64db::PlayerBase::Photo> m_photos;

public:
    bool encyclopediaEntryFound() const { return m_known; }
    size_t fideRatingCount() const { return m_ratings.size(); }
    size_t photoCount() const { return m_photos.size(); }
};
