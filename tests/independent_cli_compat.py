"""Exercise independent CLI and IDE artifacts; never create a repository through the IDE library."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(*command, cwd=None, expected=0):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    print(result.stdout, end="", flush=True)
    print(result.stderr, end="", flush=True)
    if result.returncode != expected:
        raise RuntimeError(f"{command[0]} exited {result.returncode}, expected {expected}")
    return result.stdout + result.stderr


def cache_value(cache, key):
    for line in cache.read_text().splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"{cache} has no {key}")


def independent_cli(args):
    if args.cli:
        cli = Path(args.cli).resolve()
    else:
        source = args.work / "cli-source"
        build = args.work / "cli-build"
        if not source.exists():
            run("git", "clone", "--no-checkout", "https://github.com/oramasearch/liquid.git", str(source))
        # The clone is cached between runs, so a revision pinned after it was made
        # is not present yet; without this fetch the test can only ever check out
        # revisions that existed when the directory was first created.
        run("git", "fetch", "--quiet", "origin", cwd=source)
        run("git", "checkout", "--detach", args.revision, cwd=source)
        run("git", "submodule", "update", "--init", "--recursive", cwd=source)
        run("cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Release", "-DLIQUID_BUILD_CLI=ON",
            "-DLIQUID_BUILD_TESTS=OFF", "-DLIQUID_GRAMMARS=c;c++")
        run("cmake", "--build", str(build), "--target", "liquid", "-j", "6")
        cli = build / ("liquid.exe" if os.name == "nt" else "liquid")
    cache = cli.parent / "CMakeCache.txt"
    source = Path(cache_value(cache, "CMAKE_HOME_DIRECTORY")).resolve()
    pinned_source = Path(__file__).resolve().parents[1] / "modules" / "liquid"
    if source == pinned_source.resolve() or cache_value(cache, "CMAKE_PROJECT_NAME") != "liquid":
        raise RuntimeError("CLI must come from a separate liquid source checkout and CMake project")
    if cache_value(cache, "LIQUID_BUILD_CLI") != "ON":
        raise RuntimeError("The independent build must enable LIQUID_BUILD_CLI")
    revision = run("git", "rev-parse", "HEAD", cwd=source).strip()
    if revision != args.revision:
        raise RuntimeError(f"CLI source revision {revision} differs from supported {args.revision}")
    print(f"Independent CLI source: {source}\nIndependent CLI artifact: {cli}", flush=True)
    run(str(cli), "version")
    return cli


def check_repository(args, cli, repository, file):
    output = run(str(args.ide), "--print-tree", str(repository))
    if file not in output:
        raise RuntimeError(f"IDE tree does not list {file}")
    run(str(args.editor), str(repository), file)
    # Read the IDE's saved store through the independent CLI, then check its exported bytes.
    run(str(cli), "cut", "Compatibility round trip", cwd=repository)
    exported = repository.parent / (repository.name + "-git")
    run(str(cli), "export", "git", str(exported), cwd=repository)
    saved = run("git", "-C", str(exported), "show", f"HEAD:{file}")
    if not saved.endswith("saved by liquify compatibility check\n"):
        raise RuntimeError("Independent CLI read-back lost the IDE edit")
    print(f"Independent CLI read-back: {file} PASS", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ide", type=Path, required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--cli", default="")
    parser.add_argument("--lake-repository", type=Path)
    parser.add_argument("--lake-file", default="README.md")
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    for name in ("home", "tmp"):
        (args.work / name).mkdir(exist_ok=True)
    os.environ["LIQUID_HOME"] = str(args.work / "home")
    os.environ["TMPDIR"] = str(args.work / "tmp")
    cli = independent_cli(args)
    run(str(args.ide), "version")
    with tempfile.TemporaryDirectory(prefix="run-", dir=args.work) as directory:
        root = Path(directory)
        repository = root / "first-repository"
        repository.mkdir()
        (repository / "notes.txt").write_text("first repository\n")
        run(str(cli), "init", str(repository))
        check_repository(args, cli, repository, "notes.txt")
        unsupported = root / "unsupported-repository"
        shutil.copytree(repository, unsupported)
        objects = unsupported / ".liquid-store" / "packs" / "objects.meta"
        data = bytearray(objects.read_bytes())
        if data[:5] != b"LQOBJ":
            raise RuntimeError("CLI fixture has no recognized object-map tag")
        data[5] = 6
        objects.write_bytes(data)
        output = run(str(args.ide), "--print-tree", str(unsupported), expected=1)
        for required in ("object map v6", "Upgrade liquify", args.revision):
            if required not in output:
                raise RuntimeError(f"Unsupported-format diagnostic lacks {required}")
        if "invalid object map" in output or objects.read_bytes() != data:
            raise RuntimeError("Unsupported map was misdiagnosed or changed")
    if args.lake_repository:
        check_repository(args, cli, args.lake_repository.resolve(), args.lake_file)
    print("Independent CLI -> IDE -> CLI compatibility PASS", flush=True)


if __name__ == "__main__":
    main()
