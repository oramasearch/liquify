#!/usr/bin/env node

import { execFileSync, spawnSync } from "node:child_process";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";

const ROOT = dirname(fileURLToPath(import.meta.url));
const REPOSITORY_ROOT = dirname(ROOT);
const temporary = mkdtempSync(join(tmpdir(), "liquify-token-gate-"));

function run(script, arguments_) {
  execFileSync(process.execPath, [join(ROOT, script), ...arguments_], { stdio: "inherit" });
}

function mustFail(script, arguments_, message) {
  const result = spawnSync(process.execPath, [join(ROOT, script), ...arguments_], {
    stdio: "ignore",
  });
  if (result.status === 0) {
    throw new Error(message);
  }
}

try {
  run("sync-design-system.mjs", ["--check"]);
  run("build-tokens.mjs", ["--check"]);

  const generated = join(temporary, "design_tokens.generated.hpp");
  cpSync(join(REPOSITORY_ROOT, "include", "liquify", "design_tokens.generated.hpp"), generated);
  writeFileSync(generated, `${readFileSync(generated, "utf8")}\n// changed\n`);
  mustFail("build-tokens.mjs", ["--check-file", generated], "changed output was accepted");

  const vendor = join(temporary, "orama-design");
  cpSync(join(ROOT, "vendor", "orama-design"), vendor, { recursive: true });
  const color = join(vendor, "tokens", "dtcg", "primitive", "color.tokens.json");
  writeFileSync(color, `${readFileSync(color, "utf8")}\n`);
  mustFail(
    "sync-design-system.mjs",
    ["--check", "--vendor-dir", vendor],
    "changed vendored token was accepted",
  );

  console.log("token-gates: ok — generated output and snapshot changes were rejected");
} finally {
  rmSync(temporary, { recursive: true, force: true });
}
