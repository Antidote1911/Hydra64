#include "udf.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0 // only Windows distinguishes "text" files
#endif
#if defined(_WIN32)
#include <io.h>
#include <mutex>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
namespace {
// Windows has no pread: seek then read, under a lock (one reader at a time).
// Tablebase ISO images are not mounted on Windows anyway (no FUSE).
ssize_t pread(int fd, void* out, size_t count, long long offset)
{
    static std::mutex lock;
    const std::lock_guard<std::mutex> guard(lock);
    if (_lseeki64(fd, offset, SEEK_SET) < 0) return -1;
    // _read takes an "unsigned int": in slices of one gigabyte at most
    size_t done = 0;
    while (done < count) {
        const int got = _read(fd, static_cast<char*>(out) + done, unsigned(std::min<size_t>(count - done, 1u << 30)));
        if (got < 0) return -1;
        if (got == 0) break;
        done += size_t(got);
    }
    return ssize_t(done);
}
} // namespace
#endif

namespace tablefs {

namespace {

constexpr uint64_t Sector = 2048;

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

struct Reader {
    int fd = -1;
    uint64_t imageSize = 0;
    uint64_t partition = 0; // first sector of the partition
    int budget = 200000;    // safeguard against a damaged image (entries read)

    bool sector(uint64_t n, uint8_t* out) const { return n * Sector + Sector <= imageSize && pread(fd, out, Sector, off_t(n * Sector)) == ssize_t(Sector); }

    // File entry (tag 261, or 266 for the extended form): type, size, extents.
    bool fileEntry(uint32_t block, int& type, uint64_t& size, std::vector<Extent>& extents, std::vector<uint8_t>& embedded) const
    {
        uint8_t d[Sector];
        if (!sector(partition + block, d)) return false;
        const uint16_t tag = le16(d);
        if (tag != 261 && tag != 266) return false;
        type = d[16 + 11];
        const int allocation = le16(d + 16 + 18) & 7;
        size = le64(d + 56);
        const size_t base = tag == 261 ? 176 : 216;
        const uint32_t eaLength = le32(d + base - 8), adLength = le32(d + base - 4);
        if (base + uint64_t(eaLength) + adLength > Sector) return false;
        const uint8_t* ad = d + base + eaLength;
        extents.clear();
        embedded.clear();
        if (allocation == 3) { // data in the entry itself
            embedded.assign(ad, ad + std::min<uint64_t>(size, adLength));
            return true;
        }
        if (allocation != 0 && allocation != 1) return false;
        const size_t step = allocation == 0 ? 8 : 16;
        for (size_t p = 0; p + step <= adLength; p += step) {
            const uint32_t raw = le32(ad + p), position = le32(ad + p + 4);
            const uint32_t length = raw & 0x3FFFFFFF;
            if (raw >> 30 == 3) return false; // continuation of the extents in another block: not handled
            if (!length) break;
            if (raw >> 30 == 0) extents.push_back({(partition + position) * Sector, length});
        }
        // the file size bounds the last extent
        uint64_t left = size;
        for (Extent& e : extents) {
            e.length = std::min(e.length, left);
            left -= e.length;
        }
        return left == 0;
    }

    bool contents(uint32_t block, std::vector<uint8_t>& out) const
    {
        int type = 0;
        uint64_t size = 0;
        std::vector<Extent> extents;
        if (!fileEntry(block, type, size, extents, out)) return false;
        if (!out.empty() || size == 0) return true;
        if (size > (64u << 20)) return false; // a directory of more than 64 MB: damaged image
        out.resize(size);
        uint64_t at = 0;
        for (const Extent& e : extents) {
            if (e.offset + e.length > imageSize || pread(fd, out.data() + at, e.length, off_t(e.offset)) != ssize_t(e.length)) return false;
            at += e.length;
        }
        return true;
    }

    bool walk(uint32_t block, const std::string& prefix, std::vector<ImageFile>& files, int depth)
    {
        if (depth > 16) return false;
        std::vector<uint8_t> d;
        if (!contents(block, d)) return false;
        for (size_t p = 0; p + 38 <= d.size();) {
            if (le16(d.data() + p) != 257 || --budget < 0) break;
            const uint8_t characteristics = d[p + 18], nameLength = d[p + 19];
            const uint32_t icb = le32(d.data() + p + 24);
            const uint16_t useLength = le16(d.data() + p + 36);
            const size_t nameAt = p + 38 + useLength;
            if (nameAt + nameLength > d.size()) break;
            std::string name;
            if (nameLength > 1) {
                const uint8_t* raw = d.data() + nameAt;
                if (raw[0] == 8) {
                    for (int i = 1; i < nameLength; ++i) { // Latin-1 → UTF-8
                        const uint8_t c = raw[i];
                        if (c < 0x80) name += char(c);
                        else name += char(0xC0 | c >> 6), name += char(0x80 | (c & 0x3F));
                    }
                } else if (raw[0] == 16) {
                    for (int i = 1; i + 1 < nameLength; i += 2) { // big-endian UCS-2 → UTF-8
                        const unsigned c = unsigned(raw[i]) << 8 | raw[i + 1];
                        if (c < 0x80) name += char(c);
                        else if (c < 0x800) name += char(0xC0 | c >> 6), name += char(0x80 | (c & 0x3F));
                        else name += char(0xE0 | c >> 12), name += char(0x80 | ((c >> 6) & 0x3F)), name += char(0x80 | (c & 0x3F));
                    }
                }
            }
            p += (38 + size_t(useLength) + nameLength + 3) & ~size_t(3);
            if ((characteristics & 0x0C) || name.empty() || name.find('/') != std::string::npos) continue; // parent, deleted
            if (characteristics & 2) {
                walk(icb, prefix + name + "/", files, depth + 1);
                continue;
            }
            ImageFile f;
            int type = 0;
            std::vector<uint8_t> embedded;
            if (!fileEntry(icb, type, f.size, f.extents, embedded) || !embedded.empty()) continue; // (a file stored in its entry is tiny: not a table)
            f.path = prefix + name;
            files.push_back(std::move(f));
        }
        return true;
    }
};

bool fail(std::string* error, const char* message)
{
    if (error) *error = message;
    return false;
}

} // namespace

bool readImage(const std::string& imagePath, std::vector<ImageFile>& files, std::string* error)
{
    files.clear();
    Reader r;
    r.fd = open(imagePath.c_str(), O_RDONLY | O_CLOEXEC | O_BINARY);
    if (r.fd < 0) return fail(error, "image illisible");
    struct Closer {
        int fd;
        ~Closer() { close(fd); }
    } closer{r.fd};
#if defined(_WIN32)
    const long long end = _lseeki64(r.fd, 0, SEEK_END); // lseek stops at 2 GB on Windows
#else
    const long long end = lseek(r.fd, 0, SEEK_END);
#endif
    if (end < (long long)(257 * Sector)) return fail(error, "fichier trop petit pour une image de disque");
    r.imageSize = uint64_t(end);
    uint8_t d[Sector];
    // Anchor point (tag 2) at sector 256: it gives the volume descriptor sequence
    if (!r.sector(256, d) || le16(d) != 2) return fail(error, "pas une image UDF (point d'ancrage absent)");
    const uint32_t length = le32(d + 16), location = le32(d + 20);
    bool hasPartition = false, hasVolume = false;
    uint32_t fileSet = 0;
    for (uint32_t i = 0; i < std::min<uint32_t>(length / Sector, 64); ++i) {
        if (!r.sector(location + i, d)) break;
        const uint16_t tag = le16(d);
        if (tag == 5 && !hasPartition) r.partition = le32(d + 188), hasPartition = true;
        else if (tag == 6 && !hasVolume) {
            if (le32(d + 212) != Sector) return fail(error, "taille de bloc UDF inattendue");
            if (le32(d + 268) > 1) return fail(error, "image UDF à plusieurs partitions (disque réinscriptible ?) : non gérée");
            fileSet = le32(d + 252), hasVolume = true;
        } else if (tag == 8) break;
    }
    if (!hasPartition || !hasVolume) return fail(error, "image UDF incomplète (partition ou volume absent)");
    if (!r.sector(r.partition + fileSet, d) || le16(d) != 256) return fail(error, "image UDF : jeu de fichiers introuvable");
    const uint32_t root = le32(d + 404);
    if (!r.walk(root, "", files, 0)) return fail(error, "image UDF : répertoire illisible");
    return true;
}

std::vector<ImageFile> syzygyTables(const std::vector<ImageFile>& files)
{
    std::vector<ImageFile> out;
    for (const ImageFile& f : files) {
        const std::string name = f.name();
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos || f.size < 64) continue;
        const std::string ext = name.substr(dot);
        if (ext == ".rtbw" || ext == ".rtbz") out.push_back(f);
    }
    return out;
}

long readFile(int imageFd, const ImageFile& file, char* buffer, size_t size, uint64_t offset)
{
    if (offset >= file.size) return 0;
    size = size_t(std::min<uint64_t>(size, file.size - offset));
    size_t done = 0;
    uint64_t start = 0; // offset of the current extent in the file
    for (const Extent& e : file.extents) {
        if (done == size) break;
        const uint64_t at = offset + done;
        if (at < start + e.length) {
            const size_t chunk = size_t(std::min<uint64_t>(size - done, start + e.length - at));
            const ssize_t got = pread(imageFd, buffer + done, chunk, off_t(e.offset + (at - start)));
            if (got < 0) return -1;
            done += size_t(got);
            if (size_t(got) < chunk) break;
        }
        start += e.length;
    }
    return long(done);
}

} // namespace tablefs
