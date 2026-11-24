#pragma once

#include "db/database.h"

#include <QDialog>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QTableWidget;

// Database → Upkeep → Improve the player names… (ChessBase: "Améliorer les noms des joueurs"):
// the spelling of the players encyclopedia proposed for the names of the database it recognises
// without doubt; the user ticks what is applied.
namespace namefix {

struct Proposal {
    uint32_t id = 0;       // number of the name in the database
    std::string from, to;
    uint32_t games = 0;
    bool sure = false;     // same words, only their order, case, accents or punctuation differ
};

// `canonical(name)`: the reference spelling, empty when the name is unknown or could be several
// players. Names used by no game, and those already written that way, are left out. Most used first.
std::vector<Proposal> proposals(const hy64db::Database& db, const std::function<std::string(const std::string&)>& canonical,
                                const std::function<bool(uint64_t, uint64_t)>& progress = {});
// The same words, whatever their order, case, accents and punctuation ("Kasparov Garry (RUS)" and
// "Kasparov, Garry": the federation in parentheses does not count).
bool sameWords(const std::string& a, const std::string& b);
// Reference spelling of the open players encyclopedia: the only player it knows under that name,
// provided the name carries at least the start of a first name.
std::string encyclopediaName(const std::string& name);

} // namespace namefix

class NameFixDialog : public QDialog {
    Q_OBJECT
public:
    NameFixDialog(std::vector<namefix::Proposal> proposals, QWidget* parent = nullptr);
    std::vector<std::pair<uint32_t, std::string>> chosen() const; // ticked rows
    int rowCount() const;
    void setAllChecked(bool on);

private:
    void updateCount();
    std::vector<namefix::Proposal> m_proposals;
    QTableWidget* m_table;
    QLabel* m_count;
};
