#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reading of the directory of a disc image in UDF format (ChessBase's "Endgame Turbo"
// DVDs, for instance): name, size and location of each file in the image. The
// files of a DVD are not compressed: a file is a sequence of byte extents of the
// image, which can be read without extracting anything.
// Limits: UDF 1.02 to 2.01 on a simple partition (no rewritable disc with a virtual
// allocation table and no metadata partition); ISO 9660 images without UDF are not
// read (their short names do not tell ".rtbw" from ".rtbz").
namespace tablefs {

struct Extent {
    uint64_t offset = 0; // in the image
    uint64_t length = 0;
};

struct ImageFile {
    std::string path; // "Syzygy345/KBBBvK.rtbw"
    uint64_t size = 0;
    std::vector<Extent> extents; // empty: zero-size file
    std::string name() const
    {
        const size_t slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
};

// Files of the image. False (with a message) if it is unreadable or is not UDF.
bool readImage(const std::string& imagePath, std::vector<ImageFile>& files, std::string* error = nullptr);
// Among `files`, the Syzygy tables (.rtbw, .rtbz).
std::vector<ImageFile> syzygyTables(const std::vector<ImageFile>& files);
// Reads `size` bytes of a file of the image from `offset`; number of bytes read, -1 on error.
long readFile(int imageFd, const ImageFile& file, char* buffer, size_t size, uint64_t offset);

} // namespace tablefs
