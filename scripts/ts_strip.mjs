// Type-strips a PARENA-generated .ts file to plain JavaScript using Node's own TypeScript stripper
// (node:module stripTypeScriptTypes, no npm dependency). parena's TS target emits only erasable
// syntax (type annotations), which is exactly what "strip" mode accepts.
//   node scripts/ts_strip.mjs <in.ts> <out.js>
import fs from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
const [inp, out] = process.argv.slice(2);
if (!inp || !out) { console.error('usage: ts_strip.mjs <in.ts> <out.js>'); process.exit(2); }
const js = stripTypeScriptTypes(fs.readFileSync(inp, 'utf8'), { mode: 'strip' });
fs.writeFileSync(out, js);
