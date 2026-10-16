// Parity test: BRAWLPIT's C engine (physics.h + local_game.h) vs the PARENA wasm engine.
//
//   node brawlpit_parity_test.mjs <brawlpit.wasm> <parity_ref binary> <BRAWLPIT repo dir> [quick]
//
// For every scenario the C reference plays a scripted match (tests/brawlpit_parity_ref.c) and
// records the complete state after each tick. This test feeds the *recorded inputs* to the wasm
// engine and requires every player word, every projectile and the match-over flag to be identical
// after every tick -- bit for bit (floats are compared as their raw 32-bit patterns). One wrong
// constant, one reordered float expression or one missed branch shows up as the first diverging
// frame, with the field name.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const [wasmPath, refBin, bpDir, quick] = process.argv.slice(2);
const wasmBytes = fs.readFileSync(wasmPath);
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bp-parity-'));

const FIELDS = ['id', 'character_id', 'active', 'is_bot', 'x', 'y', 'vx', 'vy', 'on_ground', 'facing', 'in_x', 'in_y',
  'btn_jump', 'btn_jump_prev', 'btn_attack', 'btn_attack_prev', 'btn_shield', 'btn_shield_prev', 'btn_special',
  'btn_special_prev', 'state', 'damage_percent', 'stocks', 'shield_health', 'shield_regen_timer', 'shield_stun_frames',
  'shield_drop_timer', 'shield_windup_frames', 'shield_overpowered_frames', 'jumps_remaining', 'ground_platform_type',
  'drop_through_timer', 'wavedash_frames', 'dash_cooldown', 'special_b_cooldown', 'umbrella_open', 'upb_frame',
  'upb_landing_lag', 'parasol_rehit_timer', 'rosie_dash_frame', 'hitlag_frames', 'hit_flash_timer', 'hit_flash_multihit',
  'hitstun_frames', 'attack_cooldown', 'attack_timer', 'parry_timer', 'smash_charge_timer', 'smash_active_timer',
  'smash_charge_level', 'launch_delay_frames', 'pending_kb_x', 'pending_kb_y', 'smash_release_timer',
  'smash_flash_timer', 'invuln_frames', 'respawn_timer', 'kills', 'deaths'];
const TURNIP_FIELDS = ['active', 'x', 'y', 'vx', 'vy', 'owner_id', 'ttl_frames', 'style'];
const NCOMPARE = FIELDS.length; // 59 words; 59..63 are web-only
const PLAYERS = 139264, TURNIPS = 143360, G = 135168;
const REC_WORDS = 12 + 8 * 64 + 16 * 8 + 3;

const f32 = new Float32Array(1), u32 = new Uint32Array(f32.buffer);
const asFloat = (u) => { u32[0] = u; return f32[0]; };
const bitsOf = (f) => { f32[0] = f; return u32[0]; };

// ---- coverage: which game events did the C reference actually exercise? ---------------------------
// A parity test that never reaches a branch proves nothing about it. Every scenario's C trace is
// tallied here and the run asserts the interesting events all happened (and, since the wasm engine
// matched bit for bit through them, that the PARENA port agrees on every one).
const cover = {};
const bump = (k) => { cover[k] = (cover[k] || 0) + 1; };
const IX = Object.fromEntries(FIELDS.map((n, i) => [n, i]));
function tally(trace, cfg) {
  const frames = trace.length / REC_WORDS;
  const prev = new Array(8).fill(null);
  for (let f = 0; f < frames; f++) {
    const b = f * REC_WORDS + 12;
    for (let p = 0; p < 8; p++) {
      const w = (k) => trace[b + p * 64 + IX[k]];
      if (!w('active')) continue;
      const st = w('state');
      bump('state ' + st);
      if (w('hitlag_frames')) bump('hitlag');
      if (w('launch_delay_frames')) bump('high-percent launch delay');
      if (w('parry_timer')) bump('parry window');
      if (w('shield_overpowered_frames')) bump('overpowered shield');
      if (w('shield_windup_frames')) bump('shield windup');
      if (w('shield_stun_frames')) bump('shield stun');
      if (w('smash_charge_timer')) bump('smash charging');
      if (w('smash_active_timer')) bump('smash active');
      if (w('rosie_dash_frame')) bump('rosie rush');
      if (w('upb_frame')) bump('up-B');
      if (w('umbrella_open')) bump('umbrella');
      if (w('respawn_timer')) bump('respawning');
      if (w('invuln_frames')) bump('invulnerable');
      if (w('hit_flash_timer')) bump('hit flash');
      if (w('drop_through_timer')) bump('drop-through');
      if (w('wavedash_frames')) bump('wavedash');
      if (w('shield_health') === bitsOf(0) && w('state') === 4) bump('shield broken');
      const sb = w('special_b_cooldown'), dc = w('dash_cooldown');
      const pv = prev[p];
      if (pv && (sb === 45 || sb === 35) && pv.sb < sb) bump('special-B by char ' + w('character_id'));
      if (pv && dc === 30 && pv.dc < dc && st === 13) bump('dash/wavedash');
      if (pv && w('stocks') < pv.stocks) bump('stock lost (KO)');
      if (pv && asFloat(w('damage_percent')) > asFloat(pv.dmg) + 0.01) bump('damage taken');
      if (pv && w('hitstun_frames') > pv.hs + 5) bump('hitstun applied');
      prev[p] = { sb, dc, stocks: w('stocks'), dmg: w('damage_percent'), hs: w('hitstun_frames') };
    }
    for (let t = 0; t < 16; t++) if (trace[b + 512 + t * 8]) { bump('projectile style ' + trace[b + 512 + t * 8 + 7]); }
    if (trace[b + 512 + 128]) bump('match over');
    if (trace[b + 512 + 130]) bump('match restarted');
  }
}

let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

async function replay(trace, cfg) {
  const { instance } = await WebAssembly.instantiate(wasmBytes, {});
  const x = instance.exports;
  const mem = new Uint32Array(x.memory.buffer);
  x.init_fighters();
  x.bp_seed_bang(cfg.seed);
  x.set_slot_char_bang(0, cfg.c0);
  for (let i = 1; i < 8; i++) x.set_slot_char_bang(i, cfg.c1);
  x.sim_init_match(cfg.n, cfg.mode, cfg.stage);
  if (cfg.two && cfg.n > 1) x.set_p_is_bot_bang(1, 0);
  const frames = trace.length / REC_WORDS;
  for (let f = 0; f < frames; f++) {
    const b = f * REC_WORDS;
    const inp = (o) => [asFloat(trace[b + o]), asFloat(trace[b + o + 1]), trace[b + o + 2], trace[b + o + 3], trace[b + o + 4], trace[b + o + 5]];
    // local_update() returns before touching input once the match is over; mirror that.
    if (mem[(G + 8) / 4] === 0) {
      x.sim_set_input_bang(0, ...inp(0));
      if (cfg.two && cfg.n > 1) x.sim_set_input_bang(1, ...inp(6));
    }
    x.sim_step();
    // players
    for (let p = 0; p < 8; p++) {
      for (let k = 0; k < NCOMPARE; k++) {
        const want = trace[b + 12 + p * 64 + k];
        const got = mem[(PLAYERS + p * 512) / 4 + k];
        if (want !== got) {
          return `frame ${f + 1} player ${p} ${FIELDS[k]}: C=${want >>> 0} (${asFloat(want)}) wasm=${got >>> 0} (${asFloat(got)})`;
        }
      }
    }
    for (let t = 0; t < 16; t++) {
      for (let k = 0; k < 8; k++) {
        const want = trace[b + 12 + 512 + t * 8 + k];
        const got = mem[(TURNIPS + t * 32) / 4 + k];
        if (want !== got) return `frame ${f + 1} turnip ${t} ${TURNIP_FIELDS[k]}: C=${want >>> 0} (${asFloat(want)}) wasm=${got >>> 0} (${asFloat(got)})`;
      }
    }
    const wantOver = trace[b + 12 + 512 + 128], gotOver = mem[(G + 8) / 4];
    if (wantOver !== gotOver) return `frame ${f + 1} match_over: C=${wantOver} wasm=${gotOver}`;
    if (trace[b + 12 + 512 + 130]) { // the C reference restarted the match after this tick
      x.sim_init_match(cfg.n, cfg.mode, cfg.stage);
      if (cfg.two && cfg.n > 1) x.set_p_is_bot_bang(1, 0);
    }
  }
  return null;
}

const scenarios = [];
const stages = [0, 1, 4];
for (let c = 0; c < 10; c++) {
  scenarios.push({ name: `char ${c} vs bot (stage ${stages[c % 3]})`, seed: 100 + c, frames: 1800, n: 2, stage: stages[c % 3], c0: c, c1: (c + 3) % 10, two: 0, mode: 0 });
}
for (let c = 0; c < 10; c++) {
  scenarios.push({ name: `char ${c} 4-player FFA (stage ${stages[(c + 1) % 3]})`, seed: 200 + c, frames: 1500, n: 4, stage: stages[(c + 1) % 3], c0: c, c1: (c * 7 + 1) % 10, two: 0, mode: 0 });
}
for (let c = 0; c < 10; c++) {
  scenarios.push({ name: `char ${c} vs char ${(c + 5) % 10} two humans (stage ${stages[(c + 2) % 3]})`, seed: 300 + c, frames: 1500, n: 2, stage: stages[(c + 2) % 3], c0: c, c1: (c + 5) % 10, two: 1, mode: 0 });
}
for (let c = 0; c < 10; c += 3) {
  scenarios.push({ name: `char ${c} sandbox practice`, seed: 400 + c, frames: 900, n: 2, stage: 0, c0: c, c1: 6, two: 0, mode: 2 });
}
// aggressive scripted human (chases and swings) vs a bot, vs a passive human dummy, and in a brawl
for (let c = 0; c < 10; c++) {
  scenarios.push({ name: `char ${c} aggressive vs bot (stage ${stages[c % 3]})`, seed: 500 + c, frames: 3600, n: 2, stage: stages[c % 3], c0: c, c1: (c + 4) % 10, two: 0, mode: 0, aggro: 85 });
  scenarios.push({ name: `char ${c} aggressive vs idle human dummy (stage ${stages[(c + 1) % 3]})`, seed: 600 + c, frames: 3600, n: 2, stage: stages[(c + 1) % 3], c0: c, c1: (c + 6) % 10, two: 1, mode: 0, aggro: 90, dummy: 1 });
  scenarios.push({ name: `char ${c} aggressive 3-way brawl (stage ${stages[(c + 2) % 3]})`, seed: 700 + c, frames: 3600, n: 3, stage: stages[(c + 2) % 3], c0: c, c1: (c + 2) % 10, two: 1, mode: 0, aggro: 80 });
}
// special-move spam against a dummy, so every character's up/down specials (and the projectiles)
// are exercised many times over
for (let c = 0; c < 10; c++) {
  scenarios.push({ name: `char ${c} special-move spam vs dummy (stage ${stages[c % 3]})`, seed: 800 + c, frames: 1500, n: 2, stage: stages[c % 3], c0: c, c1: (c + 1) % 10, two: 1, mode: 0, aggro: 101, dummy: 1 });
}
const run = quick ? scenarios.filter((_, i) => i % 4 === 0) : scenarios;

let totalFrames = 0;
for (const cfg of run) {
  const out = path.join(tmp, 'trace.bin');
  execFileSync(refBin, [out, cfg.seed, cfg.frames, cfg.n, cfg.stage, cfg.c0, cfg.c1, cfg.two, cfg.mode, cfg.aggro || 0, cfg.dummy || 0], { cwd: bpDir });
  const buf = fs.readFileSync(out);
  const trace = new Uint32Array(buf.buffer, buf.byteOffset, buf.byteLength / 4);
  tally(trace, cfg);
  const bad = await replay(trace, cfg);
  totalFrames += cfg.frames;
  ok(bad === null, `${cfg.name}: ${cfg.frames} ticks bit-identical to physics.h` + (bad ? `\n      first divergence: ${bad}` : ''));
}
fs.rmSync(tmp, { recursive: true, force: true });
if (!quick) {
  const must = ['state 0', 'state 3', 'state 4', 'state 5', 'state 6', 'state 7', 'state 13', 'state 14', 'state 15',
    'hitlag', 'high-percent launch delay', 'parry window', 'overpowered shield', 'shield windup', 'shield stun',
    'smash charging', 'smash active', 'rosie rush', 'up-B', 'umbrella', 'respawning', 'invulnerable', 'hit flash',
    'drop-through', 'wavedash', 'stock lost (KO)', 'damage taken', 'hitstun applied', 'match over',
    'projectile style 0', 'projectile style 3', 'projectile style 10', 'projectile style 11'];
  for (let c = 0; c < 10; c++) if (c !== 7) must.push('special-B by char ' + c); // Raccoon's specials ride dash_cooldown
  must.push('dash/wavedash');
  const missing = must.filter((k) => !cover[k]);
  ok(missing.length === 0, 'every listed game event was exercised by the scripted matches' +
     (missing.length ? ' -- MISSING: ' + missing.join(', ') : ''));
  console.log('coverage (ticks/events): ' + Object.entries(cover).sort().map(([k, v]) => `${k}=${v}`).join('  '));
}
console.log(`\n${pass} passed, ${fail} failed (${totalFrames} ticks compared, ${NCOMPARE} words x 8 players + projectiles each)`);
process.exit(fail ? 1 : 0);
