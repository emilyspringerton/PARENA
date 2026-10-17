#!/usr/bin/env python3
"""Generates ../rg_data.prn -- every table the REDGARDEN web client needs that the arena's wire protocol does not carry
(hero names and roles, ability names and descriptions, the item catalog, the static map) -- from dumps of the real game's
own code. The dumps in data/ come from tiny C programs linked against github.com/emilyspringerton/REDGARDEN
(`packages/simulation/arena_game.c`):

  rg_dump_map.c        obstacles, nodes, towers, fountains, shops, camps, spawns, the item catalog   -> data/map.txt
  rg_dump_abilities.c  arena_ability_name / arena_ability_description for all 31 heroes              -> data/abilities.txt
  rg_dump_layout.c     sizeof / offsetof of every wire struct in packages/common/protocol.h          -> data/layout.txt

Build recipe (from a REDGARDEN checkout):  gcc -std=c99 -D_DEFAULT_SOURCE -O2 -I packages -o dump \\
  -include simulation/<each mod>_host.h tools/rg_dump_map.c simulation/arena_game.c ... -lm   (see scripts/build.sh).
Run:  python3 rg_data_gen.py  > ../rg_data.prn
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")

HERO_NAMES = ["Unicorn", "Duck", "Ghost", "Frog", "Doc Wheel", "Tree", "Pizza", "Flamel", "Morrigan", "Dagda", "Courier",
              "Loki", "Gary", "Flute-Debt", "Bacon Puck", "Abraham", "Ada", "Tyler", "Paimon", "NOOR-1", "Cain", "Gunnr",
              "Vassago", "He Xiangu", "Beleth", "MnM", "Weatherman", "Zagan", "Warrior", "Cart", "Michael"]
# docs/HEROES_VS0.md's role for each (the headings), short
ROLES = ["Tank", "Fighter / Assassin", "Mage / Support", "Support", "Support", "Tank", "Mage", "Support", "Assassin / Duelist",
         "Bruiser / Support", "Fighter / Assassin", "Trickster / Bruiser", "Marksman", "DoT / Payoff", "Trickster / Skirmisher",
         "Caster", "Tank / Controller", "Fighter / Assassin", "Commander / Caster", "Scout", "Duelist", "Duelist",
         "Support / Diviner", "Support / Sustain", "Burst / Control", "Tank", "Fighter / Support", "Control / Disruptor",
         "Fighter", "Indirect control", "Support"]
SLOTS = ["WEAPON", "HEAD", "BODY", "HANDS", "LEGS", "FEET", "RING", "NECK", "BACK", "WAIST", "TRINKET"]


def q(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + s + '"'


def case_str(name, rows, argname="i", default='""'):
    out = [f"(defn {name} [({argname} : I32)] : String", f"  (case {argname}"]
    for k, v in rows:
        out.append(f"    {k} {q(v)}")
    out.append(f"    {default}))")
    return "\n".join(out)


def case_f(name, rows, argname="i", default="0.0"):
    out = [f"(defn {name} [({argname} : I32)] : F32", f"  (case {argname}"]
    for k, v in rows:
        out.append(f"    {k} {v:.3f}")
    out.append(f"    {default}))")
    return "\n".join(out)


def case_i(name, rows, argname="i", default="0"):
    out = [f"(defn {name} [({argname} : I32)] : I32", f"  (case {argname}"]
    for k, v in rows:
        out.append(f"    {k} {v}")
    out.append(f"    {default}))")
    return "\n".join(out)


def main():
    heroes = json.load(open(os.path.join(os.path.dirname(HERE), "..", "..", "..", "..", "WOTAN", "data", "redgarden-heroes.json")))["heroes"] \
        if os.path.exists(os.path.join(os.path.dirname(HERE), "..", "..", "..", "..", "WOTAN", "data", "redgarden-heroes.json")) else None
    names = HERO_NAMES
    if heroes:
        names = [heroes[str(i)] for i in range(31)]
    abil = {}
    desc = {}
    for line in open(os.path.join(D, "abilities.txt")):
        line = line.rstrip("\n")
        if line.startswith("H "):
            parts = line[2:].split("|")
            h = int(parts[0])
            for s in range(3):
                abil[(h, s)] = parts[1 + s]
        elif line.startswith("D "):
            head, text = line[2:].split("|", 1)
            h, s = (int(x) for x in head.split())
            desc[(h, s)] = text
    obst, nodes, towers, fount, shops, camps, items, pus = [], [], [], [], [], [], [], []
    half = 51.777
    for line in open(os.path.join(D, "map.txt")):
        f = line.split()
        if not f:
            continue
        if f[0] == "HALF":
            half = float(f[1])
        elif f[0] == "OBST":
            obst.append((int(f[1]), float(f[2]), float(f[3]), float(f[4]), int(f[5])))
        elif f[0] == "NODE":
            nodes.append((int(f[1]), float(f[2]), float(f[3])))
        elif f[0] == "TOWER":
            towers.append((int(f[1]), float(f[2]), float(f[3])))
        elif f[0] == "FOUNT":
            fount.append((int(f[1]), float(f[2]), float(f[3])))
        elif f[0] == "SHOP":
            shops.append((int(f[1]), float(f[2]), float(f[3])))
        elif f[0] == "CAMP":
            camps.append((int(f[1]), float(f[2]), float(f[3])))
        elif f[0] == "PU":
            pus.append((int(f[1]), float(f[2]), float(f[3])))
        elif f[0] == "ITEM":
            m = re.match(r"ITEM (\d+)\|(.*?)\|(\d+)\|(\d+)\|(.*)$", line.strip())
            idx, name, slot, cost, stats = int(m[1]), m[2], int(m[3]), int(m[4]), m[5]
            kv = dict(re.findall(r"([a-z]+)(-?[\d.]+)", stats))
            bits = []
            if int(kv.get("ad", 0)): bits.append("AD+%d" % int(kv["ad"]))
            if int(kv.get("hp", 0)): bits.append("HP+%d" % int(kv["hp"]))
            if int(kv.get("mp", 0)): bits.append("MP+%d" % int(kv["mp"]))
            if int(kv.get("ar", 0)): bits.append("ARM+%d" % int(kv["ar"]))
            if float(kv.get("ms", 0)) > 0: bits.append("SPD+%.1f" % float(kv["ms"]))
            items.append((idx, name, slot, cost, " ".join(bits)))
    out = []
    out.append(";; stdlib/redgarden/web/rg_data.prn -- GENERATED by tools/rg_data_gen.py from dumps of the real REDGARDEN code")
    out.append(";; (packages/simulation/arena_game.c). Do not edit by hand; regenerate. See tools/rg_data_gen.py.")
    out.append("")
    out.append(f"(defn map-half [] : F32 {half:.3f})")
    out.append("(defn hero-count [] : I32 31)")
    out.append("(defn item-count [] : I32 43)")
    out.append("(defn obstacle-count [] : I32 %d)" % len(obst))
    out.append("")
    out.append(case_str("hero-name", [(i, n) for i, n in enumerate(names)], "id", '"?"'))
    out.append("")
    out.append(case_str("hero-role", [(i, n) for i, n in enumerate(ROLES)], "id", '""'))
    out.append("")
    out.append(case_str("abil-name", [(h * 3 + s, abil[(h, s)]) for h in range(31) for s in range(3)], "k", '""'))
    out.append("")
    out.append(case_str("abil-desc", [(h * 3 + s, desc[(h, s)]) for h in range(31) for s in range(3)], "k", '""'))
    out.append("")
    out.append(case_str("item-name", [(i, n.upper()) for (i, n, s, c, st) in items], "i", '""'))
    out.append("")
    out.append(case_str("item-stat", [(i, st) for (i, n, s, c, st) in items], "i", '""'))
    out.append("")
    out.append(case_i("item-slot", [(i, s) for (i, n, s, c, st) in items], "i", "0"))
    out.append("")
    out.append(case_i("item-cost", [(i, c) for (i, n, s, c, st) in items], "i", "0"))
    out.append("")
    out.append(case_str("slot-name", list(enumerate(SLOTS)), "s", '""'))
    out.append("")
    out.append(case_f("obst-x", [(i, x) for (i, x, z, r, k) in obst]))
    out.append("")
    out.append(case_f("obst-z", [(i, z) for (i, x, z, r, k) in obst]))
    out.append("")
    out.append(case_f("obst-r", [(i, r) for (i, x, z, r, k) in obst], default="1.0"))
    out.append("")
    out.append(case_i("obst-kind", [(i, k) for (i, x, z, r, k) in obst]))
    out.append("")
    out.append(case_f("fountain-x", [(i, x) for (i, x, z) in fount]))
    out.append("")
    out.append(case_f("fountain-z", [(i, z) for (i, x, z) in fount]))
    out.append("")
    out.append(case_f("shop-x", [(i, x) for (i, x, z) in shops]))
    out.append("")
    out.append(case_f("shop-z", [(i, z) for (i, x, z) in shops]))
    out.append("")
    out.append(case_f("camp-x", [(i, x) for (i, x, z) in camps]))
    out.append("")
    out.append(case_f("camp-z", [(i, z) for (i, x, z) in camps]))
    out.append("")
    out.append(case_f("powerup-x", [(i, x) for (i, x, z) in pus]))
    out.append("")
    out.append(case_f("powerup-z", [(i, z) for (i, x, z) in pus]))
    print("\n".join(out))


main()
