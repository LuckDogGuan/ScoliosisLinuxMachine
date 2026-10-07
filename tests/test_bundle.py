"""Run: python3 tests/test_bundle.py bundles/<label>/instrument-<label>.tar.gz.
Uses a temporary workspace; never installs files or preserves extracted fixtures.
"""
import hashlib
import pathlib
import subprocess
import sys
import tarfile
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
archive = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="instrument-bundle-check-") as temporary:
    work = pathlib.Path(temporary)
    with tarfile.open(archive) as stream:
        members = stream.getmembers()
        tops = {pathlib.PurePosixPath(m.name).parts[0] for m in members}
        assert len(tops) == 1
        for member in members:
            path = pathlib.PurePosixPath(member.name)
            assert not path.is_absolute() and ".." not in path.parts
            assert member.isdir() or member.isfile(), "No archive links/devices allowed"
        stream.extractall(work)
    package = work / tops.pop()
    script = package / "deploy/install_bundle.sh"

    def run(*args, success):
        result = subprocess.run(["sh", str(script), *args], text=True, capture_output=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result

    run("--check", success=True)
    run("--install", success=False)  # Explicit idle confirmation required.
    run("--unknown", success=False)
    original = subprocess.run(["sha256sum", "-c", "SOURCE_SHA256SUMS"], cwd=package, capture_output=True)
    assert original.returncode == 0, original.stderr
    tracked = {line.split("  ", 1)[1].strip().removeprefix("./") for line in (package / "SHA256SUMS").read_text().splitlines()}
    actual = {str(p.relative_to(package)) for p in package.rglob("*") if p.is_file() and p.name != "SHA256SUMS"}
    assert tracked == actual, "Every payload file must be in the manifest"
    wav = package / "sounds/capture_done.wav"
    wav.write_bytes(wav.read_bytes() + b"corruption")
    failure = run("--check", success=False)
    assert "FAILED" in failure.stdout, failure.stdout
    outer = hashlib.sha256(archive.read_bytes()).hexdigest()
    report = "PASS: safe archive layout, complete manifest, pristine payload, idle guard, invalid mode, tampered audio rejection.\n"
    report += "archive_sha256=" + outer + "\nfixture=" + str(work) + "\n"
    print(report)
