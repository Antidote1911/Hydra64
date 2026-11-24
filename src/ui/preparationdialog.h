#pragma once

#include "db/preparation.h"
#include "errorreport.h"

#include <QDialog>

#include <functional>

class QDoubleSpinBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStringListModel;
class QTextBrowser;
class QToolButton;

// Database → Statistics → Prepare against a player… (ChessBase: "Se préparer contre les Blancs /
// Noirs", "Dossier"): what a player of the database plays with one colour — tree of his lines
// with the number of games and his score, the lines where he scores worst (to aim for) and best
// (to avoid), his openings by ECO code, his losses by length and his shortest ones.
class PreparationDialog : public QDialog {
    Q_OBJECT
public:
    PreparationDialog(const hy64db::Database& db, const QString& baseName, const QString& player = {}, chess::Color side = chess::White, QWidget* parent = nullptr);
    bool showPlayer(const QString& name); // false if the player is not in the database
    void setSide(chess::Color side);
    void setOptions(int moves, int fromYear, int minGames);
    // One's own repertoire for a colour (false: none): the report then says where the player's
    // lines leave it. Read again at each report, for the colour opposite to the player's.
    using RepertoireLoader = std::function<bool(chess::Color, chess::Game&)>;
    void setRepertoireLoader(RepertoireLoader loader);
    const hy64db::PrepCoverage& coverage() const { return m_coverage; }
    // Error report: an engine looks, in his most recent losses with this colour, for the move that
    // cost him the most. `cacheFile`: where the results are kept (empty: not kept).
    void setErrorCache(const QString& cacheFile);
    void setErrorOptions(int games, double secondsPerPosition);
    bool startErrorScan(const EngineConfig& engine = {}); // the fallback engine if none is given
    bool isScanning() const { return m_scan && m_scan->isRunning(); }
    std::vector<uint32_t> recentLosses() const;           // the losses concerned, most recent first
    const std::map<uint32_t, errorreport::GameErrors>& errors() const { return m_errors; }
    bool hasRepertoire() const { return m_hasRepertoire; }
    QString html(bool links = true) const; // displayed report; without links for the exported page
    const hy64db::Preparation& preparation() const { return m_prep; }
    // Games behind link no. `link` of the displayed report (a line of the tree, a short loss…).
    std::vector<uint32_t> linkedGames(int link) const { return link >= 0 && size_t(link) < m_links.size() ? m_links[size_t(link)] : std::vector<uint32_t>{}; }
    int linkCount() const { return int(m_links.size()); }

signals:
    void gamesRequested(const std::vector<uint32_t>& ids, const QString& title);
    void positionRequested(uint32_t game, int ply); // a game to open on the position before half-move `ply`
    void scanFinished(bool completed);

private:
    void compute();
    void complete(const QString& text); // names proposed while typing
    QString lineText(const std::vector<const hy64db::PrepNode*>& path, size_t from = 0) const;

    const hy64db::Database& m_db;
    QString m_baseName;
    QLineEdit* m_name;
    QStringListModel* m_names;
    QToolButton* m_sideButton[2];
    QSpinBox* m_moves;
    QSpinBox* m_year;
    QSpinBox* m_minGames;
    QTextBrowser* m_report;
    QPushButton* m_games;
    QPushButton* m_export;
    QString m_player;
    uint32_t m_id = hy64db::NoName;
    chess::Color m_side = chess::White;
    hy64db::Preparation m_prep;
    RepertoireLoader m_loadRepertoire;
    chess::Game m_repertoire; // m_coverage points into m_prep only
    bool m_hasRepertoire = false;
    hy64db::PrepCoverage m_coverage;
    // Error report
    QSpinBox* m_errorGames;
    QDoubleSpinBox* m_errorSeconds;
    QPushButton* m_errorButton;
    QProgressBar* m_errorProgress;
    ErrorScan* m_scan = nullptr;
    QString m_errorCache;
    std::map<uint32_t, errorreport::GameErrors> m_errors;
    QString errorsHtml(bool links) const;
    mutable std::vector<std::pair<uint32_t, int>> m_positionLinks; // (game, half-move) of each "position:" link
    std::vector<uint32_t> m_usage;
    mutable std::vector<std::vector<uint32_t>> m_links; // games of each link of the report
    mutable std::vector<QString> m_linkTitles;
};
