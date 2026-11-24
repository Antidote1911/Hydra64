#!/usr/bin/env python3
"""Downloads Qt 6 and its dependencies for Windows (MSYS2 "ucrt64" packages) into a folder.

    python3 tools/windows/fetch_sysroot.py build-win/sysroot [Qt version, e.g. 6.12.0]

Used for cross-compilation from Linux (see "./build.sh --win"): the compiler is the system MinGW
(UCRT, POSIX threads, like MSYS2 ucrt64), Qt and zlib come from these prebuilt packages. The Qt
version must be that of the machine's Qt (moc, rcc, lrelease are its own). The packages are stored
as they are under <folder>/ucrt64/.
"""
import io
import os
import subprocess
import sys
import tarfile
import urllib.request

REPO = "https://repo.msys2.org/mingw/ucrt64/"
PREFIX = "mingw-w64-ucrt-x86_64-"
WANTED = ["qt6-base", "qt6-svg", "qt6-translations", "zlib"]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "curl"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return r.read()


def database():
    raw = fetch(REPO + "ucrt64.db")
    try:
        tar = tarfile.open(fileobj=io.BytesIO(raw))
    except tarfile.ReadError:  # database compressed with zstd
        raw = subprocess.run(["zstd", "-dc"], input=raw, stdout=subprocess.PIPE, check=True).stdout
        tar = tarfile.open(fileobj=io.BytesIO(raw))
    packages, provides = {}, {}
    for member in tar:
        if not member.name.endswith("/desc"):
            continue
        fields, key = {}, None
        for line in tar.extractfile(member).read().decode().splitlines():
            if line.startswith("%") and line.endswith("%"):
                key = line.strip("%")
                fields[key] = []
            elif line and key:
                fields[key].append(line)
        name = fields["NAME"][0]
        packages[name] = fields
        for p in fields.get("PROVIDES", []):
            provides.setdefault(p.split("=")[0], name)
    return packages, provides


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    dest = os.path.abspath(sys.argv[1])
    qt_version = sys.argv[2] if len(sys.argv) > 2 else None
    packages, provides = database()
    todo, done = [PREFIX + w for w in WANTED], {}
    while todo:
        name = todo.pop()
        name = name.split(">")[0].split("=")[0].split("<")[0]
        if name not in packages:
            name = provides.get(name, name)
        if name in done:
            continue
        if name not in packages:
            sys.exit(f"paquet introuvable : {name}")
        done[name] = packages[name]
        todo += packages[name].get("DEPENDS", [])
    if qt_version:
        found = done[PREFIX + "qt6-base"]["VERSION"][0].split("-")[0]
        if found != qt_version:
            sys.exit(f"MSYS2 offers Qt {found}, this machine has Qt {qt_version}: the versions must match")
    cache = os.path.join(dest, "paquets")
    os.makedirs(cache, exist_ok=True)
    for name in sorted(done):
        filename = done[name]["FILENAME"][0]
        path = os.path.join(cache, filename)
        if not os.path.exists(path):
            print(f"  {filename}", flush=True)
            data = fetch(REPO + filename)
            open(path + ".part", "wb").write(data)
            os.rename(path + ".part", path)
        subprocess.run(["tar", "--zstd", "-xf", path, "-C", dest, "--exclude=.*"], check=True)
    print(f"{len(done)} packages in {dest}/ucrt64")


if __name__ == "__main__":
    main()
