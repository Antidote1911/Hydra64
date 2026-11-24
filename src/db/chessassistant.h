#pragma once

// Reading of Chess Assistant / ChessOK Aquarium game databases ("Chess Data Pro format 1.3":
// .cdp, .d00, .a01/.d01, .a02/.d02, .dxx, .emb). Read-only, for conversion.
// Format described in docs/chess-assistant-re.md (validated against an Aquarium PGN export).

#include "convert.h"
#include "mappedfile.h"

#include <string>
#include <vector>

namespace hy64db {

class ChessAssistantSource : public GameSource {
public:
    // File of a Chess Assistant database (extension .cdp, .d00, .d02, .emb…)?
    static bool isDatabaseFile(const std::string& path);
    // Path without extension of the database `path` belongs to.
    static std::string basePath(const std::string& path);

    bool open(const std::string& path, std::string* error);
    uint64_t count() const override { return m_games.size(); } // without the deleted games
    bool read(uint64_t index, chess::Game& game, std::string* error) const override;
    uint64_t total() const { return m_total; }                 // with the deleted games

private:
    std::string name(int dictionary, uint32_t id, std::string* title = nullptr) const;

    MappedFile m_headers, m_moves, m_moveIndex;
    uint64_t m_total = 0;
    std::vector<uint32_t> m_games; // numbers of the non-deleted games
    // Dictionaries of the .emb (players, tournaments, sites, sources): texts and offsets
    struct Dictionary {
        std::vector<uint8_t> text;
        std::vector<uint32_t> offsets;
    };
    Dictionary m_dict[4];
};

} // namespace hy64db
