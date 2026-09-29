#!/usr/bin/env python3
"""Archive a Git revision and its submodule sources without local profile data."""

import argparse
import io
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

from chroma_version import read_version


def git(repo, *args, **kwargs):
    return subprocess.run(
        ["git", "-c", f"safe.directory={repo.as_posix()}", "-C", str(repo), *args],
        check=True,
        **kwargs,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", default="HEAD", help="Committed release revision")
    parser.add_argument("--version", help="Must match the version at --ref; defaults to that version")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists():
        parser.error("Output already exists; choose a new archive path")

    repo = Path(__file__).resolve().parent.parent
    commit = git(repo, "rev-parse", "--verify", f"{args.ref}^{{commit}}", capture_output=True, text=True).stdout.strip()
    version = read_version(git(repo, "show", f"{commit}:CMakeLists.txt", capture_output=True, text=True).stdout)
    if args.version and args.version != version:
        parser.error(f"Requested version differs from application version {version} at {commit}")
    args.version = version
    prefix = f"Chroma-{args.version}/"
    revisions = [f"Chroma: {commit}"]
    sources = [(repo, commit, prefix)]
    tree = git(repo, "ls-tree", "-rz", commit, capture_output=True).stdout
    for entry in tree.split(b"\0"):
        if not entry:
            continue
        header, path_bytes = entry.split(b"\t", 1)
        mode, kind, sha = header.decode("ascii").split()
        if mode != "160000":
            continue
        path = path_bytes.decode("utf-8")
        if PurePosixPath(path).is_absolute() or ".." in PurePosixPath(path).parts:
            raise ValueError(f"Invalid submodule path: {path}")
        subrepo = repo / path
        if not (subrepo / ".git").exists():
            raise RuntimeError(f"Initialize submodule first: {path}")
        revision = sha
        git(subrepo, "cat-file", "-e", f"{revision}^{{commit}}")
        sources.append((subrepo, revision, prefix + path + "/"))
        revisions.append(f"{path}: {revision}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="chroma-source-", dir=args.output.parent) as temporary:
        staged_archive = Path(temporary) / args.output.name
        with tarfile.open(staged_archive, "w:gz", format=tarfile.PAX_FORMAT) as destination:
            seen = set()
            for index, (source_repo, revision, source_prefix) in enumerate(sources):
                raw = Path(temporary) / f"{index}.tar"
                git(source_repo, "archive", "--format=tar", f"--prefix={source_prefix}", f"--output={raw}", revision)
                with tarfile.open(raw, "r:") as source:
                    for member in source:
                        if member.name in seen:
                            continue
                        seen.add(member.name)
                        destination.addfile(member, source.extractfile(member) if member.isfile() else None)
            metadata = ("\n".join(revisions) + "\n").encode("utf-8")
            member = tarfile.TarInfo(prefix + "SOURCE-REVISION.txt")
            member.size = len(metadata)
            member.mode = 0o644
            member.mtime = int(git(repo, "show", "-s", "--format=%ct", commit, capture_output=True, text=True).stdout.strip())
            destination.addfile(member, io.BytesIO(metadata))
        staged_archive.rename(args.output)
    print(f"Created {args.output.resolve()} from {commit} (including {len(sources) - 1} submodule)")


if __name__ == "__main__":
    main()
