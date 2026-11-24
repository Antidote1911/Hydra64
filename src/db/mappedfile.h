#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hy64db {

// File read through memory mapping (read on demand by the system, no copy).
// Without mmap available, the file is read into memory.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile() { close(); }
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open(const std::string& path, std::string* error = nullptr);
    // Only part of the file: `length` bytes from `offset` (section of a single-file database).
    bool openRange(const std::string& path, uint64_t offset, uint64_t length, std::string* error = nullptr);
    void close();
    bool isOpen() const { return m_open; }
    const uint8_t* data() const { return m_data; }
    size_t size() const { return m_size; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    bool m_open = false;
    bool m_mapped = false;
    uint8_t* m_owned = nullptr; // fallback without mmap
    void* m_mapBase = nullptr;  // real start of the mapping (page-aligned)
    size_t m_mapLen = 0;
};

} // namespace hy64db
