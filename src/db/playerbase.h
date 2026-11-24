#pragma once

#include "mappedfile.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace hy64db {

// ChessBase players encyclopedia ("Playbase": folder containing Playbase.player,
// PlayBase.rating, PlayBase.media and the photo folders PlayBase.MMA … MMZ), read-
// only. Format described in docs/playerbase-re.md. All returned strings are UTF-8.
class PlayerBase {
public:
    struct Date {
        int year = 0, month = 0, day = 0; // 0: unknown
        bool known() const { return year != 0; }
    };
    struct Player {
        std::string last, first;
        uint32_t id = 0;   // identifier of the Elo ratings
        int homonym = 0;   // rank among players of the same name (0: the only one, or the first)
        Date birth, death;
        std::string title;      // "GM", "IM", "FM" or empty
        std::string womenTitle; // "WGM", "WIM", "WFM" or empty
        bool worldChampion = false, womenWorldChampion = false, reigningChampion = false;
        std::string nation, formerNation; // federation code ("NOR"), empty if unknown
        bool male = true;
        int32_t media = -1; // first photo record, -1: none
        std::string name() const { return first.empty() ? last : last + ", " + first; }
    };
    struct Rating {
        Date date;      // date of the list
        int elo = 0;
        int rank = -1;  // world rank in the list, -1: unranked
    };
    struct Photo {
        std::string path; // .jpg file, empty if it is missing from disk
        std::string photographer, place;
        int year = 0;
    };

    // `dir`: the "Playbase" folder. False (and `error`) if the players file is missing; the
    // ratings and photos are optional.
    bool open(const std::string& dir, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_players.isOpen(); }
    size_t count() const { return m_index.size(); }

    // Players called `name`, ignoring case and accents. Accepted forms:
    // "Last, First", "Last" alone, "Last First", "First Last", with or without a federation in
    // parentheses at the end ("Kasparov Garry (RUS)"); a first name reduced to initials accepts
    // every first name starting that way. Most likely first: exact first name, stated
    // federation, best Elo, then the best documented.
    std::vector<Player> find(const std::string& name) const;
    std::vector<Rating> ratings(uint32_t id) const; // by increasing date
    int peakElo(uint32_t id) const;
    std::vector<Photo> photos(const Player& player) const; // by increasing date

    static std::string nationCode(int code); // ChessBase code → federation code
    static std::string foldName(const std::string& utf8); // lower case without accents, for comparing

private:
    Player read(size_t offset) const;
    // Players with family name `last` (folded) whose first name fits; `nation` breaks ties.
    std::vector<Player> lookup(const std::string& last, const std::string& first, const std::string& nation, bool* exactFirst) const;
    void buildRatingIndex() const;

    std::string m_dir;
    MappedFile m_players, m_ratings, m_media;
    struct Entry {
        std::string key; // folded family name
        uint32_t offset; // start of the record (after its length)
    };
    std::vector<Entry> m_index; // sorted by key
    struct Group {
        uint32_t id, first, count; // a player's ratings: `count` records starting at `first`
    };
    mutable std::vector<Group> m_groups; // sorted by identifier
    mutable bool m_groupsBuilt = false;
    mutable std::mutex m_mutex;
    mutable std::map<std::string, std::map<std::string, std::string>> m_folders; // photo folder → (lower-case name → real name)
};

} // namespace hy64db
