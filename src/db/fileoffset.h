#pragma once

// 64-bit file offsets, everywhere.
//
// On Windows, "long" is 32 bits even in 64-bit builds: std::fseek / std::ftell stop at 2 GB, and
// so does off_t without _FILE_OFFSET_BITS=64. Databases commonly exceed that size (the Mega is
// several gigabytes): every positioned access to a database file goes through these two functions.

#include <cstdint>
#include <cstdio>

namespace hy64db {

inline int seek64(std::FILE* f, int64_t offset, int whence)
{
#if defined(_WIN32)
    return ::_fseeki64(f, offset, whence);
#else
    return ::fseeko(f, off_t(offset), whence);
#endif
}

inline int64_t tell64(std::FILE* f)
{
#if defined(_WIN32)
    return ::_ftelli64(f);
#else
    return int64_t(::ftello(f));
#endif
}

} // namespace hy64db
