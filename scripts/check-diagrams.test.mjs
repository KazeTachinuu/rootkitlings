import assert from "node:assert/strict";
import { mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";

const checker = new URL("./check-diagrams.mjs", import.meta.url);

async function check(markdown) {
  const directory = await mkdtemp(join(tmpdir(), "rklings-diagrams-"));
  const file = join(directory, "README.md");
  await writeFile(file, markdown);
  return spawnSync(process.execPath, [checker.pathname, file], {
    encoding: "utf8",
  });
}

test("accepts an aligned ASCII box", async () => {
  const result = await check("```text\n+---+\n| x |\n+---+\n```\n");
  assert.equal(result.status, 0, result.stderr);
});

test("rejects a shifted box row", async () => {
  const result = await check("```text\n+---+\n | x |\n+---+\n```\n");
  assert.equal(result.status, 1);
  assert.match(result.stderr, /does not close at columns/);
});

test("rejects a missing closing border", async () => {
  const result = await check("```text\n+---+\n| x |\n```\n");
  assert.equal(result.status, 1);
  assert.match(result.stderr, /unmatched box border/);
});

test("rejects Unicode architecture boxes", async () => {
  const result = await check("```text\n┌───┐\n│ x │\n└───┘\n```\n");
  assert.equal(result.status, 1);
  assert.match(result.stderr, /use ASCII characters/);
});

test("accepts tree output", async () => {
  const result = await check("```text\nroot\n├── one\n└── two\n```\n");
  assert.equal(result.status, 0, result.stderr);
});
