#!/usr/bin/env python3
"""Package Linux sources without the simulator, generated UI, Git or build outputs."""
import argparse
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    "framework", "apps", "services", "product", "config", "cmake", "tests",
    "custom/assets", "platform/linux", "platform/preview", "platform/lvgl", "docs", "tools",
    "books-demo", "licenses", "LICENSE", "README.md", "ARCHITECTURE.md", ".github", ".gitignore", ".gitattributes",
)


def package(output):
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w:gz") as archive:
        for source in SOURCES:
            path = ROOT / source
            files = [path] if path.is_file() else sorted(path.rglob("*"))
            for file in files:
                relative = file.relative_to(ROOT)
                if not file.is_file() or file.is_symlink():
                    continue
                if any(part.startswith("build") or part in (".git", "__pycache__")
                       for part in relative.parts[:-1]):
                    continue
                if file.resolve() == output:
                    continue
                archive.add(file, arcname=relative.as_posix(), recursive=False)
    with tarfile.open(output) as archive:
        names = set(archive.getnames())
        assert "platform/linux/build-armhf.sh" in names
        assert "config/lv_conf_common.h" in names
        assert not any(name.startswith("platform/simulator/") for name in names)
    print(f"{output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="Output .tar.gz file")
    package(parser.parse_args().output)
