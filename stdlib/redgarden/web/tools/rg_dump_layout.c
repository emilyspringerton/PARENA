#include <stdio.h>
#include <stddef.h>
#include "simulation/arena_game.h"
#include "common/protocol.h"
#define OFF(t,f) printf("%s.%s=%zu\n",#t,#f,offsetof(t,f))
int main(void){
 printf("NetHeader=%zu MatchFound=%zu ArenaHeroSnapshot=%zu ArenaSnapshotMsg=%zu HeroesMsg=%zu\n",sizeof(NetHeader),sizeof(MatchFoundMsg),sizeof(ArenaHeroSnapshot),sizeof(ArenaSnapshotMsg),sizeof(ArenaSnapshotHeroesMsg));
 printf("sizes: MoveCmd=%zu CastCmd=%zu PickCmd=%zu AttackCmd=%zu ShopBuy=%zu ShopSell=%zu\n",sizeof(ArenaMoveCmd),sizeof(ArenaCastCmd),sizeof(ArenaPickCmd),sizeof(ArenaAttackCmd),sizeof(ArenaShopBuyCmd),sizeof(ArenaShopSellCmd));
 OFF(NetHeader,type);OFF(NetHeader,client_id);OFF(NetHeader,sequence);OFF(NetHeader,timestamp);OFF(NetHeader,entity_count);
 OFF(MatchFoundMsg,port);OFF(MatchFoundMsg,seed);OFF(MatchFoundMsg,mode);
 OFF(ArenaMoveCmd,target_x);OFF(ArenaMoveCmd,target_z);OFF(ArenaMoveCmd,unit_owner);
 OFF(ArenaCastCmd,slot);OFF(ArenaCastCmd,hover_target);OFF(ArenaCastCmd,has_ground_target);OFF(ArenaCastCmd,target_x);OFF(ArenaCastCmd,target_z);
 OFF(ArenaAttackCmd,target_owner);OFF(ArenaAttackCmd,commander_unit);
 #define H(f) OFF(ArenaHeroSnapshot,f)
 H(x);H(z);H(hp);H(max_hp);H(alive);H(hero_id);H(is_clone);H(clone_owner);H(cast_flash_slot);H(q_cooldown_ms);H(w_cooldown_ms);H(r_cooldown_ms);H(mp);H(attack_target);H(flow);H(flow_earned);H(xp);H(kills);H(deaths);H(equipped_item);H(w_active);H(silenced_ms);H(rooted_ms);H(stunned_ms);H(slowed_ms);H(r_zone_x);H(r_zone_z);H(r_active_ms);H(zone_radius_x10);H(casting_slot);H(cast_time_remaining_ms);H(cast_total_ms);H(blink_cooldown_ms);H(shield_hp);H(intangible_ms);H(duck_smoke_x);H(duck_smoke_ms);
 #define S(f) OFF(ArenaSnapshotMsg,f)
 S(count);S(winner);S(phase);S(picked);S(nodes);S(projectile_count);S(projectiles);S(creeps);S(towers);S(powerups);S(lane_creep_count);S(lane_creeps);S(resources);S(camp_minion_count);S(camp_minions);S(kings);S(obstacle_hp);
 OFF(ArenaSnapshotHeroesMsg,chunk_index);OFF(ArenaSnapshotHeroesMsg,total_count);OFF(ArenaSnapshotHeroesMsg,heroes);
 printf("sz node=%zu proj=%zu creep=%zu tower=%zu powerup=%zu lane=%zu campm=%zu king=%zu\n",sizeof(ArenaNodeSnapshot),sizeof(ArenaProjectileSnapshot),sizeof(ArenaCreepSnapshot),sizeof(ArenaTowerSnapshot),sizeof(ArenaPowerupSnapshot),sizeof(ArenaLaneCreepSnapshot),sizeof(ArenaCampMinionSnapshot),sizeof(ArenaKingSnapshot));
 OFF(ArenaNodeSnapshot,owner);OFF(ArenaNodeSnapshot,capturing_team);OFF(ArenaNodeSnapshot,capture_progress_ms);
 OFF(ArenaProjectileSnapshot,owner);OFF(ArenaProjectileSnapshot,hero_id);
 OFF(ArenaCreepSnapshot,hp);OFF(ArenaCreepSnapshot,alive);OFF(ArenaCreepSnapshot,flavor);
 OFF(ArenaLaneCreepSnapshot,hp);OFF(ArenaLaneCreepSnapshot,team);OFF(ArenaLaneCreepSnapshot,role);
 OFF(ArenaKingSnapshot,hp);OFF(ArenaKingSnapshot,alive);
 arena_obstacles_reset_layout();
 return 0;}
