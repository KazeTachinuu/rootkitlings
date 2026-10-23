#!/usr/bin/env node

import { readFile } from "node:fs/promises";

const borderPattern = /\+(?:[-=]+\+)+/g;
const unicodeDrawingPattern = /[┌┐└┘├┤┬┴┼─│╭╮╯╰═║▶◀►◄▲▼→←]/u;
let failures = 0;

function report(file, line, message) {
  process.stderr.write(`${file}:${line}: ${message}\n`);
  failures++;
}

function borderSpans(line) {
  const spans = [];

  for (const match of line.matchAll(borderPattern)) {
    spans.push({ start: match.index, end: match.index + match[0].length - 1 });
  }

  return spans;
}

function hasVerticalBoundaries(line, span) {
  return line?.[span.start] === "|" && line?.[span.end] === "|";
}

function checkBlock(file, startLine, lines) {
  const open = new Map();
  const isTree = lines.some((line) => /(?:├──|└──)/u.test(line)) &&
    !lines.some((line) => line.includes("┌"));

  for (let row = 0; row < lines.length; row++) {
    const line = lines[row];

    if (!isTree && unicodeDrawingPattern.test(line)) {
      report(file, startLine + row, "use ASCII characters in text diagrams");
    }

    for (const span of borderSpans(line)) {
      const key = `${span.start}:${span.end}`;
      const top = open.get(key);

      if (top === undefined) {
        open.set(key, row);
        continue;
      }

      for (let bodyRow = top + 1; bodyRow < row; bodyRow++) {
        const body = lines[bodyRow];
        const left = body[span.start];
        const right = body[span.end];

        const boundary = new Set(["|", "+", "-", "<", ">", "="]);

        if (!boundary.has(left) || !boundary.has(right)) {
          report(
            file,
            startLine + bodyRow,
            `box opened on line ${startLine + top} does not close at columns ` +
              `${span.start + 1} and ${span.end + 1}`,
          );
        }
      }

      open.delete(key);
    }
  }

  for (const [key, row] of open) {
    const [start, end] = key.split(":").map(Number);
    const span = { start, end };

    if (hasVerticalBoundaries(lines[row - 1], span) ||
        hasVerticalBoundaries(lines[row + 1], span)) {
      report(file, startLine + row, `unmatched box border at columns ${key}`);
    }
  }

}

async function checkFile(file) {
  const lines = (await readFile(file, "utf8")).split("\n");
  let block = null;

  for (let index = 0; index < lines.length; index++) {
    const line = lines[index];

    if (block === null) {
      if (/^```text\s*$/.test(line)) {
        block = { startLine: index + 2, lines: [] };
      }
      continue;
    }

    if (/^```\s*$/.test(line)) {
      checkBlock(file, block.startLine, block.lines);
      block = null;
      continue;
    }

    block.lines.push(line);
  }

  if (block !== null) {
    report(file, block.startLine - 1, "unterminated text diagram fence");
  }
}

if (process.argv.length < 3) {
  process.stderr.write("usage: check-diagrams.mjs README...\n");
  process.exit(2);
}

for (const file of process.argv.slice(2)) {
  await checkFile(file);
}

if (failures > 0) {
  process.stderr.write(`diagram check failed with ${failures} issue(s)\n`);
  process.exit(1);
}
