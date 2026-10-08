"""Exercise release source snapshots using disposable Git repositories only."""

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("archive_chroma_source", Path(__file__).with_name("archive-chroma-source.py"))
archiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archiver)


class SourceArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="chroma-source-test-")
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git(self.repo, "init", "-q")
        self.write("CMakeLists.txt", "set(Launcher_VERSION_MAJOR 0)\nset(Launcher_VERSION_MINOR 3)\nset(Launcher_VERSION_PATCH 0)\n")
        self.write("source.cpp", "original source\n")
        self.write("deleted.cpp", "removed later\n")
        self.write(".gitignore", ".tools/\n.chroma-test/\ndist/\nignored.txt\n")
        self.git(self.repo, "add", ".")
        self.git(self.repo, "commit", "-qm", "Synthetic fixture")

    def tearDown(self):
        self.temporary.cleanup()

    def git(self, repo, *args):
        return subprocess.run(["git", "-c", f"safe.directory={repo.as_posix()}", "-c", "user.name=Fixture",
                               "-c", "user.email=fixture@example.invalid", "-c", "core.autocrlf=false", "-C", str(repo), *args],
                              check=True, capture_output=True, text=True).stdout.strip()

    def write(self, path, value):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(value.encode("utf-8"))

    def test_working_snapshot_preserves_local_source_and_excludes_profile_files(self):
        self.write("CMakeLists.txt", "set(Launcher_VERSION_MAJOR 1)\nset(Launcher_VERSION_MINOR 0)\nset(Launcher_VERSION_PATCH 0)\n")
        self.write("source.cpp", "local source\n")
        self.write("new/source.cpp", "new feature\n")
        (self.repo / "deleted.cpp").unlink()
        for path in ("ignored.txt", ".tools/fixture.txt", ".chroma-test/fixture.txt", "dist/fixture.txt",
                     ".codex/fixture.txt", ".aws/fixture.txt", ".env", "new/.env.local", "accounts.json",
                     "skin-outfits/fixture.skinoutfit"):
            self.write(path, "synthetic excluded content\n")
        self.git(self.repo, "add", "-f", ".env", ".tools/fixture.txt")
        before = self.git(self.repo, "status", "--porcelain")
        first, second = self.root / "first.tar.gz", self.root / "second.tar.gz"
        archiver.archive(self.repo, first, working_tree=True, requested_version="1.0.0")
        archiver.archive(self.repo, second, working_tree=True)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(before, self.git(self.repo, "status", "--porcelain"))
        with tarfile.open(first) as archive:
            names = {name.removeprefix("Chroma-1.0.0/") for name in archive.getnames()}
            self.assertEqual(names, {"CMakeLists.txt", ".gitignore", "source.cpp", "new/source.cpp", "SOURCE-REVISION.txt", "SOURCE-MANIFEST.json"})
            self.assertEqual(archive.extractfile("Chroma-1.0.0/source.cpp").read(), b"local source\n")
            manifest = json.load(archive.extractfile("Chroma-1.0.0/SOURCE-MANIFEST.json"))
            source = next(entry for entry in manifest["files"] if entry["path"] == "source.cpp")
            self.assertEqual(source["sha256"], hashlib.sha256(b"local source\n").hexdigest())
            self.assertEqual(manifest["mode"], "working-tree")

    def test_committed_archive_keeps_release_revision_and_rejects_overwrite(self):
        self.write("source.cpp", "uncommitted\n")
        target = self.root / "release.tar.gz"
        archiver.archive(self.repo, target)
        with tarfile.open(target) as archive:
            self.assertEqual(archive.extractfile("Chroma-0.3.0/source.cpp").read(), b"original source\n")
        with self.assertRaisesRegex(ValueError, "already exists"):
            archiver.archive(self.repo, target)
        with self.assertRaisesRegex(ValueError, "differs"):
            archiver.archive(self.repo, self.root / "wrong.tar.gz", requested_version="1.0.0")

    def test_submodule_snapshot_records_checked_out_revision_and_rejects_local_changes(self):
        subrepo = self.root / "submodule-source"
        subrepo.mkdir()
        self.git(subrepo, "init", "-q")
        (subrepo / "library.cpp").write_bytes(b"library fixture\n")
        self.git(subrepo, "add", ".")
        self.git(subrepo, "commit", "-qm", "Synthetic library")
        self.git(self.repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(subrepo), "libraries/example")
        checked_out = self.repo / "libraries/example"
        (checked_out / "library.cpp").write_bytes(b"updated library fixture\n")
        self.git(checked_out, "commit", "-am", "Synthetic library update")
        actual = self.git(checked_out, "rev-parse", "HEAD")
        target = self.root / "with-submodule.tar.gz"
        archiver.archive(self.repo, target, working_tree=True)
        with tarfile.open(target) as archive:
            self.assertEqual(archive.extractfile("Chroma-0.3.0/libraries/example/library.cpp").read(), b"updated library fixture\n")
            self.assertIn(actual.encode(), archive.extractfile("Chroma-0.3.0/SOURCE-REVISION.txt").read())
        (checked_out / "library.cpp").write_bytes(b"local change\n")
        with self.assertRaisesRegex(RuntimeError, "Submodule has local changes"):
            archiver.archive(self.repo, self.root / "dirty.tar.gz", working_tree=True)

    def test_windows_text_symlinks_keep_link_type_and_refuse_external_targets(self):
        self.write("fixture-link", "source.cpp")
        blob = self.git(self.repo, "hash-object", "-w", "fixture-link")
        self.git(self.repo, "update-index", "--add", "--cacheinfo", "120000," + blob + ",fixture-link")
        target = self.root / "with-link.tar.gz"
        archiver.archive(self.repo, target, working_tree=True)
        with tarfile.open(target) as archive:
            member = archive.getmember("Chroma-0.3.0/fixture-link")
            self.assertTrue(member.issym())
            self.assertEqual(member.linkname, "source.cpp")
        self.write("fixture-link", "../outside-repository")
        with self.assertRaisesRegex(ValueError, "Invalid source symlink"):
            archiver.archive(self.repo, self.root / "external-link.tar.gz", working_tree=True)


if __name__ == "__main__":
    unittest.main()
