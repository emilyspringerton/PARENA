// scripts/frontend_vs0_wasm_test.mjs -- the GFD-frontend VS0 rules, asked of the wasm module itself.
//
//   node scripts/frontend_vs0_wasm_test.mjs [build/frontend_vs0.wasm]
//
// Every expected value is hand-derived from the four .prn files, not read back out of the module. The
// point is the seam: SHANKPIT's C host compiles the SAME sources (packages/simulation/era_tech_rules.c
// etc.), so a disagreement here means the browser and the game would disagree about a train, an ore or a
// citizen's morning.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = process.argv[2] || path.join(here, '..', 'build', 'frontend_vs0.wasm');

const { instance } = await WebAssembly.instantiate(fs.readFileSync(wasmPath), {});
const x = instance.exports;

let pass = 0, fail = 0;
const ok = (cond, msg) => { if (cond) pass++; else { fail++; console.log('FAIL: ' + msg); } };
const eq = (got, want, msg) => ok(got === want, `${msg}: got ${got}, want ${want}`);

// ---- the calendar: 1700, and a century per era ------------------------------------------------
eq(x.era_count(), 4, 'era count');
eq(x.era_start(), 0, 'the world starts in era 0');
eq(x.era_year(0), 1700, 'era 0 is 1700');
eq(x.era_year(3), 2000, 'era 3 is 2000');

// ---- the tree: track -> steam -> freight/passenger -> signal -> armored ------------------------
eq(x.tech_count(), 6, 'six nodes');
eq(x.tech_cost(0), 2, 'track costs 2');
eq(x.tech_cost(5), 8, 'armored costs 8');
eq(x.tech_prereq(0), -1, 'nothing before track');
eq(x.tech_prereq(1), 0, 'steam needs track');
eq(x.tech_prereq(5), 4, 'armored needs signal');

eq(x.tech_can_buy(2, 0, 0, 0), 1, 'exactly enough points buys track');
eq(x.tech_can_buy(1, 0, 0, 0), 0, 'one short is refused');
eq(x.tech_can_buy(5, 1, 0, 0), 0, 'steam without track is refused');
eq(x.tech_can_buy(5, 1, 0, 1), 1, 'steam after track is allowed');
eq(x.tech_can_buy(99, 2, 0, 3), 0, 'freight cannot be had in the 1700s');
eq(x.tech_can_buy(99, 2, 1, 3), 1, 'freight in the 1800s is fine');
eq(x.tech_can_buy(99, 0, 0, 1), 0, 'an already-known node cannot be rebought');
eq(x.tech_next_mask(0, 0), 1, 'buying track sets bit 0');
eq(x.tech_next_mask(1, 0), 1, 'buying track twice is idempotent');

// ---- the train: track + steam, and what it can carry ------------------------------------------
eq(x.train_unlocked(1), 0, 'track alone is not a train');
eq(x.train_unlocked(2), 0, 'steam alone is not a train');
eq(x.train_unlocked(3), 1, 'track + steam is a train');
eq(x.train_capacity(0), 0, 'no train, no seats');
eq(x.train_capacity(3), 4, 'a plain train seats 4');
eq(x.train_capacity(7), 8, 'freight adds 4');
eq(x.train_capacity(15), 12, 'freight and passenger add 8');
eq(x.train_speed_milli(3, 0), 700, '1700s freight speed');
eq(x.train_speed_milli(11, 0), 850, 'passenger service adds 150 permille');
eq(x.train_speed_milli(3, 3), 1000, 'a 2000s train is 1000 permille');
eq(x.train_speed_milli(0, 3), 0, 'no train, no speed');
eq(x.rails_cost_per_tile(0), 3, 'rails cost 3 a tile in 1700');
eq(x.rails_cost_per_tile(3), 1, 'rails cost 1 a tile in 2000');

// ---- turning the century: research, not a timer ------------------------------------------------
eq(x.era_can_advance(0, 3), 1, 'a railway advances the 1700s');
eq(x.era_can_advance(0, 1), 0, 'track alone does not');
eq(x.era_can_advance(1, 15), 1, 'freight + passenger advance the 1800s');
eq(x.era_can_advance(1, 7), 0, 'freight alone does not');
eq(x.era_can_advance(2, 15), 0, 'the 1900s need the signal network');
eq(x.era_can_advance(2, 31), 1, 'signal advances the 1900s');
eq(x.era_advance(0, 3), 1, 'era advances to 1');
eq(x.era_advance(0, 1), 0, 'era stays at 0');

// ---- mining: a swing, a vein, a cart ------------------------------------------------------------
eq(x.pick_strike_ms(0), 1200, 'stone pick swing');
eq(x.pick_strike_ms(3), 450, 'steel pick swing');
eq(x.strike_yield(0, 0), 4, 'stone pick on coal');
eq(x.strike_yield(0, 3), 10, 'steel pick on coal');
eq(x.strike_yield(3, 0), 1, 'stone pick on gold still takes one');
eq(x.strike_yield(3, 3), 2, 'steel pick on gold');
eq(x.vein_deplete(5, 4), 1, 'vein 5 minus 4');
eq(x.vein_deplete(3, 10), 0, 'a vein cannot go negative');
eq(x.vein_deplete(-5, 2), 0, 'a negative vein reads as empty');
eq(x.vein_ratio_milli(500, 1000), 500, 'half a seam');
eq(x.vein_ratio_milli(0, 0), 0, 'no capacity, no ratio');
eq(x.stamina_cost_milli(3), 2200, 'steel is exhausting');
eq(x.can_strike(1000, 0, 0), 1, 'a full bar swings a stone pick');
eq(x.can_strike(999, 0, 0), 0, 'not quite enough stamina');
eq(x.can_strike(5000, 10, 0), 0, 'mid-swing is refused');
eq(x.ore_from_variant(0), 0, 'variant 0 is coal');
eq(x.ore_from_variant(3), 1, 'variant 3 is iron');
eq(x.ore_from_variant(4), 1, 'variant 4 is iron');
eq(x.ore_from_variant(5), 2, 'variant 5 is copper');
eq(x.ore_from_variant(6), 3, 'variant 6 is gold');
eq(x.ore_from_variant(7), 4, 'variant 7 is a gem');
eq(x.ore_from_variant(8), 0, 'the variant table wraps');
eq(x.xp_per_ore(4), 10, 'a gem is a story');
eq(x.tool_integrity_step(1000, 10, 0), 990, 'ten stone swings');
eq(x.tool_integrity_step(1000, 10, 3), 960, 'ten steel swings wear faster');
eq(x.tool_integrity_step(5, 10, 0), 0, 'a tool cannot go negative');
eq(x.smelt_bars(0, 10), 0, 'coal is fuel, not a bar');
eq(x.smelt_bars(1, 10), 10, 'iron smelts one to one');
eq(x.smelt_bars(4, 3), 0, 'gems are cut, not smelted');
eq(x.smelt_fuel_needed(7), 7, 'one coal a bar');
eq(x.carry_capacity(0), 20, 'a stone-tier cart');
eq(x.carry_capacity(3), 50, 'a steel-tier cart');

// ---- the citizens' day -------------------------------------------------------------------------
eq(x.cd_phase(359), 0, '05:59 is night');
eq(x.cd_phase(360), 1, '06:00 is morning');
eq(x.cd_phase(719), 1, '11:59 is morning');
eq(x.cd_phase(720), 2, '12:00 is afternoon');
eq(x.cd_phase(1079), 2, '17:59 is afternoon');
eq(x.cd_phase(1080), 3, '18:00 is evening');
eq(x.cd_phase(2000), 3, 'past midnight clamps');
eq(x.cd_activity(0, 1), 1, 'a crafter works in the morning');
eq(x.cd_activity(0, 3), 5, 'and socialises at night');
eq(x.cd_activity(0, 0), 0, 'everyone sleeps');
eq(x.cd_activity(1, 1), 2, 'a trader runs the route');
eq(x.cd_activity(1, 3), 3, 'then drinks');
eq(x.cd_activity(2, 2), 4, 'a duellist patrols');
eq(x.cd_activity(3, 1), 6, 'an endgame player travels');
eq(x.cd_activity(3, 2), 1, 'and then works');
eq(x.cd_activity(4, 1), 7, 'an idle citizen drifts');
eq(x.cd_activity(4, 2), 5, 'and socialises by afternoon');
eq(x.cd_activity_in_public(0), 0, 'sleep is private');
eq(x.cd_activity_in_public(3), 1, 'drinking is public');
eq(x.cd_activity_zone_kind(1), 1, 'work is at a workplace');
eq(x.cd_activity_zone_kind(3), 3, 'drinking is at a bar');
eq(x.cd_activity_zone_kind(7), 7, 'idling can be anywhere');
eq(x.cd_hunger_step(0, 1), 60, 'hunger in the morning');
eq(x.cd_hunger_step(500, 0), 500, 'sleeping does not make you hungry');
eq(x.cd_hunger_step(1000, 2), 1000, 'hunger clamps');
eq(x.cd_energy_step(1000, 0, 0), 1000, 'sleep cannot overfill');
eq(x.cd_energy_step(500, 1, 1), 380, 'a morning shift drains 120');
eq(x.cd_energy_step(100, 2, 2), 0, 'energy clamps at zero');
eq(x.cd_mood_step(500, 500, 500), 500, 'a fed, rested citizen is neutral');
eq(x.cd_mood_step(500, 0, 0), 540, 'a fed, rested citizen drifts 40 above neutral');
eq(x.cd_mood_step(0, 1000, 1000), 0, 'mood clamps at zero');
eq(x.cd_wage(0, 1), 6, 'a crafter earns 6 an hour');
eq(x.cd_wage(1, 2), 9, 'a trader earns 9 on the route');
eq(x.cd_wage(4, 1), 1, 'an idle citizen earns almost nothing');
eq(x.cd_wage(1, 0), 0, 'sleeping pays nothing');
eq(x.cd_should_seek_home(0, 1000), 1, 'night sends them home');
eq(x.cd_should_seek_home(1, 100), 1, 'exhaustion sends them home');
eq(x.cd_should_seek_home(2, 500), 0, 'a rested citizen stays out');
eq(x.cd_should_socialize(200, 2), 1, 'a low mood finds company');
eq(x.cd_should_socialize(500, 2), 0, 'a fine mood does not');
eq(x.cd_should_socialize(200, 0), 0, 'nobody socialises at night');

// ---- contacts: Animal Crossing with a police state ---------------------------------------------
eq(x.cc_gift_worth(3), 260, 'a tech gift is worth 260');
eq(x.cc_affinity_step(100, 3, 0), 360, 'a tech gift moves affinity 260');
eq(x.cc_affinity_step(100, 0, 1), 20, 'one insult costs 80');
eq(x.cc_affinity_step(990, 3, 0), 1000, 'affinity clamps at the top');
eq(x.cc_affinity_step(10, 0, 2), 0, 'affinity clamps at zero');
eq(x.cc_contact_chance_milli(1000, 100), 550, 'full affinity and clean trust');
eq(x.cc_contact_chance_milli(0, -100), 0, 'a stranger with a record gets nothing');
eq(x.cc_contact_share(1000, 100, 549), 1, 'roll under the chance shares');
eq(x.cc_contact_share(1000, 100, 550), 0, 'roll at the chance does not');
eq(x.cc_service_can_ask(200, 0), 1, 'commerce unlocks at 200');
eq(x.cc_service_can_ask(199, 0), 0, 'not at 199');
eq(x.cc_service_can_ask(600, 3), 1, 'a mercenary at 600');
eq(x.cc_service_can_ask(599, 3), 0, 'not at 599');
eq(x.cc_service_price_milli(0, 0), 1000, 'commerce at list price');
eq(x.cc_service_price_milli(0, 1000), 800, 'a friend pays 20% less');
eq(x.cc_service_price_milli(3, 1000), 1800, 'a friend mercenary is 1800');
eq(x.cc_mercenary_strength(1000), 200, 'a friend mercenary is a real body');
eq(x.cc_mercenary_strength(0), 100, 'a stranger is a warm body');
eq(x.cc_affinity_decay(0), 0, 'affinity cannot decay negative');
eq(x.cc_affinity_decay(100), 99, 'affinity fades a point a day');
eq(x.cc_known(4, 2), 1, 'bit 2 in the address book');
eq(x.cc_add(0, 2), 4, 'adding contact 2 sets bit 2');
eq(x.cc_add(4, 2), 4, 'adding twice is idempotent');

// ---- one run of the actual loop: 1700 -> a working railway -> the 1800s -------------------------
{
  let era = x.era_start(), known = 0, points = 2 + 3;
  ok(x.tech_can_buy(points, 0, era, known) === 1, 'the opening move is track');
  known = x.tech_next_mask(known, 0); points -= x.tech_cost(0);
  ok(x.tech_can_buy(points, 1, era, known) === 1, 'then the boiler');
  known = x.tech_next_mask(known, 1); points -= x.tech_cost(1);
  eq(known, 3, 'track + steam');
  ok(x.train_unlocked(known) === 1, 'there is a train in the 1700s');
  ok(x.train_capacity(known) === 4, 'and it seats four');
  eq(x.era_advance(era, known), 1, 'the railway advances the century');
  ok(x.cd_activity(1, 2) === 2, 'meanwhile the traders run the route');
}

console.log(`${pass} passed, ${fail} failed`);
if (fail) process.exit(1);
