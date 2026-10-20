#!/usr/bin/env node

import { readFile } from "node:fs/promises";

const files = process.argv.slice(2).filter((file) =>
  file.startsWith("exercises/") && file.endsWith("/README.md"),
);
const required = [
  "Mission",
  "What you will observe",
  "Workspace",
  "Test it",
  "Validate",
  "Next",
];
const operational = new Set([
  "exercises/D00-deploy/README.md",
  "exercises/P00-complete-rootkit/README.md",
]);
let failed = 0;

for (const file of files) {
  const markdown = await readFile(file, "utf8");
  const headings = [...markdown.matchAll(/^## (.+)$/gm)].map((match) => match[1]);
  const implementation = headings.includes("TODOs") ? "TODOs" : "Implement it";
  const expected = operational.has(file)
    ? required
    : [...required.slice(0, 3), implementation, ...required.slice(3)];
  let previous = -1;
  for (const heading of expected) {
    const index = headings.indexOf(heading);
    if (index < 0) {
      process.stderr.write(`${file}: missing "## ${heading}"\n`);
      failed++;
    } else if (index <= previous) {
      process.stderr.write(`${file}: "## ${heading}" is out of order\n`);
      failed++;
    }
    previous = index;
  }
  for (const forbidden of ["Failure clinic", "Failure to avoid", "Useful interfaces", "If it fails"]) {
    if (headings.includes(forbidden)) {
      process.stderr.write(`${file}: remove top-level "## ${forbidden}"\n`);
      failed++;
    }
  }
}

if (failed > 0) process.exit(1);
