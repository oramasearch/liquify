#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Exercise real rendering mutations, restoring source and binaries before returning."""

import argparse
import os
import re
import shutil
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def run(command, log, env):
    result = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True)
    output = result.stdout + result.stderr
    log.write_text(output)
    print(output, end="", flush=True)
    return result.returncode, output


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f"Expected exactly one mutation target: {old}")
    return source.replace(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    args = parser.parse_args()
    build = args.build.resolve()
    if not build.is_relative_to(ROOT):
        raise RuntimeError("The proof build must be inside this worktree")
    output = build / "property-proof"
    output.mkdir(exist_ok=True)
    env = os.environ.copy()
    env["CMAKE_BUILD_PARALLEL_LEVEL"] = "6"
    env["LIQUID_HOME"] = str(output / "home")
    source = ROOT / "src/app.cpp"
    original = source.read_bytes()
    baseline = original.decode()
    targets = ["cmake", "--build", str(build), "-j", "6", "--target",
               "liquify-native-window-test", "liquify-tests"]
    tests = ["ctest", "--test-dir", str(build), "--output-on-failure", "-V",
             "-R", r"^(liquify-native-window|Screenshot.PanelRendersDiagnostics)$"]
    mutations = [
        ("baseline", baseline, None),
        ("blank-shell", replace_once(
            baseline,
            "  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);",
            "  // Proof mutation: leave the cleared framebuffer untouched."),
         "shell is blank: no geometry differs from the clear color"),
        ("quarter-size", replace_once(
            baseline,
            re.search(r"SDL_SetRenderScale\(renderer_, framebuffer_scale\.x,\s*framebuffer_scale\.y\)",
                      baseline).group(),
            "SDL_SetRenderScale(renderer_, framebuffer_scale.x * 0.5F,\n                          framebuffer_scale.y * 0.5F)"),
         "geometry does not fill the window"),
        ("moved-toolbar", replace_once(
            baseline,
            "void App::draw_editor_toolbar() {",
            "void App::draw_editor_toolbar() {\n"
            "  // Proof mutation: move every toolbar group by one layout-derived row.\n"
            "  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFrameHeight());"),
         None),
    ]
    try:
        for name, modified, expected in mutations:
            source.write_text(modified)
            code, _ = run(targets, output / f"{name}-build.log", env)
            if code:
                raise RuntimeError(f"{name}: build failed")
            code, log = run(tests, output / f"{name}.log", env)
            native = build / "tests/native-window"
            if native.exists():
                shutil.copytree(native, output / f"{name}-native", dirs_exist_ok=True)
            for theme in ("light", "dark"):
                image = build / f"tests/diagnostics-{theme}-test.png"
                if image.exists():
                    shutil.copy2(image, output / f"{name}-diagnostics-{theme}.png")
            if expected is None:
                if code:
                    raise RuntimeError(f"{name}: expected both property tests to pass")
            elif code == 0 or log.count(expected) < 2 or "0% tests passed, 2 tests failed" not in log:
                raise RuntimeError(f"{name}: both tests must fail with {expected!r}")
            print(f"property-proof: {name} PASS ({'expected rejection' if expected else 'unchanged assertions'})")
    finally:
        source.write_bytes(original)
        code, _ = run(targets, output / "restored-build.log", env)
        if code:
            raise RuntimeError("Restored source did not rebuild")
    code, _ = run(tests, output / "restored.log", env)
    if code:
        raise RuntimeError("Restored property tests failed")


if __name__ == "__main__":
    main()
