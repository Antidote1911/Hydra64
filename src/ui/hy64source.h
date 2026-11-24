#pragma once

#include "db/database.h"
#include "indexedsource.h"

#include <vector>

// Hydra64 database (.hy64i/.hy64n/.hy64g, positions .hy64p) in the Databases page: editable, fast,
// position search and tree.
class Hy64Source : public IndexedGameSource {
public:
    static bool isDatabasePath(const QString& path); // .hy64i (or another file of the database)
    static bool create(const QString& path, QString* error); // empty database

    bool open(const QString& path, QString* error);
    hy64db::Database& database() { return m_db; }
    const hy64db::Database& database() const { return m_db; }
    void reload(); // after an operation on the files (compaction, position index)

    QString formatName() const override { return QStringLiteral("Hydra64"); }
    bool isReadOnly() const override { return false; }
    void sync() { m_db.sync(); } // after a series of additions (copy between databases)
    int total() const override { return m_total; }
    int deletedCount() const override { return m_deleted; }
    int count() const override { return int(m_view.size()); }
    bool isFiltered() const override { return m_filtered; }
    bool header(int row, Header& out) const override;
    bool game(int row, chess::Game& out, QString* error) const override;
    QString gameText(int row) const override;

    std::unique_ptr<View> search(const QString& text, const Cancel* cancel, QString* error) const override;
    std::unique_ptr<View> viewIds(bool all) const override;
    bool sort(View& view, int column, bool ascending, const Cancel* cancel, QString* error) const override;
    void setView(std::unique_ptr<View> view, bool filtered) override;

    std::unique_ptr<View> searchHeaders(const hy64db::HeaderQuery& q, Combine combine, const View* current, const Cancel* cancel,
                                        QString* error) const override;
    std::unique_ptr<View> negate(const View& current) const override;
    int rowOfNumber(int number) const override;
    bool hasPositions() const override { return m_db.positions() != nullptr; }
    std::unique_ptr<View> searchPosition(const chess::Position& pos, const Cancel* cancel, QString* error) const override;
    bool summary(int row, Summary& out) const override;
    std::vector<chess::treebook::BookMove> tree(const chess::Position& pos) const override;

    int append(const chess::Game& game, QString* error) override;
    // Direct copy of game `row` of another Hydra64 database (without going through PGN); row added or -1.
    int appendFrom(const Hy64Source& from, int row, QString* error);
    bool replace(int row, const chess::Game& game, QString* error) override;
    bool setDeleted(QList<int> rows, bool deleted, QString* error) override;
    bool isDeleted(int row) const override;
    bool supportsUserFlags() const override { return true; }
    bool setUserFlag(const QList<int>& rows, int flag, bool on, QString* error) override;

    uint32_t idOf(int row) const { return row >= 0 && row < int(m_view.size()) ? m_view[size_t(row)] : 0; }
    // View made of these games (in this order), and games marked deleted by number.
    static std::unique_ptr<View> viewOf(std::vector<uint32_t> ids);
    bool setDeletedIds(const std::vector<uint32_t>& ids, bool deleted, QString* error);

private:
    struct Ids : View {
        std::vector<uint32_t> ids;
        int count() const override { return int(ids.size()); }
    };
    hy64db::Database m_db;
    std::vector<uint32_t> m_view; // numbers of the displayed games, in order
    int m_total = 0, m_deleted = 0;
    bool m_filtered = false;
};
