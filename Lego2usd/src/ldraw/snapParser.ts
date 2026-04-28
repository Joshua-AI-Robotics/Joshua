import * as THREE from 'three';

// A resolved snap hotspot in the part's local frame.
export type SnapHotspot = {
  id: string;
  kind: 'cyl' | 'clp' | 'gen' | 'fgr';
  gender: 'M' | 'F' | null;
  profile: 'round' | 'axle' | 'other';
  center: THREE.Vector3;       // center of the connector in part-local space
  axis: THREE.Vector3;         // unit axis direction
  radius: number;              // primary radius in LDU
  length: number;              // total length in LDU
  slide: boolean;              // slide=true => prismatic motion allowed
};

type Bracketed = Record<string, string>;

function parseBrackets(line: string): Bracketed {
  const out: Bracketed = {};
  const re = /\[([^\]=]+)=([^\]]*)\]/g;
  let m;
  while ((m = re.exec(line)) !== null) {
    out[m[1].trim().toLowerCase()] = m[2].trim();
  }
  return out;
}

function parseNums(s: string | undefined): number[] {
  if (!s) return [];
  return s.trim().split(/\s+/).map(Number).filter((n) => !Number.isNaN(n));
}

function buildMatrix(pos: string | undefined, ori: string | undefined): THREE.Matrix4 {
  const p = parseNums(pos);
  const o = parseNums(ori);
  const m = new THREE.Matrix4();
  if (o.length === 9) {
    // LDraw SNAP ori is row-major 3x3
    m.set(
      o[0], o[1], o[2], p[0] ?? 0,
      o[3], o[4], o[5], p[1] ?? 0,
      o[6], o[7], o[8], p[2] ?? 0,
      0, 0, 0, 1,
    );
  } else {
    m.setPosition(p[0] ?? 0, p[1] ?? 0, p[2] ?? 0);
  }
  return m;
}

function buildLDrawMatrix(nums: number[]): THREE.Matrix4 | null {
  if (nums.length < 12) return null;
  const [x, y, z, a, b, c, d, e, f, g, h, i] = nums;
  const m = new THREE.Matrix4();
  m.set(
    a, b, c, x,
    d, e, f, y,
    g, h, i, z,
    0, 0, 0, 1,
  );
  return m;
}

// Parse a "secs" spec like "R 8 2   R 6 16   R 8 2" or "A 6 20" into segments.
// Returns the dominant radius and total length.
function summarizeSecs(
  secs: string | undefined,
): { radius: number; length: number; profile: SnapHotspot['profile'] } {
  if (!secs) return { radius: 0, length: 0, profile: 'other' };
  const toks = secs.trim().split(/\s+/);
  let i = 0;
  let totalLen = 0;
  let maxRadius = 0;
  let profile: SnapHotspot['profile'] = 'other';
  while (i < toks.length) {
    // token[i] = 'R' | 'A' | 'S' ; i+1 = radius ; i+2 = length
    if (i === 0) {
      const sectionType = toks[i].toUpperCase();
      profile = sectionType === 'R'
        ? 'round'
        : sectionType === 'A'
          ? 'axle'
          : 'other';
    }
    const r = parseFloat(toks[i + 1]);
    const l = parseFloat(toks[i + 2]);
    if (!Number.isNaN(r)) maxRadius = Math.max(maxRadius, r);
    if (!Number.isNaN(l)) totalLen += l;
    i += 3;
  }
  return { radius: maxRadius, length: totalLen, profile };
}

// Compose an absolute hotspot from a snap-cyl line in its local matrix.
function hotspotFromCyl(
  fields: Bracketed,
  xform: THREE.Matrix4,
  idx: number,
): SnapHotspot {
  const scaleMode = (fields.scale || '').toLowerCase();
  const { radius, length, profile } = summarizeSecs(fields.secs);

  if (scaleMode === 'yonly') {
    const start = new THREE.Vector3(0, 0, 0).applyMatrix4(xform);
    const end = new THREE.Vector3(0, length || 1, 0).applyMatrix4(xform);
    const axisVec = end.clone().sub(start);
    const effectiveLength = axisVec.length();
    const axis = effectiveLength > 0
      ? axisVec.clone().normalize()
      : new THREE.Vector3(0, 1, 0).transformDirection(xform).normalize();
    const center = start.clone().add(end).multiplyScalar(0.5);
    const gender = fields.gender ? (fields.gender.toUpperCase() as 'M' | 'F') : null;
    const slide = (fields.slide || 'false').toLowerCase() === 'true';

    return {
      id: fields.id || `cyl${idx}`,
      kind: 'cyl',
      gender,
      profile,
      center,
      axis,
      radius,
      length: effectiveLength,
      slide,
    };
  }

  const localMat = buildMatrix(fields.pos, fields.ori);
  const worldMat = new THREE.Matrix4().multiplyMatrices(xform, localMat);

  const center = new THREE.Vector3().setFromMatrixPosition(worldMat);
  // Cylinder axis in LDraw snap is the local Y axis by convention.
  const axis = new THREE.Vector3(0, 1, 0).transformDirection(worldMat).normalize();

  const centerFlag = (fields.center || 'false').toLowerCase() === 'true';
  if (!centerFlag) center.addScaledVector(axis, length / 2);

  const gender = fields.gender ? (fields.gender.toUpperCase() as 'M' | 'F') : null;
  const slide = (fields.slide || 'false').toLowerCase() === 'true';

  return {
    id: fields.id || `cyl${idx}`,
    kind: 'cyl',
    gender,
    profile,
    center,
    axis,
    radius,
    length,
    slide,
  };
}

// In-memory cache of shadow files by relative path (e.g. "parts/connhole.dat")
const shadowFileCache = new Map<string, Promise<string | null>>();
const ldrawFileCache = new Map<string, Promise<string | null>>();

async function fetchShadow(shadowRoot: string, rel: string): Promise<string | null> {
  if (shadowFileCache.has(rel)) return shadowFileCache.get(rel)!;
  const p = (async () => {
    // Try both parts/ and p/ subdirs (SNAP_INCL refs are bare filenames)
    const candidates = rel.includes('/')
      ? [rel]
      : [`parts/${rel}`, `p/${rel}`];
    for (const c of candidates) {
      const res = await fetch(shadowRoot + c);
      if (res.ok) return await res.text();
    }
    return null;
  })();
  shadowFileCache.set(rel, p);
  return p;
}

async function fetchLDraw(ldrawRoot: string, rel: string): Promise<string | null> {
  const key = `${ldrawRoot}|${rel}`;
  if (ldrawFileCache.has(key)) return ldrawFileCache.get(key)!;
  const p = (async () => {
    const res = await fetch(ldrawRoot + rel);
    if (res.ok) return await res.text();
    return null;
  })();
  ldrawFileCache.set(key, p);
  return p;
}

async function resolveLDrawRef(
  ldrawRoot: string,
  currentRel: string,
  ref: string,
): Promise<string | null> {
  const cleanRef = ref.replace(/\\/g, '/').toLowerCase();
  const currentDir = currentRel.includes('/')
    ? currentRel.slice(0, currentRel.lastIndexOf('/'))
    : '';
  const candidates = cleanRef.includes('/')
    ? [
        currentDir ? `${currentDir}/${cleanRef}` : cleanRef,
        cleanRef,
        `parts/${cleanRef}`,
        `p/${cleanRef}`,
      ]
    : [
        currentDir ? `${currentDir}/${cleanRef}` : cleanRef,
        `parts/${cleanRef}`,
        `p/${cleanRef}`,
      ];

  for (const candidate of [...new Set(candidates)]) {
    if (await fetchLDraw(ldrawRoot, candidate)) return candidate;
  }
  return null;
}

function parseLDrawSubfile(line: string): { transform: THREE.Matrix4; ref: string } | null {
  const toks = line.trim().split(/\s+/);
  if (toks[0] !== '1' || toks.length < 15) return null;
  const nums = toks.slice(2, 14).map(Number);
  if (nums.some((n) => Number.isNaN(n))) return null;
  const transform = buildLDrawMatrix(nums);
  if (!transform) return null;
  return { transform, ref: toks.slice(14).join(' ') };
}

// Walk a shadow file and recursively expand SNAP_INCL references.
async function collectHotspots(
  rel: string,
  shadowRoot: string,
  xform: THREE.Matrix4,
  out: SnapHotspot[],
  depth = 0,
): Promise<void> {
  if (depth > 6) return; // safety
  const text = await fetchShadow(shadowRoot, rel);
  if (!text) return;

  const lines = text.split(/\r?\n/);
  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed.startsWith('0 !LDCAD ')) continue;
    const rest = trimmed.substring('0 !LDCAD '.length);
    const kind = rest.split(/\s|\[/)[0];
    const fields = parseBrackets(rest);

    if (kind === 'SNAP_CYL') {
      out.push(hotspotFromCyl(fields, xform, out.length));
    } else if (kind === 'SNAP_INCL') {
      const ref = fields.ref;
      if (!ref) continue;
      const local = buildMatrix(fields.pos, fields.ori);
      const child = new THREE.Matrix4().multiplyMatrices(xform, local);
      // grid expansion — fetchShadow resolves bare names against parts/ then p/.
      const grid = parseGrid(fields.grid);
      if (grid) {
        for (const g of grid) {
          const gm = new THREE.Matrix4().makeTranslation(g[0], g[1], g[2]);
          const cm = new THREE.Matrix4().multiplyMatrices(child, gm);
          await collectHotspots(ref, shadowRoot, cm, out, depth + 1);
        }
      } else {
        await collectHotspots(ref, shadowRoot, child, out, depth + 1);
      }
    }
    // SNAP_CLP / SNAP_GEN / SNAP_FGR intentionally ignored for first milestone
  }
}

async function collectLDrawPrimitiveHotspots(
  rel: string,
  ldrawRoot: string,
  shadowRoot: string,
  xform: THREE.Matrix4,
  out: SnapHotspot[],
  stack: Set<string>,
  depth = 0,
): Promise<void> {
  if (depth > 12 || stack.has(rel)) return;
  const text = await fetchLDraw(ldrawRoot, rel);
  if (!text) return;

  const nextStack = new Set(stack).add(rel);
  const lines = text.split(/\r?\n/);
  for (const line of lines) {
    const subfile = parseLDrawSubfile(line);
    if (!subfile) continue;
    const childRel = await resolveLDrawRef(ldrawRoot, rel, subfile.ref);
    if (!childRel) continue;
    const childXform = new THREE.Matrix4().multiplyMatrices(xform, subfile.transform);
    if (await fetchShadow(shadowRoot, childRel)) {
      await collectHotspots(childRel, shadowRoot, childXform, out, depth + 1);
    } else {
      await collectLDrawPrimitiveHotspots(
        childRel,
        ldrawRoot,
        shadowRoot,
        childXform,
        out,
        nextStack,
        depth + 1,
      );
    }
  }
}

// grid=[[C] countX [C] countY stepX stepY]  — expand to XZ translation list.
// Each dimension may be independently prefixed with 'C' to indicate it's
// centered around the reference position.
function parseGrid(gridStr: string | undefined): [number, number, number][] | null {
  if (!gridStr) return null;
  const toks = gridStr.trim().split(/\s+/);
  let i = 0;
  let centeredX = false;
  let centeredY = false;
  if (toks[i] === 'C') { centeredX = true; i++; }
  const countX = parseInt(toks[i++], 10);
  if (toks[i] === 'C') { centeredY = true; i++; }
  const countY = parseInt(toks[i++], 10);
  const stepX = parseFloat(toks[i++]);
  const stepY = parseFloat(toks[i++]);
  if ([countX, countY, stepX, stepY].some((v) => Number.isNaN(v))) return null;
  const out: [number, number, number][] = [];
  const offX = centeredX ? -((countX - 1) * stepX) / 2 : 0;
  const offY = centeredY ? -((countY - 1) * stepY) / 2 : 0;
  for (let a = 0; a < countX; a++) {
    for (let b = 0; b < countY; b++) {
      out.push([offX + a * stepX, 0, offY + b * stepY]);
    }
  }
  return out;
}

/**
 * Parse a part's shadow file and return its snap hotspots in the part's local
 * frame (LDraw coordinates: Y down, 1 unit = 1 LDU).
 *
 * `partFile` is the path used to load the part, e.g. "parts/32009.dat".
 * `shadowRoot` is the URL prefix where the shadow library is served, e.g. "/ldraw/shadow/".
 */
export async function parseShadowPart(
  partFile: string,
  shadowRoot: string,
  ldrawRoot?: string,
): Promise<SnapHotspot[]> {
  const rel = partFile.replace(/^\/+/, '').toLowerCase();
  const hotspots: SnapHotspot[] = [];
  await collectHotspots(rel, shadowRoot, new THREE.Matrix4(), hotspots);
  if (hotspots.length === 0 && ldrawRoot) {
    await collectLDrawPrimitiveHotspots(
      rel,
      ldrawRoot,
      shadowRoot,
      new THREE.Matrix4(),
      hotspots,
      new Set(),
    );
  }
  // Give each hotspot a unique id
  const seen = new Set<string>();
  const unique = hotspots.filter((h) => {
    const key = [
      h.kind,
      h.gender ?? '',
      h.profile,
      h.slide ? 'slide' : 'fixed',
      h.center.x.toFixed(3),
      h.center.y.toFixed(3),
      h.center.z.toFixed(3),
      h.axis.x.toFixed(3),
      h.axis.y.toFixed(3),
      h.axis.z.toFixed(3),
      h.radius.toFixed(3),
      h.length.toFixed(3),
    ].join('|');
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
  return unique.map((h, i) => ({ ...h, id: `${h.id}_${i}` }));
}
