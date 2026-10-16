/* tests/shankpit460_parity_ref.c -- the C side of the shankpit-460 parity test.
 *
 * Links shankpit-460's REAL physics.h / net_sim.h / local_game.h (the online movement path the live server
 * runs) and executes a tiny line-oriented script from stdin, printing the players' state on `dump`. The
 * Node test (shankpit460_parity_test.mjs) feeds the same script to the PARENA wasm engine and compares.
 *
 *   reset                          fresh world, v0_shankpit map loaded
 *   seed <n>                       rand() seed (both sides use glibc's TYPE_0 LCG)
 *   spawn <p> <x> <y> <z>          activate player p at that spot with a full loadout
 *   cmd <p> <fwd> <str> <yaw> <pitch> <buttons> <weapon>
 *   step <n>                       n server ticks (movement tick or dead-countdown, then projectiles)
 *   dump                           one line per active player
 *
 * Build:  gcc -O2 -ffp-contract=off -I<shankpit-460>/packages -o ref shankpit460_parity_ref.c -lm
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/protocol.h"
#include "common/physics.h"
#include "common/shared_movement.h"
#include "common/net_sim.h"
#include "simulation/local_game.h"

/* glibc TYPE_0 LCG, output bits 16..30: the generator stdlib/shankpit460/sp_mem.prn's sp-rand is */
static unsigned int rng_state = 1;
void srand(unsigned int s) { rng_state = s; }
int rand(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)((rng_state >> 16) & 32767u);
}

static unsigned int now_ms = 0;

int main(int argc, char **argv) {
    const char *map = argc > 1 ? argv[1] : "maps/v0_shankpit.map";
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32];
        if (sscanf(line, "%31s", cmd) != 1) continue;
        if (!strcmp(cmd, "reset")) {
            memset(&local_state, 0, sizeof local_state);
            map_loader_active = 0;
            if (!map_load_file(map)) { fprintf(stderr, "cannot load %s\n", map); return 2; }
            local_state.scene_id = SCENE_STADIUM;
            local_state.pending_scene = -1;
            for (int i = 0; i < MAX_CLIENTS; i++) local_state.players[i].id = i;
            phys_set_scene(SCENE_STADIUM);
            now_ms = 0; rng_state = 1;
        } else if (!strcmp(cmd, "seed")) {
            unsigned int s; sscanf(line, "%*s %u", &s); rng_state = s;
        } else if (!strcmp(cmd, "spawn")) {
            int p; float x, y, z;
            sscanf(line, "%*s %d %f %f %f", &p, &x, &y, &z);
            PlayerState *ps = &local_state.players[p];
            ps->id = p; ps->active = 1; ps->scene_id = SCENE_STADIUM;
            phys_respawn(ps, 0);
            ps->x = x; ps->y = y; ps->z = z;
            ps->vx = ps->vy = ps->vz = 0.0f;
        } else if (!strcmp(cmd, "cmd")) {
            int p, buttons, weapon; float fwd, str, yaw, pitch;
            sscanf(line, "%*s %d %f %f %f %f %d %d", &p, &fwd, &str, &yaw, &pitch, &buttons, &weapon);
            UserCmd uc; memset(&uc, 0, sizeof uc);
            uc.fwd = fwd; uc.str = str; uc.yaw = yaw; uc.pitch = pitch;
            uc.buttons = (unsigned)buttons; uc.weapon_idx = weapon;
            shankpit_apply_usercmd_inputs(&local_state.players[p], &uc);
        } else if (!strcmp(cmd, "step")) {
            int n; sscanf(line, "%*s %d", &n);
            for (int t = 0; t < n; t++) {
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    PlayerState *p = &local_state.players[i];
                    if (!p->active) continue;
                    if (p->state != STATE_DEAD) { phys_set_scene(p->scene_id); shankpit_simulate_movement_tick(p, now_ms); }
                    else update_entity(p, SHANKPIT_NET_FIXED_DT, NULL, now_ms);
                }
                update_projectiles(now_ms);
                now_ms += 16;
                local_state.server_tick++;
            }
        } else if (!strcmp(cmd, "dump")) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                PlayerState *p = &local_state.players[i];
                if (!p->active) continue;
                printf("P %d pos %.9g %.9g %.9g vel %.9g %.9g %.9g ang %.9g %.9g hp %d sh %d st %d k %d d %d wpn %d ammo %d %d %d %d %d %d rt %d ac %d dash %d storm %d abc %d\n",
                       i, p->x, p->y, p->z, p->vx, p->vy, p->vz, p->yaw, p->pitch, p->health, p->shield, p->state,
                       p->kills, p->deaths, p->current_weapon,
                       p->ammo[0], p->ammo[1], p->ammo[2], p->ammo[3], p->ammo[4], p->ammo[5],
                       p->reload_timer, p->attack_cooldown, p->dash_timer, p->storm_charges, p->ability_cooldown);
            }
            printf("END\n");
            fflush(stdout);
        }
    }
    return 0;
}
