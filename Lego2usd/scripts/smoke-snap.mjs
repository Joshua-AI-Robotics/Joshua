// Standalone smoke test for the shadow-library snap parsing logic.
// Mirrors src/ldraw/snapParser.ts without Three.js, so we can run it in Node.
import fs from 'node:fs';
import path from 'node:path';

const SHADOW_ROOT = path.resolve('public/ldraw/shadow');

function parseBrackets(line) {
  const out = {};
  const re = /\[([^\]=]+)=([^\]]*)\]/g;
  let m;
  while ((m = re.exec(line)) !== null) {
    out[m[1].trim().toLowerCase()] = m[2].trim();
  }
  return out;
}

function parseNums(s) {
  if (!s) return [];
  return s.trim().split(/\s+/).map(Number).filter((n) => !Number.isNaN(n));
}

function summarizeSecs(secs) {
  if (!secs) return { radius: 0, length: 0 };
  const toks = secs.trim().split(/\s+/);
  let i = 0, totalLen = 0, maxR = 0;
  while (i < toks.length) {
    const r = parseFloat(toks[i + 1]);
    const l = parseFloat(toks[i + 2]);
    if (!Number.isNaN(r)) maxR = Math.max(maxR, r);
    if (!Number.isNaN(l)) totalLen += l;
    i += 3;
  }
  return { radius: maxR, length: totalLen };
}

function parseGrid(gridStr) {
  if (!gridStr) return null;
  const toks = gridStr.trim().split(/\s+/);
  let i = 0, cx = false, cy = false;
  if (toks[i] === 'C') { cx = true; i++; }
  const countX = parseInt(toks[i++], 10);
  if (toks[i] === 'C') { cy = true; i++; }
  const countY = parseInt(toks[i++], 10);
  const stepX = parseFloat(toks[i++]);
  const stepY = parseFloat(toks[i++]);
  if ([countX, countY, stepX, stepY].some((v) => Number.isNaN(v))) return null;
  const out = [];
  const offX = cx ? -((countX - 1) * stepX) / 2 : 0;
  const offY = cy ? -((countY - 1) * stepY) / 2 : 0;
  for (let a = 0; a < countX; a++)
    for (let b = 0; b < countY; b++)
      out.push([offX + a * stepX, 0, offY + b * stepY]);
  return out;
}

function readShadow(rel) {
  const candidates = rel.includes('/') ? [rel] : [`parts/${rel}`, `p/${rel}`];
  for (const c of candidates) {
    const p = path.join(SHADOW_ROOT, c);
    if (fs.existsSync(p)) return fs.readFileSync(p, 'utf8');
  }
  return null;
}

function collectHotspots(rel, depth = 0, acc = []) {
  if (depth > 6) return acc;
  const text = readShadow(rel);
  if (!text) return acc;
  for (const line of text.split(/\r?\n/)) {
    const t = line.trim();
    if (!t.startsWith('0 !LDCAD ')) continue;
    const rest = t.substring('0 !LDCAD '.length);
    const kind = rest.split(/\s|\[/)[0];
    const fields = parseBrackets(rest);
    if (kind === 'SNAP_CYL') {
      const { radius, length } = summarizeSecs(fields.secs);
      acc.push({
        kind,
        gender: fields.gender,
        slide: (fields.slide || 'false').toLowerCase() === 'true',
        pos: parseNums(fields.pos),
        radius,
        length,
        refPath: rel,
      });
    } else if (kind === 'SNAP_INCL') {
      const ref = fields.ref;
      if (!ref) continue;
      const grid = parseGrid(fields.grid);
      const repeats = grid ? grid.length : 1;
      for (let i = 0; i < repeats; i++) {
        collectHotspots(`parts/${ref}`, depth + 1, acc);
        if (!readShadow(`parts/${ref}`)) collectHotspots(`p/${ref}`, depth + 1, acc);
      }
    }
  }
  return acc;
}

for (const target of ['parts/32009.dat', 'parts/64179.dat']) {
  const spots = collectHotspots(target);
  console.log(`\n${target}: ${spots.length} hotspots`);
  for (const s of spots) {
    console.log(`  ${s.kind} gender=${s.gender} slide=${s.slide} r=${s.radius} len=${s.length} via ${s.refPath}`);
  }
}
