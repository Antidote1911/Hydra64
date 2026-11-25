#!/usr/bin/env python3
"""Gathers the Windows version of Hydra64 into an archive to unzip.

    python3 tools/windows/package.py build-win build-win/sysroot/ucrt64 build-win/Hydra64-windows.zip

Second argument: the MSYS2 "ucrt64" folder Qt was taken from — the downloaded one when building
from Linux (./build.sh --win), the installed one when building on Windows (GitHub workflow).

Content: the built programs, the libraries (DLLs) they depend on — found by reading their import
tables, step by step —, Qt's plugins and translations, the bundled databases (databases folder).
Everything fits in a "Hydra64" folder; nothing has to be installed.
"""
import os
import shutil
import subprocess
import sys
import zipfile

PROGRAMS = ["hydra64.exe", "hydra64-convert.exe", "stockfish.exe", "shuffle.exe"]
PLUGINS = {
    "platforms": ["qwindows.dll"],
    "styles": ["qmodernwindowsstyle.dll"],
    "imageformats": ["qsvg.dll", "qjpeg.dll", "qgif.dll", "qico.dll"],
    "iconengines": ["qsvgicon.dll"],
    "tls": ["qschannelbackend.dll", "qcertonlybackend.dll"],
}
TRANSLATIONS = ["qtbase_fr.qm", "qtbase_en.qm"]
# From Linux, the system MinGW's objdump; on Windows (MSYS2), plain "objdump".
OBJDUMP = shutil.which("x86_64-w64-mingw32-objdump") or shutil.which("objdump")
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def imports(path):
    out = subprocess.run([OBJDUMP, "-p", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout
    return [line.split("DLL Name:")[1].strip() for line in out.splitlines() if "DLL Name:" in line]


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    if not OBJDUMP:
        sys.exit("objdump introuvable (MinGW)")
    build, sysroot, archive = (os.path.abspath(a) for a in sys.argv[1:])
    stage = os.path.join(build, "Hydra64")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)

    todo, missing = [], []
    for name in PROGRAMS:
        src = os.path.join(build, name)
        if not os.path.exists(src):
            missing.append(name)
            continue
        shutil.copy2(src, stage)
        todo.append(os.path.join(stage, name))
    for folder, names in PLUGINS.items():
        os.makedirs(os.path.join(stage, "plugins", folder))
        for name in names:
            dst = os.path.join(stage, "plugins", folder, name)
            shutil.copy2(os.path.join(sysroot, "share", "qt6", "plugins", folder, name), dst)
            todo.append(dst)
    os.makedirs(os.path.join(stage, "translations"))
    for name in TRANSLATIONS:
        shutil.copy2(os.path.join(sysroot, "share", "qt6", "translations", name), os.path.join(stage, "translations"))
    # Qt looks for its plugins and translations according to this file, next to the program.
    open(os.path.join(stage, "qt.conf"), "w").write("[Paths]\nPrefix = .\nPlugins = plugins\nTranslations = translations\n")
    # The licence, the notice of the third-party components and the texts of their licences
    shutil.copy2(os.path.join(ROOT, "LICENSE"), stage)
    shutil.copy2(os.path.join(ROOT, "THIRD-PARTY.md"), stage)
    shutil.copytree(os.path.join(ROOT, "licenses"), os.path.join(stage, "licenses"))
    bases = os.path.join(build, "databases")  # bundled databases and their notices, copied by the build
    if os.path.isdir(bases):
        shutil.copytree(bases, os.path.join(stage, "databases"))

    # Libraries: those of MSYS2 first (Qt and its dependencies), then those of the system MinGW.
    places = [os.path.join(sysroot, "bin"), "/usr/x86_64-w64-mingw32/bin"]
    copied, system = set(), set()
    while todo:
        for dll in imports(todo.pop()):
            key = dll.lower()
            if key in copied or key in system:
                continue
            for place in places:
                found = next((f for f in os.listdir(place) if f.lower() == key), None) if os.path.isdir(place) else None
                if found:
                    shutil.copy2(os.path.join(place, found), stage)
                    copied.add(key)
                    todo.append(os.path.join(stage, found))
                    break
            else:
                system.add(key)  # provided by Windows (kernel32, user32, api-ms-win-crt…)

    if os.path.exists(archive):
        os.remove(archive)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for root, _, files in os.walk(stage):
            for f in sorted(files):
                full = os.path.join(root, f)
                z.write(full, os.path.join("Hydra64", os.path.relpath(full, stage)))
    size = os.path.getsize(archive) / 1e6
    print(f"{len(copied)} libraries, archive of {size:.0f} MB: {archive}")
    odd = sorted(d for d in system if not (d.startswith("api-ms-") or d.startswith("ext-ms-")))
    print("fournies par Windows : " + ", ".join(odd))
    if missing:
        print("ABSENTS de la compilation : " + ", ".join(missing))


if __name__ == "__main__":
    main()
