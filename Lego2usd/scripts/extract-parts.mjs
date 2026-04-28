#!/usr/bin/env node
// Walks LDraw type-1 references for a list of top-level parts and copies
// every transitive dependency from an unpacked LDraw library + LDCad shadow
// library into public/ldraw/. Run once after updating TARGET_PARTS.
//
// Usage:
//   LDRAW_ROOT=/tmp/ldraw-dl/ldraw SHADOW_ROOT=/tmp/ldraw-dl/shadow \
//     node scripts/extract-parts.mjs

import fs from 'node:fs';
import path from 'node:path';

const LDRAW_ROOT = process.env.LDRAW_ROOT || '/tmp/ldraw-dl/ldraw';
const SHADOW_ROOT = process.env.SHADOW_ROOT || '/tmp/ldraw-dl/shadow';
const OUT = path.resolve('public/ldraw');

const TARGET_PARTS = [
  '45601c01.dat',
  '54675.dat',
  '54696p01.dat',
  '64179.dat',
  '32009.dat',
];

const searchDirs = ['parts', 'p', 'p/48', 'p/8', 'parts/s'];

function findInLibrary(root, ref) {
  const lower = ref.toLowerCase().replace(/\\/g, '/');
  for (const d of searchDirs) {
    const p = path.join(root, d, lower);
    if (fs.existsSync(p)) return { abs: p, rel: path.join(d, lower) };
  }
  return null;
}

function copyFile(abs, rel) {
  const dst = path.join(OUT, rel);
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.copyFileSync(abs, dst);
}

function copyShadowIfExists(rel) {
  const abs = path.join(SHADOW_ROOT, rel);
  if (fs.existsSync(abs)) {
    const dst = path.join(OUT, 'shadow', rel);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.copyFileSync(abs, dst);
    return true;
  }
  return false;
}

const visited = new Set();
const shadowRefs = new Set();

function walk(ref) {
  const key = ref.toLowerCase();
  if (visited.has(key)) return;
  visited.add(key);

  const found = findInLibrary(LDRAW_ROOT, ref);
  if (!found) {
    console.warn(`[miss] ${ref}`);
    return;
  }
  copyFile(found.abs, found.rel);

  // every part/sub-part may have a shadow patch at the same rel path
  if (copyShadowIfExists(found.rel)) shadowRefs.add(found.rel);

  const content = fs.readFileSync(found.abs, 'utf8');
  for (const line of content.split(/\r?\n/)) {
    const parts = line.trim().split(/\s+/);
    if (parts[0] === '1' && parts.length >= 15) {
      const sub = parts.slice(14).join(' ');
      walk(sub);
    }
  }
}

// Also walk references inside shadow files (SNAP_INCL points at e.g. connhole.dat)
function walkShadowDeps() {
  const scan = (dirRel) => {
    const dir = path.join(OUT, 'shadow', dirRel);
    if (!fs.existsSync(dir)) return;
    for (const name of fs.readdirSync(dir)) {
      const full = path.join(dir, name);
      if (fs.statSync(full).isDirectory()) continue;
      const content = fs.readFileSync(full, 'utf8');
      const re = /!LDCAD\s+SNAP_INCL[^[\n]*\[ref=([^\]]+)\]/g;
      let m;
      while ((m = re.exec(content)) !== null) {
        const ref = m[1].trim();
        // shadow refs resolve against shadow/parts or shadow/p
        const tryPaths = [
          path.join('parts', ref),
          path.join('p', ref),
          path.join('p/48', ref),
        ];
        for (const rel of tryPaths) {
          if (copyShadowIfExists(rel)) {
            shadowRefs.add(rel);
            // also copy underlying primitive/part from official lib if not already
            if (!visited.has(ref.toLowerCase())) walk(ref);
            break;
          }
        }
      }
    }
  };
  // iterate repeatedly until closure stabilizes
  let prev = -1;
  while (shadowRefs.size !== prev) {
    prev = shadowRefs.size;
    scan('parts');
    scan('p');
  }
}

fs.mkdirSync(OUT, { recursive: true });
// Copy color definitions
for (const f of ['LDConfig.ldr']) {
  fs.copyFileSync(path.join(LDRAW_ROOT, f), path.join(OUT, f));
}

for (const p of TARGET_PARTS) walk(p);
walkShadowDeps();

console.log(`done. ${visited.size} files copied from library, ${shadowRefs.size} shadow files.`);
