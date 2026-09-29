"""Read the shared release version from the application's CMake configuration."""

import re


def read_version(cmake):
    parts = []
    for part in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"^set\(Launcher_VERSION_{part}\s+(\d+)\)\s*$", cmake, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing Launcher_VERSION_{part} in CMakeLists.txt")
        parts.append(match[1])
    return ".".join(parts)
