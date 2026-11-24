#pragma once

#include "db/nameindex.h"

#include <QAbstractTableModel>
#include <QDialog>

class QLineEdit;
class QTabWidget;
class QTableView;
class QLabel;

// Table of an index: entries filtered by a text, sorted by a column.
class IndexModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Games, Years, BestElo, Score, ColumnCount };
    IndexModel(hy64db::NameType type, QObject* parent = nullptr);
    void setEntries(std::vector<hy64db::IndexEntry> entries);
    void setFilter(const QString& text);
    const hy64db::IndexEntry& entry(int row) const { return m_entries[m_rows[size_t(row)]]; }
    int total() const { return int(m_entries.size()); }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex& = {}) const override { return m_type == hy64db::Player ? ColumnCount : Years + 1; }
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    void rebuild();
    hy64db::NameType m_type;
    std::vector<hy64db::IndexEntry> m_entries; // alphabetical order
    std::vector<uint32_t> m_rows;
    QString m_filter;
    int m_sortColumn = Name;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

// Browsable index of a Hydra64 database: players, tournaments, sites, annotators, sources.
// Double-click: the games of the entry in the list.
class IndexDialog : public QDialog {
    Q_OBJECT
public:
    IndexDialog(const hy64db::Database& db, const QString& baseName, QWidget* parent = nullptr);
    IndexModel* model(hy64db::NameType type); // computed on first request
    void showType(hy64db::NameType type);

signals:
    void gamesRequested(int nameType, quint32 id, const QString& name);
    void playerCardRequested(const QString& name);

private:
    void tabChanged();
    void updateCount();
    const hy64db::Database& m_db;
    QTabWidget* m_tabs;
    QLineEdit* m_filter;
    QLabel* m_count;
    std::vector<hy64db::NameType> m_types;
    std::vector<IndexModel*> m_models;
    std::vector<QTableView*> m_views;
    std::vector<bool> m_loaded;
};
