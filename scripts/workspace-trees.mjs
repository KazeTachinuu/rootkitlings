#!/usr/bin/env node

import { readFile, writeFile } from "node:fs/promises";
import { dirname, basename } from "node:path";
import { spawnSync } from "node:child_process";

const args = process.argv.slice(2);
const check = args[0] === "--check";
const files = check ? args.slice(1) : args;
let stale = 0;

function actualTree(directory) {
  const result = spawnSync(
    "tree",
    ["--noreport", "-a", "-I", ".rklings|README.md|certs", directory],
    { encoding: "utf8" },
  );

  if (result.error?.code === "ENOENT") {
    throw new Error("tree is required to format workspace diagrams");
  }
  if (result.status !== 0) {
    throw new Error(result.stderr.trim() || `tree failed for ${directory}`);
  }

  return result.stdout.trimEnd();
}

for (const file of files) {
  if (!file.startsWith("exercises/") || !file.endsWith("/README.md")) {
    continue;
  }

  const markdown = await readFile(file, "utf8");
  const heading = markdown.indexOf("## Workspace\n");
  if (heading < 0) {
    continue;
  }

  const nextHeading = markdown.indexOf("\n## ", heading + 1);
  const sectionEnd = nextHeading < 0 ? markdown.length : nextHeading;
  const section = markdown.slice(heading, sectionEnd);
  const fence = /```text\n([\s\S]*?)\n```/.exec(section);
  if (fence === null) {
    continue;
  }

  const directory = dirname(file);
  const firstLine = fence[1].split("\n", 1)[0].replace(/\/$/, "");
  if (firstLine !== directory && firstLine !== basename(directory)) {
    continue;
  }

  const tree = actualTree(directory);
  if (fence[1] === tree) {
    continue;
  }

  if (check) {
    process.stderr.write(`${file}: workspace tree is stale; run make format-docs\n`);
    stale++;
    continue;
  }

  const fenceStart = heading + fence.index;
  const contentStart = fenceStart + fence[0].indexOf(fence[1]);
  const updated =
    markdown.slice(0, contentStart) + tree + markdown.slice(contentStart + fence[1].length);
  await writeFile(file, updated);
}

if (stale > 0) {
  process.exit(1);
}
