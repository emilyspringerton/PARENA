/* tests/brawlpit_parity_ref.c -- the C side of the BRAWLPIT parity test.
 *
 * Runs BRAWLPIT's real engine (BRAWLPIT/packages/common/physics.h + packages/simulation/
 * local_game.h, unmodified) through a scripted match and writes a binary trace of the complete
 * simulation state after every tick. tests/brawlpit_parity_test.mjs replays the same inputs
 * through the PARENA wasm engine (stdlib/brawlpit, the .prn files) and requires the state to match word for
 * word, every tick: positions, velocities, damage, timers, shield, projectiles -- all of it.
 *
 *   gcc -O2 -ffp-contract=off -I<BRAWLPIT>/packages -o ref brawlpit_parity_ref.c -lm
 *   (cd <BRAWLPIT> && ref OUT SEED FRAMES NPLAYERS STAGE P0CHAR P1CHAR TWOHUMAN MODE)
 *
 * The only substitution is rand(): C's bot calls rand(); the wasm engine has no libc, so rand() is
 * mapped onto the same LCG bp-rand implements (stdlib/brawlpit/bp_sim.prn).
 *
 * Trace layout (little-endian uint32 words), one record per tick:
 *   12 input words: sx sy jump attack shield special for P0, then the same for P1
 *   8 players x 64 words (PlayerState fields in bp_fields.prn order; words 59..63 are web-only and
 *   written as 0), 16 turnips x 8 words, then match_over, tick number, and a re-init flag (a
 *   finished match is restarted immediately so long scripts keep producing fights).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <netinet/in.h>

static uint32_t bp_seed_state = 1;
static int bp_rand(void) {
    bp_seed_state = bp_seed_state * 1103515245u + 12345u;
    return (int)((bp_seed_state >> 16) & 32767u);
}
#define rand bp_rand

#include "common/protocol.h"
/* local_game.h pulls in ai_opponent.h (network + RL weights). The guard macro below makes that a
   no-op; the stub is the "no checkpoint loaded" state the web client is always in. */
#define BRAWLPIT_AI_OPPONENT_H
typedef struct { int loaded; } AiOpponentStub;
static AiOpponentStub g_ai_opponent;
static inline void ai_opponent_drive(PlayerState *a, PlayerState *b) { (void)a; (void)b; }
#include "simulation/local_game.h"

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static void dump_player(FILE *o, const PlayerState *p) {
    uint32_t w[64];
    memset(w, 0, sizeof w);
    int k = 0;
#define I(v) (w[k++] = (uint32_t)(int32_t)(v))
#define F(v) (w[k++] = f2u(v))
    I(p->id); I(p->character_id); I(p->active); I(p->is_bot);
    F(p->x); F(p->y); F(p->vx); F(p->vy);
    I(p->on_ground); I(p->facing);
    F(p->in_x); F(p->in_y);
    I(p->btn_jump); I(p->btn_jump_prev); I(p->btn_attack); I(p->btn_attack_prev);
    I(p->btn_shield); I(p->btn_shield_prev); I(p->btn_special); I(p->btn_special_prev);
    I(p->state); F(p->damage_percent); I(p->stocks); F(p->shield_health);
    I(p->shield_regen_timer); I(p->shield_stun_frames); I(p->shield_drop_timer);
    I(p->shield_windup_frames); I(p->shield_overpowered_frames);
    I(p->jumps_remaining); I(p->ground_platform_type); I(p->drop_through_timer);
    I(p->wavedash_frames); I(p->dash_cooldown); I(p->special_b_cooldown);
    I(p->umbrella_open); I(p->upb_frame); I(p->upb_landing_lag);
    I(p->parasol_rehit_timer); I(p->rosie_dash_frame);
    I(p->hitlag_frames); I(p->hit_flash_timer); I(p->hit_flash_multihit);
    I(p->hitstun_frames); I(p->attack_cooldown); I(p->attack_timer); I(p->parry_timer);
    I(p->smash_charge_timer); I(p->smash_active_timer); F(p->smash_charge_level);
    I(p->launch_delay_frames); F(p->pending_kb_x); F(p->pending_kb_y);
    I(p->smash_release_timer); I(p->smash_flash_timer);
    I(p->invuln_frames); I(p->respawn_timer); I(p->kills); I(p->deaths);
#undef I
#undef F
    fwrite(w, 4, 64, o);
}

static void dump_turnip(FILE *o, const Turnip *t) {
    uint32_t w[8] = { (uint32_t)t->active, f2u(t->x), f2u(t->y), f2u(t->vx), f2u(t->vy),
                      (uint32_t)t->owner_id, (uint32_t)t->ttl_frames, (uint32_t)t->style };
    fwrite(w, 4, 8, o);
}

/* xorshift32: the scenario generator (separate from the bot LCG above) */
static uint32_t gx = 2463534242u;
static uint32_t gnext(void) { gx ^= gx << 13; gx ^= gx >> 17; gx ^= gx << 5; return gx; }
static int gpct(int pct) { return (int)(gnext() % 100u) < pct; }

typedef struct { float sx, sy; int hold_j, hold_a, hold_s, hold_sp; int stick_t; } Script;

static void script_step(Script *s) {
    if (s->stick_t <= 0) {
        static const float xs[] = { 0, 0, 1, -1, 1, -1, 0.7f, -0.7f, 0.55f, -0.55f, 0.3f, 0.65f };
        static const float ys[] = { 0, 0, 0, 0, 1, -1, 0.55f, -0.55f, -0.8f, 0.8f, 0.6f, -0.7f };
        s->sx = xs[gnext() % (sizeof xs / sizeof xs[0])];
        s->sy = ys[gnext() % (sizeof ys / sizeof ys[0])];
        s->stick_t = 5 + (int)(gnext() % 28u);
    }
    s->stick_t--;
    if (s->hold_j > 0) s->hold_j--; else if (gpct(5)) s->hold_j = 1 + (int)(gnext() % 4u);
    if (s->hold_a > 0) s->hold_a--; else if (gpct(8)) s->hold_a = 1 + (int)(gnext() % 3u);
    if (s->hold_s > 0) s->hold_s--; else if (gpct(2)) s->hold_s = 4 + (int)(gnext() % 40u);
    if (s->hold_sp > 0) s->hold_sp--; else if (gpct(3)) s->hold_sp = 1 + (int)(gnext() % 55u);
}

/* steer: make a scripted human chase the nearest live opponent and swing at it, so the scripted
   matches reach the hit/shield/KO/launch branches instead of wandering around. State-aware on the
   C side only -- the wasm replay just consumes the recorded inputs. */
static void steer(Script *s, int self, int aggro_pct) {
    const PlayerState *me = &local_state.players[self];
    int target = -1;
    float best = 1e9f;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const PlayerState *o = &local_state.players[i];
        if (i == self || !o->active || o->state == STATE_DEAD) continue;
        float d = fabsf(o->x - me->x);
        if (d < best) { best = d; target = i; }
    }
    if (aggro_pct > 100) {
        /* special spam: stand still and fire the up/down special as often as it will go */
        static const float ys[] = { 1, -1, 0.8f, -0.8f };
        /* only from the ground: in the air an up-special is the parasol, which rides straight out
           of the top blast zone and ends the scenario's fight */
        if (me->on_ground && gpct(35)) { s->hold_sp = 1 + (int)(gnext() % 3u); s->sy = ys[gnext() % 4u]; s->sx = 0; s->stick_t = 2; }
        else if (!me->on_ground) { s->hold_sp = 0; s->sy = 0; }
        if (target >= 0 && gpct(20)) s->hold_a = 1 + (int)(gnext() % 3u);
        if (gpct(5)) s->hold_j = 2;
        return;
    }
    if (target < 0 || !gpct(aggro_pct)) return;
    const PlayerState *t = &local_state.players[target];
    float dx = t->x - me->x, dy = t->y - me->y;
    s->sx = dx > 1.0f ? 1.0f : (dx < -1.0f ? -1.0f : s->sx);
    if (fabsf(dx) < 6.0f && fabsf(dy) < 4.0f) {
        if (gpct(35)) s->hold_a = 1 + (int)(gnext() % 3u);
        if (gpct(14)) {
            /* a special with a still stick fires the character's up/down special (a moving stick
               would start a smash charge instead) */
            s->hold_sp = 1 + (int)(gnext() % 4u);
            static const float ys[] = { 1, -1, 0.8f, -0.8f, 0, 1, -1 };
            s->sy = ys[gnext() % 7u];
            s->sx = 0;
            s->stick_t = 3;
        }
        if (gpct(6)) s->hold_s = 6 + (int)(gnext() % 30u);
    }
    if (dy > 3.0f && gpct(25)) s->hold_j = 2;
    if (me->y < -2.0f && gpct(40)) { s->hold_j = 2; s->sx = me->x > 0 ? -1.0f : 1.0f; }
}

int main(int argc, char **argv) {
    if (argc < 12) {
        fprintf(stderr, "usage: %s OUT SEED FRAMES NPLAYERS STAGE P0CHAR P1CHAR TWOHUMAN MODE AGGRO P1MODE\n", argv[0]);
        return 2;
    }
    FILE *o = fopen(argv[1], "wb");
    if (!o) return 2;
    uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 10);
    int frames = atoi(argv[3]), nplayers = atoi(argv[4]), stage = atoi(argv[5]);
    int c0 = atoi(argv[6]), c1 = atoi(argv[7]), two_human = atoi(argv[8]), mode = atoi(argv[9]);
    int aggro = atoi(argv[10]), p1mode = atoi(argv[11]); /* p1mode 1 = idle dummy human */
    gx = 2463534242u ^ (seed * 2654435761u);
    if (gx == 0) gx = 1;
    bp_seed_state = seed;

    local_init_match(nplayers, mode, stage, (CharacterId)c0, (CharacterId)c1);
    if (two_human && nplayers > 1) local_state.players[1].is_bot = 0;

    Script s0 = {0}, s1 = {0};
    for (int f = 0; f < frames; f++) {
        script_step(&s0);
        script_step(&s1);
        if (aggro) { steer(&s0, 0, aggro); if (two_human && nplayers > 1 && !p1mode) steer(&s1, 1, aggro); }
        if (p1mode) { s1.sx = 0; s1.sy = 0; s1.hold_j = s1.hold_a = s1.hold_s = s1.hold_sp = 0; }
        uint32_t in[12] = { f2u(s0.sx), f2u(s0.sy), (uint32_t)(s0.hold_j > 0), (uint32_t)(s0.hold_a > 0),
                            (uint32_t)(s0.hold_s > 0), (uint32_t)(s0.hold_sp > 0),
                            f2u(s1.sx), f2u(s1.sy), (uint32_t)(s1.hold_j > 0), (uint32_t)(s1.hold_a > 0),
                            (uint32_t)(s1.hold_s > 0), (uint32_t)(s1.hold_sp > 0) };
        /* local_update ignores input once the match is over; do the same for the second human */
        if (two_human && nplayers > 1 && !local_state.match_over)
            local_set_player_input(1, s1.sx, s1.sy, s1.hold_j > 0, s1.hold_a > 0, s1.hold_s > 0, s1.hold_sp > 0);
        local_update(s0.sx, s0.sy, s0.hold_j > 0, s0.hold_a > 0, s0.hold_s > 0, s0.hold_sp > 0, NULL, 0);
        fwrite(in, 4, 12, o);
        for (int i = 0; i < MAX_CLIENTS; i++) dump_player(o, &local_state.players[i]);
        for (int i = 0; i < MAX_TURNIPS; i++) dump_turnip(o, &local_state.turnips[i]);
        /* a finished match is restarted at once so the scenario keeps producing fights; the third
           tail word tells the replay to re-run its own init at the same point. */
        uint32_t reinit = local_state.match_over ? 1u : 0u;
        uint32_t tail[3] = { (uint32_t)local_state.match_over, (uint32_t)(f + 1), reinit };
        fwrite(tail, 4, 3, o);
        if (reinit) {
            local_init_match(nplayers, mode, stage, (CharacterId)c0, (CharacterId)c1);
            if (two_human && nplayers > 1) local_state.players[1].is_bot = 0;
        }
    }
    fclose(o);
    return 0;
}
