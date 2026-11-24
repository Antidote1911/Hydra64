#pragma once

#include "db/database.h"

#include <QDialog>

class QTableWidget;

// Crosstable of a tournament of the database (like "Crosstable" of SCID vs PC): standings,
// results between players, points, performance. Double-click: the games concerned.
class EventTableDialog : public QDialog {
    Q_OBJECT
public:
    EventTableDialog(const hy64db::Database& db, uint32_t event, QWidget* parent = nullptr);
    const hy64db::EventTable& table() const { return m_table; }
    QTableWidget* view() const { return m_view; }

signals:
    void gamesRequested(const std::vector<uint32_t>& ids, const QString& title);

private:
    const hy64db::Database& m_db;
    hy64db::EventTable m_table;
    QString m_event;
    QTableWidget* m_view;
};
