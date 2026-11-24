#include "mappedfile.h"

#include "fileoffset.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define HY64DB_MMAP 1
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define HY64DB_WINMAP 1
#endif

namespace hy64db {

bool MappedFile::open(const std::string& path, std::string* error) { return openRange(path, 0, UINT64_MAX, error); }

bool MappedFile::openRange(const std::string& path, uint64_t offset, uint64_t length, std::string* error)
{
    close();
    auto fail = [&](const std::string& why) {
        if (error) *error = path + " : " + why;
        close();
        return false;
    };
#ifdef HY64DB_MMAP
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return fail(std::strerror(errno));
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const std::string why = std::strerror(errno);
        ::close(fd);
        return fail(why);
    }
    const uint64_t fileSize = uint64_t(st.st_size);
    if (offset > fileSize || (length != UINT64_MAX && length > fileSize - offset)) {
        ::close(fd);
        return fail("section hors du fichier");
    }
    m_size = size_t(length == UINT64_MAX ? fileSize - offset : length);
    m_open = true;
    if (m_size > 0) {
        // The mapping starts on a page boundary; the data is a little further on.
        const uint64_t page = uint64_t(::sysconf(_SC_PAGESIZE));
        const uint64_t aligned = offset - offset % page;
        m_mapLen = size_t(offset - aligned) + m_size;
        void* p = ::mmap(nullptr, m_mapLen, PROT_READ, MAP_SHARED, fd, off_t(aligned));
        if (p == MAP_FAILED) {
            const std::string why = std::strerror(errno);
            ::close(fd);
            m_mapLen = 0;
            return fail(why);
        }
        m_mapBase = p;
        m_data = static_cast<const uint8_t*>(p) + (offset - aligned);
        m_mapped = true;
    }
    ::close(fd);
    return true;
#elif defined(HY64DB_WINMAP)
    // Windows: mapping through CreateFileMapping. The path is UTF-8 (the program's code page,
    // set by its manifest); converted here so as not to depend on that setting.
    std::wstring wide(path.size() + 1, L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), int(wide.size()));
    if (n <= 0) return fail("chemin illisible");
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return fail("ouverture impossible (erreur Windows " + std::to_string(GetLastError()) + ")");
    LARGE_INTEGER li;
    if (!GetFileSizeEx(file, &li)) {
        CloseHandle(file);
        return fail("taille illisible");
    }
    const uint64_t fileSize = uint64_t(li.QuadPart);
    if (offset > fileSize || (length != UINT64_MAX && length > fileSize - offset)) {
        CloseHandle(file);
        return fail("section hors du fichier");
    }
    m_size = size_t(length == UINT64_MAX ? fileSize - offset : length);
    m_open = true;
    if (m_size > 0) {
        // The mapping starts on a granularity boundary (64 KB); the data is a little further on.
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const uint64_t grain = info.dwAllocationGranularity;
        const uint64_t aligned = offset - offset % grain;
        m_mapLen = size_t(offset - aligned) + m_size;
        HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        void* p = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, DWORD(aligned >> 32), DWORD(aligned & 0xffffffffu), m_mapLen) : nullptr;
        const DWORD why = GetLastError();
        if (mapping) CloseHandle(mapping); // the view keeps the mapping alive
        if (!p) {
            CloseHandle(file);
            m_mapLen = 0;
            return fail("projection impossible (erreur Windows " + std::to_string(why) + ")");
        }
        m_mapBase = p;
        m_data = static_cast<const uint8_t*>(p) + (offset - aligned);
        m_mapped = true;
    }
    CloseHandle(file);
    return true;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return fail(std::strerror(errno));
    seek64(f, 0, SEEK_END);
    const uint64_t fileSize = uint64_t(tell64(f));
    if (offset > fileSize || (length != UINT64_MAX && length > fileSize - offset)) {
        std::fclose(f);
        return fail("section hors du fichier");
    }
    m_size = size_t(length == UINT64_MAX ? fileSize - offset : length);
    seek64(f, int64_t(offset), SEEK_SET);
    m_owned = new uint8_t[m_size ? m_size : 1];
    const bool ok = std::fread(m_owned, 1, m_size, f) == m_size;
    std::fclose(f);
    if (!ok) return fail("lecture incomplète");
    m_data = m_owned;
    m_open = true;
    return true;
#endif
}

void MappedFile::close()
{
#ifdef HY64DB_MMAP
    if (m_mapped && m_mapBase) ::munmap(m_mapBase, m_mapLen);
#elif defined(HY64DB_WINMAP)
    if (m_mapped && m_mapBase) UnmapViewOfFile(m_mapBase);
#endif
    delete[] m_owned;
    m_owned = nullptr;
    m_mapBase = nullptr;
    m_mapLen = 0;
    m_data = nullptr;
    m_size = 0;
    m_open = false;
    m_mapped = false;
}



} // namespace hy64db
