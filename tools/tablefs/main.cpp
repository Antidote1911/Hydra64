// hydra64-tablefs: presents the Syzygy endgame tablebases contained in disc images
// (.iso, UDF) as an ordinary folder, read-only, copying nothing and with no administrator
// rights (FUSE). Hydra64 starts it itself; the engines receive this folder.
//
//   hydra64-tablefs <mount folder> <image.iso> [<image.iso> …]
//
// All the tables of all the images appear flat in the folder. A table present
// in two images is only taken once. The program stays in the foreground; it unmounts the
// folder when it receives SIGTERM or SIGINT.
#define FUSE_USE_VERSION 31

#include "tablefs/udf.h"

#include <fuse3/fuse.h>

#include <sys/prctl.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct Table {
    int fd = -1;
    tablefs::ImageFile file;
};
std::map<std::string, Table> tables; // name → file of an image

const Table* find(const char* path)
{
    if (path[0] != '/') return nullptr;
    const auto it = tables.find(path + 1);
    return it == tables.end() ? nullptr : &it->second;
}

int fsGetattr(const char* path, struct stat* st, fuse_file_info*)
{
    std::memset(st, 0, sizeof *st);
    st->st_uid = getuid();
    st->st_gid = getgid();
    if (std::strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        return 0;
    }
    const Table* t = find(path);
    if (!t) return -ENOENT;
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = off_t(t->file.size);
    st->st_blocks = blkcnt_t((t->file.size + 511) / 512);
    return 0;
}

int fsReaddir(const char* path, void* buffer, fuse_fill_dir_t fill, off_t, fuse_file_info*, fuse_readdir_flags)
{
    if (std::strcmp(path, "/") != 0) return -ENOENT;
    fill(buffer, ".", nullptr, 0, fuse_fill_dir_flags(0));
    fill(buffer, "..", nullptr, 0, fuse_fill_dir_flags(0));
    for (const auto& [name, table] : tables) fill(buffer, name.c_str(), nullptr, 0, fuse_fill_dir_flags(0));
    return 0;
}

int fsOpen(const char* path, fuse_file_info* info)
{
    if (!find(path)) return -ENOENT;
    if ((info->flags & O_ACCMODE) != O_RDONLY) return -EROFS;
    info->keep_cache = 1; // the content never changes: the kernel may keep its pages
    return 0;
}

int fsRead(const char* path, char* buffer, size_t size, off_t offset, fuse_file_info*)
{
    const Table* t = find(path);
    if (!t) return -ENOENT;
    const long got = tablefs::readFile(t->fd, t->file, buffer, size, uint64_t(offset));
    return got < 0 ? -EIO : int(got);
}

} // namespace

int main(int argc, char** argv)
{
    prctl(PR_SET_PDEATHSIG, SIGTERM); // if Hydra64 stops abruptly, the folder is unmounted
    if (argc < 3) {
        std::fprintf(stderr, "Usage : %s <dossier de montage> <image.iso> [<image.iso> …]\n", argv[0]);
        return 2;
    }
    for (int i = 2; i < argc; ++i) {
        std::vector<tablefs::ImageFile> files;
        std::string error;
        if (!tablefs::readImage(argv[i], files, &error)) {
            std::fprintf(stderr, "%s : %s\n", argv[i], error.c_str());
            continue;
        }
        const int fd = open(argv[i], O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        for (tablefs::ImageFile& f : tablefs::syzygyTables(files)) {
            const std::string name = f.name();
            if (!tables.count(name)) tables[name] = {fd, std::move(f)};
        }
    }
    if (tables.empty()) {
        std::fprintf(stderr, "Aucune table Syzygy dans ces images.\n");
        return 1;
    }
    static fuse_operations operations{};
    operations.getattr = fsGetattr;
    operations.readdir = fsReaddir;
    operations.open = fsOpen;
    operations.read = fsRead;
    // -f: in the foreground; read-only; the SIGTERM / SIGINT signals unmount (fuse_main)
    std::vector<std::string> args{argv[0], "-f", "-o", "ro,fsname=hydra64-tablefs,subtype=tablefs", argv[1]};
    std::vector<char*> raw;
    for (std::string& a : args) raw.push_back(a.data());
    return fuse_main(int(raw.size()), raw.data(), &operations, nullptr);
}
