#!/usr/bin/env python3
"""Archive a Git revision or a local working-tree snapshot with submodule sources."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

from chroma_version import read_version


# Apply before reading files, even when a local data file was accidentally added
# to Git. Normal ignored/untracked files are already omitted by git ls-files.
EXCLUDED_ROOTS = {
    ".tools", ".chroma-test", "dist", "instances", "skins", "skin-extras", "skin-outfits",
    "accounts", "cache", "logs", "meta", "libraries-local", "assets", "java", "run",
    "profiles", "profile", "screenshots", "saves", "mods", "resourcepacks", "shaderpacks",
}
EXCLUDED_COMPONENTS = {".git", ".codex", ".agents", ".aws", ".ssh", "secrets", "__pycache__"}
EXCLUDED_PROFILE_FILES = {"accounts.json", "prismlauncher.cfg", "chroma-ui.cfg", "profile.json"}


def source_path(path):
    relative = PurePosixPath(path)
    if relative.is_absolute() or not relative.parts or ".." in relative.parts or "\\" in path:
        raise ValueError(f"Invalid source path: {path}")
    return relative


def excluded(path):
    parts = [part.lower() for part in source_path(path).parts]
    return (parts[0] in EXCLUDED_ROOTS or any(part in EXCLUDED_COMPONENTS for part in parts)
            or any(part == ".env" or part.startswith(".env.") for part in parts)
            or (len(parts) == 1 and parts[0] in EXCLUDED_PROFILE_FILES))


def git(repo, *args, **kwargs):
    return subprocess.run(
        ["git", "-c", f"safe.directory={repo.as_posix()}", "-C", str(repo), *args],
        check=True,
        **kwargs,
    )


def index_entries(repo):
    entries = {}
    raw = git(repo, "ls-files", "--stage", "-z", capture_output=True).stdout
    for entry in raw.split(b"\0"):
        if not entry:
            continue
        header, path = entry.split(b"\t", 1)
        mode, revision, stage = header.decode("ascii").split()
        if stage != "0":
            raise RuntimeError("Resolve merge conflicts before archiving source")
        entries[path.decode("utf-8")] = (mode, revision)
    return entries


def working_files(repo, entries, output):
    raw = git(repo, "ls-files", "--cached", "--others", "--exclude-standard", "-z", capture_output=True).stdout
    files = []
    for path in sorted(set(item.decode("utf-8") for item in raw.split(b"\0") if item)):
        if excluded(path):
            continue
        if path in {"SOURCE-REVISION.txt", "SOURCE-MANIFEST.json"}:
            raise ValueError(f"Reserved source archive metadata name: {path}")
        mode = entries.get(path, ("100644", ""))[0]
        if mode == "160000":
            continue
        source = repo / path
        resolved = source.resolve()
        if not resolved.is_relative_to(repo.resolve()):
            raise ValueError(f"Source path leaves the repository: {path}")
        if resolved == output:
            continue
        if source.is_symlink():
            files.append((path, mode))
        elif source.is_file():
            files.append((path, mode))
        elif source.exists():
            raise ValueError(f"Expected a source file: {path}")
        # A tracked file deleted in the working tree is intentionally absent.
    return files


def normalized(member, epoch):
    member.uid = member.gid = 0
    member.uname = member.gname = ""
    member.mtime = epoch
    member.pax_headers = {}
    return member


def add_bytes(destination, name, data, epoch, mode=0o644):
    member = normalized(tarfile.TarInfo(name), epoch)
    member.mode = mode
    member.size = len(data)
    destination.addfile(member, io.BytesIO(data))


def archive(repo, output, ref=None, working_tree=False, requested_version=None):
    repo = repo.resolve()
    output = output.resolve()
    if output.exists():
        raise ValueError("Output already exists; choose a new archive path")
    commit = git(repo, "rev-parse", "--verify", f"{ref or 'HEAD'}^{{commit}}", capture_output=True, text=True).stdout.strip()
    cmake = ((repo / "CMakeLists.txt").read_text(encoding="utf-8") if working_tree else
             git(repo, "show", f"{commit}:CMakeLists.txt", capture_output=True, text=True).stdout)
    version = read_version(cmake)
    if requested_version and requested_version != version:
        raise ValueError(f"Requested version differs from application version {version}")
    epoch = int(git(repo, "show", "-s", "--format=%ct", commit, capture_output=True, text=True).stdout.strip())
    prefix = f"Chroma-{version}/"
    revisions = [f"Snapshot: {'working-tree (includes local changes)' if working_tree else 'committed revision'}",
                 f"Chroma base commit: {commit}"]
    entries = index_entries(repo) if working_tree else {}
    files = working_files(repo, entries, output) if working_tree else []
    sources = [] if working_tree else [(repo, commit, prefix)]
    if working_tree:
        submodules = [(path, revision) for path, (mode, revision) in entries.items() if mode == "160000"]
    else:
        submodules = []
        for entry in git(repo, "ls-tree", "-rz", commit, capture_output=True).stdout.split(b"\0"):
            if not entry:
                continue
            header, path_bytes = entry.split(b"\t", 1)
            mode, kind, sha = header.decode("ascii").split()
            if mode == "160000":
                submodules.append((path_bytes.decode("utf-8"), sha))
    for path, revision in sorted(submodules):
        source_path(path)
        if excluded(path):
            raise ValueError(f"Submodule is in an excluded source location: {path}")
        subrepo = repo / path
        if not subrepo.resolve().is_relative_to(repo) or not (subrepo / ".git").exists():
            raise RuntimeError(f"Initialize submodule inside the repository first: {path}")
        if working_tree:
            if git(subrepo, "status", "--porcelain", "--untracked-files=normal", capture_output=True, text=True).stdout:
                raise RuntimeError(f"Submodule has local changes; commit or restore them before archiving: {path}")
            revision = git(subrepo, "rev-parse", "HEAD", capture_output=True, text=True).stdout.strip()
        git(subrepo, "cat-file", "-e", f"{revision}^{{commit}}")
        sources.append((subrepo, revision, prefix + path + "/"))
        revisions.append(f"{path}: {revision}")

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest = []
    with tempfile.TemporaryDirectory(prefix="chroma-source-", dir=output.parent) as temporary:
        staged_archive = Path(temporary) / output.name
        with staged_archive.open("wb") as raw_output, gzip.GzipFile(filename="", mode="wb", fileobj=raw_output, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as destination:
                seen = set()
                for path, mode in files:
                    source = repo / path
                    if source.is_symlink() or mode == "120000":
                        # Git on Windows may represent a symlink as a text file.
                        # Retain the link in a portable corresponding-source archive.
                        target = (os.readlink(source) if source.is_symlink() else source.read_text(encoding="utf-8")).replace("\\", "/")
                        if (not target or PurePosixPath(target).is_absolute() or "\0" in target
                                or not (source.parent / target).resolve().is_relative_to(repo)):
                            raise ValueError(f"Invalid source symlink: {path}")
                        member = normalized(tarfile.TarInfo(prefix + path), epoch)
                        member.type = tarfile.SYMTYPE
                        member.linkname = target
                        member.mode = 0o777
                        destination.addfile(member)
                        manifest.append({"path": path, "symlink": target})
                    else:
                        data = source.read_bytes()
                        add_bytes(destination, prefix + path, data, epoch, 0o755 if mode == "100755" else 0o644)
                        manifest.append({"path": path, "sha256": hashlib.sha256(data).hexdigest()})
                    seen.add(prefix + path)
                for index, (source_repo, revision, source_prefix) in enumerate(sources):
                    raw = Path(temporary) / f"{index}.tar"
                    git(source_repo, "-c", "core.autocrlf=false", "-c", "core.eol=lf", "-c", "tar.umask=0022",
                        "archive", "--format=tar", f"--prefix={source_prefix}", f"--output={raw}", revision)
                    with tarfile.open(raw, "r:") as source:
                        for member in source:
                            if member.name in seen:
                                continue
                            relative = member.name.removeprefix(prefix).rstrip("/")
                            if relative and excluded(relative):
                                continue
                            seen.add(member.name)
                            destination.addfile(normalized(member, epoch), source.extractfile(member) if member.isfile() else None)
                add_bytes(destination, prefix + "SOURCE-REVISION.txt", ("\n".join(revisions) + "\n").encode("utf-8"), epoch)
                if working_tree:
                    metadata = {"mode": "working-tree", "baseCommit": commit, "version": version, "files": manifest}
                    add_bytes(destination, prefix + "SOURCE-MANIFEST.json", (json.dumps(metadata, indent=2) + "\n").encode("utf-8"), epoch)
        staged_archive.rename(output)
    return version, commit, len(submodules)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--ref", help="Committed release revision (default: HEAD)")
    mode.add_argument("--working-tree", action="store_true", help="Include tracked local changes and nonignored new source files")
    parser.add_argument("--version", help="Must match the source version; defaults to that version")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    try:
        version, commit, submodules = archive(repo, args.output, args.ref, args.working_tree, args.version)
    except (ValueError, RuntimeError) as error:
        parser.error(str(error))
    print(f"Created {args.output.resolve()} for {version} from {'working tree based on ' if args.working_tree else ''}{commit} (including {submodules} submodule)")


if __name__ == "__main__":
    main()
