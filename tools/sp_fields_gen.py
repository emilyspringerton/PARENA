#!/usr/bin/env python3
"""Generates stdlib/shankpit460/sp_fields.prn -- field accessors for the shankpit-460 web engine's
fixed-address records (players, projectiles, tracers, particles).

Same convention as tools/bp_fields_gen.py (BRAWLPIT web): PARENA's wasm backend has no structs and no
globals ("no malloc"), so every record is an array of 32-bit words at a fixed address. This is the one
place the word layout is written down. Run from the PARENA repo root:

    python3 tools/sp_fields_gen.py > stdlib/shankpit460/sp_fields.prn

Each field yields  (k-NAME) word index,  (p-NAME p) getter,  (set-p-NAME! p v) setter  (players, p = slot
0..9);  q-NAME / set-q-NAME! over a projectile slot; t-NAME over a tracer slot; f-NAME over a particle.
'i' = I32 word, 'f' = F32 word. PlayerState (shankpit-460/packages/common/protocol.h) is the model; the
words after the marker are web-client-only.
"""

PLAYER = [
    # --- protocol.h PlayerState (the subset the deathmatch uses) ---
    ("id", "i"), ("active", "i"), ("is-bot", "i"), ("state", "i"),
    ("x", "f"), ("y", "f"), ("z", "f"), ("vx", "f"), ("vy", "f"), ("vz", "f"),
    ("yaw", "f"), ("pitch", "f"), ("on-ground", "i"),
    ("in-fwd", "f"), ("in-strafe", "f"), ("in-jump", "i"), ("in-shoot", "i"), ("in-reload", "i"),
    ("crouching", "i"), ("in-ability", "i"),
    ("weapon", "i"),
    ("ammo0", "i"), ("ammo1", "i"), ("ammo2", "i"), ("ammo3", "i"), ("ammo4", "i"), ("ammo5", "i"),
    ("reload-timer", "i"), ("attack-cooldown", "i"), ("is-shooting", "i"),
    ("health", "i"), ("shield", "i"), ("shield-regen-timer", "i"),
    ("kills", "i"), ("deaths", "i"), ("hit-feedback", "i"), ("recoil-anim", "f"),
    ("respawn-ticks", "i"), ("storm-charges", "i"), ("ability-cooldown", "i"),
    ("katana-slash-timer", "i"), ("dash-timer", "i"),
    ("dash-vx", "f"), ("dash-vy", "f"), ("dash-vz", "f"),
    ("dash-hit-count", "i"),
    ("dash-hit0", "i"), ("dash-hit1", "i"), ("dash-hit2", "i"), ("dash-hit3", "i"),
    ("dash-hit4", "i"), ("dash-hit5", "i"), ("dash-hit6", "i"), ("dash-hit7", "i"),
    ("stunned-until", "i"), ("stun-immune-until", "i"),
    # --- web-client-only words ---
    ("color", "i"),          # palette index 0..9: which hue this player wears
    ("spawn-prot", "i"),     # ticks of post-spawn protection left (FFA; shown as a shimmer)
    ("last-damager", "i"),   # slot that last hurt this player, -1 none
    ("flash", "i"),          # ticks of damage-flash on the model
    ("step-phase", "f"),     # walk-cycle phase for the model
    ("net-slot", "i"),       # 1 = a remote human (position comes from snapshots, no local physics)
    ("fire-pending", "i"),   # ticks until the next shot cue is allowed
    # snapshot interpolation (remote humans): two buffered snapshots + their times
    ("s0x", "f"), ("s0y", "f"), ("s0z", "f"), ("s0yaw", "f"), ("s0pitch", "f"), ("s0t", "i"),
    ("s1x", "f"), ("s1y", "f"), ("s1z", "f"), ("s1yaw", "f"), ("s1pitch", "f"), ("s1t", "i"),
    ("rx", "f"), ("ry", "f"), ("rz", "f"), ("ryaw", "f"), ("rpitch", "f"),   # the pose we draw / hit-test
    ("net-flags", "i"),
    ("px", "f"), ("py", "f"), ("pz", "f"),   # position at the start of the last tick (render interpolation)
    ("pyaw", "f"),
    # bot brain
    ("bot-level", "i"), ("bot-target", "i"), ("bot-think", "i"),
    ("bot-aim-yaw", "f"), ("bot-aim-pitch", "f"), ("bot-yaw-vel", "f"), ("bot-pitch-vel", "f"),
    ("bot-strafe", "f"), ("bot-strafe-t", "i"),
    ("bot-goal-x", "f"), ("bot-goal-z", "f"), ("bot-goal-t", "i"),
    ("bot-stuck-t", "i"), ("bot-last-x", "f"), ("bot-last-z", "f"),
    ("bot-react", "i"), ("bot-seen-t", "i"), ("bot-last-seen-x", "f"), ("bot-last-seen-z", "f"),
    ("bot-wpn-t", "i"), ("bot-jump-t", "i"), ("bot-retreat", "i"), ("bot-burst", "i"),
    ("bot-heard", "i"), ("bot-skill", "f"),
]

PROJ = [("active", "i"), ("x", "f"), ("y", "f"), ("z", "f"), ("vx", "f"), ("vy", "f"), ("vz", "f"),
        ("owner", "i"), ("bounces", "i"), ("damage", "i"), ("ttl", "i")]

TRACER = [("ttl", "i"), ("x0", "f"), ("y0", "f"), ("z0", "f"), ("x1", "f"), ("y1", "f"), ("z1", "f"),
          ("kind", "i"), ("life", "i")]

PARTICLE = [("ttl", "i"), ("x", "f"), ("y", "f"), ("z", "f"), ("vx", "f"), ("vy", "f"), ("vz", "f"),
            ("color", "i"), ("size", "i")]


def emit(prefix, base_fn, fields, out):
    for idx, (name, ty) in enumerate(fields):
        t = "F32" if ty == "f" else "I32"
        get = "loadf32" if ty == "f" else "load32"
        put = "storef32!" if ty == "f" else "store32!"
        addr = "(+ (%s s) %d)" % (base_fn, idx * 4)
        out.append(("(defn k-%s-%s [] : I32 %d)" % (prefix, name, idx)) if prefix != "p" else ("(defn k-%s [] : I32 %d)" % (name, idx)))
        out.append("(defn %s-%s [(s : I32)] : %s (%s %s))" % (prefix, name, t, get, addr))
        out.append("(defn set-%s-%s! [(s : I32) (v : %s)] : Unit (%s %s v))" % (prefix, name, t, put, addr))


def main():
    assert len(PLAYER) <= 128, "player record is 512 bytes (128 words); has %d" % len(PLAYER)
    assert len(PROJ) <= 16 and len(TRACER) <= 16 and len(PARTICLE) <= 16
    out = [";; GENERATED by tools/sp_fields_gen.py -- do not edit. Field accessors for the shankpit-460 web engine's",
           ";; fixed-address records (players, projectiles, tracers, particles). See that script for the layout.", ""]
    emit("p", "pl-base", PLAYER, out)
    out.append("")
    emit("q", "pj-base", PROJ, out)
    out.append("")
    emit("t", "tr-base", TRACER, out)
    out.append("")
    emit("f", "pa-base", PARTICLE, out)
    out.append("")
    out.append("(defn player-words [] : I32 %d)" % len(PLAYER))
    print("\n".join(out))


main()
