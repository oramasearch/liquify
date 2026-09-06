#!/usr/bin/env node

import { existsSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = dirname(fileURLToPath(import.meta.url));
const REPOSITORY_ROOT = dirname(ROOT);
const DTCG_ROOT = join(ROOT, "vendor", "orama-design", "tokens", "dtcg");
const MAP_PATH = join(ROOT, "design-tokens.map.json");
const INVENTORY_PATH = join(ROOT, "design-tokens.inventory.json");
const FONT_ROOT = join(ROOT, "fonts");
const OUTPUT_PATH = join(REPOSITORY_ROOT, "include", "liquify", "design_tokens.generated.hpp");
const ALIAS = /^\{([^}]+)\}$/;

function fail(message) {
  console.error(`build-tokens: ${message}`);
  process.exit(1);
}

function parseArguments(argv) {
  const parsed = { check: false, checkFile: "" };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--check") {
      parsed.check = true;
      continue;
    }
    if (argument === "--check-file") {
      const value = argv[index + 1];
      if (!value) {
        fail("--check-file requires a path");
      }
      parsed.check = true;
      parsed.checkFile = resolve(value);
      index += 1;
      continue;
    }
    fail(`unrecognized argument: ${argument}`);
  }
  return parsed;
}

function tokenFiles(directory) {
  const files = [];
  function visit(current) {
    for (const entry of readdirSync(current, { withFileTypes: true }).sort((left, right) =>
      left.name.localeCompare(right.name),
    )) {
      const path = join(current, entry.name);
      if (entry.isDirectory()) {
        visit(path);
      } else if (entry.name.endsWith(".tokens.json")) {
        files.push(path);
      }
    }
  }
  visit(directory);
  return files;
}

function flatten(document, source) {
  const tokens = new Map();
  function visit(node, prefix, inheritedType) {
    const type = node.$type ?? inheritedType;
    for (const [key, child] of Object.entries(node)) {
      if (key.startsWith("$")) {
        continue;
      }
      const path = prefix ? `${prefix}.${key}` : key;
      if (child && typeof child === "object" && "$value" in child) {
        tokens.set(path, {
          dark: child.$extensions?.["orama.dark"],
          path,
          source,
          type: child.$type ?? type,
          value: child.$value,
        });
      } else if (child && typeof child === "object") {
        visit(child, path, type);
      }
    }
  }
  visit(document, "", undefined);
  return tokens;
}

function resolved(value, tokens, theme, seen = []) {
  if (typeof value !== "string" || !ALIAS.test(value)) {
    return value;
  }
  const path = value.match(ALIAS)?.[1];
  if (!path || !tokens.has(path)) {
    fail(`unresolved alias ${value}`);
  }
  if (seen.includes(path)) {
    fail(`alias cycle: ${[...seen, path].join(" -> ")}`);
  }
  const token = tokens.get(path);
  const themedValue = theme === "dark" && token.dark !== undefined ? token.dark : token.value;
  return resolved(themedValue, tokens, theme, [...seen, path]);
}

function valueFor(path, tokens, theme) {
  const token = tokens.get(path);
  if (!token) {
    fail(`unknown DTCG path ${path}`);
  }
  const value = theme === "dark" && token.dark !== undefined ? token.dark : token.value;
  return resolved(value, tokens, theme, [path]);
}

function color(value, path) {
  if (typeof value !== "string" || !/^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(value)) {
    fail(`${path} is not a hexadecimal color`);
  }
  const bytes = value.slice(1).match(/../g).map((pair) => Number.parseInt(pair, 16));
  if (bytes.length === 3) {
    bytes.push(255);
  }
  return `Rgba{${bytes.map((byte) => `${byte}.0F / 255.0F`).join(", ")}}`;
}

function number(value, path, unit = "") {
  const text = String(value);
  if (unit && !text.endsWith(unit)) {
    fail(`${path} is not expressed in ${unit}`);
  }
  const parsed = Number.parseFloat(text);
  if (!Number.isFinite(parsed)) {
    fail(`${path} is not numeric`);
  }
  return `${parsed.toFixed(1)}F`;
}

function field(name) {
  return name.replace(/[A-Z]/g, (letter) => `_${letter.toLowerCase()}`);
}

function byteArray(name, path) {
  const bytes = readFileSync(path);
  const lines = [];
  for (let offset = 0; offset < bytes.length; offset += 16) {
    lines.push(
      `    ${[...bytes.subarray(offset, offset + 16)]
        .map((byte) => `0x${byte.toString(16).padStart(2, "0")}`)
        .join(", ")},`,
    );
  }
  return [
    `inline constexpr std::array<std::uint8_t, ${bytes.length}> ${name}{`,
    ...lines,
    "};",
  ];
}

function compile() {
  const tokens = new Map();
  for (const file of tokenFiles(DTCG_ROOT)) {
    const document = JSON.parse(readFileSync(file, "utf8"));
    for (const [path, token] of flatten(document, relative(DTCG_ROOT, file))) {
      if (tokens.has(path)) {
        fail(`duplicate DTCG path ${path}`);
      }
      tokens.set(path, token);
    }
  }
  const mapping = JSON.parse(readFileSync(MAP_PATH, "utf8"));
  const inventory = JSON.parse(readFileSync(INVENTORY_PATH, "utf8"));
  for (const category of ["colors", "dimensions", "lineHeights", "typeScale"]) {
    const mapped = Object.keys(mapping[category]).sort();
    const expected = [...inventory[category]].sort();
    if (JSON.stringify(mapped) !== JSON.stringify(expected)) {
      fail(`${category} map and inventory differ`);
    }
  }
  const colorEntries = Object.entries(mapping.colors);
  const lines = [
    "// Generated by design/build-tokens.mjs. Do not edit.",
    "#pragma once",
    "",
    "#include <array>",
    "#include <cstdint>",
    "",
    "namespace liquify::design {",
    "",
    "struct Rgba {",
    "    float red;",
    "    float green;",
    "    float blue;",
    "    float alpha;",
    "};",
    "",
    "struct Palette {",
    ...colorEntries.map(([name]) => `    Rgba ${field(name)};`),
    "};",
    "",
    "inline constexpr Palette light_palette{",
    ...colorEntries.map(([name, path]) => `    .${field(name)} = ${color(valueFor(path, tokens, "light"), path)},`),
    "};",
    "",
    "inline constexpr Palette dark_palette{",
    ...colorEntries.map(([name, path]) => `    .${field(name)} = ${color(valueFor(path, tokens, "dark"), path)},`),
    "};",
    "",
    ...Object.entries(mapping.dimensions).map(
      ([name, path]) => `inline constexpr float ${field(name)} = ${number(valueFor(path, tokens, "light"), path, "px")};`,
    ),
    "",
    ...Object.entries(mapping.lineHeights).map(
      ([name, path]) => `inline constexpr float line_height_${field(name)} = ${number(valueFor(path, tokens, "light"), path)};`,
    ),
    "",
    ...Object.entries(mapping.typeScale).map(
      ([name, path]) => `inline constexpr float ${field(name)} = ${number(valueFor(path, tokens, "light"), path, "px")};`,
    ),
    "",
    ...byteArray("ibm_plex_sans_regular", join(FONT_ROOT, "IBMPlexSans-Regular.ttf")),
    "",
    ...byteArray("ibm_plex_mono_regular", join(FONT_ROOT, "IBMPlexMono-Regular.ttf")),
    "",
    "} // namespace liquify::design",
    "",
  ];
  return lines.join("\n");
}

const arguments_ = parseArguments(process.argv.slice(2));
const generated = compile();
if (arguments_.check) {
  const checkedPath = arguments_.checkFile || OUTPUT_PATH;
  if (!existsSync(checkedPath) || readFileSync(checkedPath, "utf8") !== generated) {
    fail(`${relative(REPOSITORY_ROOT, checkedPath)} does not match a fresh compile`);
  }
  console.log(`build-tokens: ok — ${relative(REPOSITORY_ROOT, checkedPath)} matches`);
} else {
  writeFileSync(OUTPUT_PATH, generated);
  console.log(`build-tokens: wrote ${relative(REPOSITORY_ROOT, OUTPUT_PATH)}`);
}
