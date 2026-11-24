#pragma once

#include "format.h"

#include <array>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hy64db {

// Text folded for comparing and searching: lower case, Latin accents removed ("Ünzicker" → "unzicker").
std::string fold(std::string_view utf8);
// A word of `foldedName` (or the whole name) starts with `foldedPrefix` (texts already folded).
bool wordPrefix(std::string_view foldedName, std::string_view foldedPrefix);

// Names of a database (.hy64n): players, tournaments, sites, rounds, annotators, sources.
// Each type has its numbers 0, 1, 2… in order of addition. Thread-safe: concurrent
// reads, exclusive additions.
class Names {
public:
    // Reads the journal (after the 64-byte header). False if the file is damaged.
    bool load(const uint8_t* data, size_t size, std::string* error);
    // Continuation of the names journal (single-file database: names added after the original section).
    bool loadMore(const uint8_t* data, size_t size, std::string* error = nullptr);

    size_t count(NameType t) const;
    std::string name(NameType t, uint32_t id) const; // empty for NoName or an unknown number

    // Number of the name (added if new; its bytes are then appended to `appended`,
    // to be written at the end of the .hy64n).
    uint32_t intern(NameType t, std::string_view name, std::vector<uint8_t>* appended);
    // Number of an existing name, NoName otherwise.
    uint32_t find(NameType t, std::string_view name) const;

    // Alphabetical rank (folded text) of each number: sorting by name = sorting these integers.
    std::shared_ptr<const std::vector<uint32_t>> ranks(NameType t) const;
    // Numbers whose name, or one of its words, starts with `prefix` (folded).
    std::vector<uint32_t> findPrefix(NameType t, std::string_view prefix) const;

    // Bytes of a name in the journal (to write a complete .hy64n).
    static void encode(std::vector<uint8_t>& out, NameType t, std::string_view name);

private:
    struct Table {
        std::vector<std::string> names;
        mutable std::unordered_map<std::string, uint32_t> lookup; // built on the first addition or search
        mutable std::shared_ptr<const std::vector<std::string>> folded;
        mutable std::shared_ptr<const std::vector<uint32_t>> ranks;
    };
    void buildLookup(const Table& table) const;
    std::shared_ptr<const std::vector<std::string>> foldedOf(NameType t) const;

    std::array<Table, NameTypeCount> m_tables;
    mutable std::shared_mutex m_mutex;
};

} // namespace hy64db
