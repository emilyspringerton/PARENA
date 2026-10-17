#include <stdio.h>
#include "simulation/arena_game.h"
int main(void){
 arena_init_teams(); arena_towers_reset();
 printf("HALF %f\n",ARENA_HALF_EXTENT);
 for(int i=0;i<ARENA_OBSTACLE_COUNT;i++){ArenaObstacle*o=&arena_state.obstacles[i];printf("OBST %d %.3f %.3f %.3f %d\n",i,o->x,o->z,o->radius,(int)o->kind);}
 for(int i=0;i<ARENA_NODE_COUNT;i++)printf("NODE %d %.3f %.3f\n",i,arena_state.nodes[i].x,arena_state.nodes[i].z);
 for(int i=0;i<ARENA_NODE_COUNT;i++)printf("TOWER %d %.3f %.3f\n",i,arena_state.towers[i].x,arena_state.towers[i].z);
 for(int i=0;i<ARENA_FOUNTAIN_COUNT;i++){float x,z;arena_fountain_position(i,&x,&z);printf("FOUNT %d %.3f %.3f\n",i,x,z);}
 for(int t=0;t<2;t++){float x,z;arena_shop_position(t,&x,&z);printf("SHOP %d %.3f %.3f\n",t,x,z);}
 for(int i=0;i<ARENA_CAMP_COUNT;i++){float x,z;arena_camp_position(i,&x,&z);printf("CAMP %d %.3f %.3f\n",i,x,z);}
 for(int i=0;i<ARENA_MAX_HEROES;i++){ArenaHero*h=&arena_state.heroes[i];printf("HERO %d team=%d %.2f %.2f hp=%d\n",i,h->team,h->x,h->z,h->max_hp);}
 for(int i=0;i<ARENA_ITEM_COUNT;i++){const ArenaItemDef*d=&ARENA_ITEMS[i];printf("ITEM %d|%s|%d|%d|ad%d hp%d mp%d ar%d ms%.1f\n",i,d->name,(int)d->slot,d->cost,d->bonus_ad,d->bonus_max_hp,d->bonus_max_mp,d->bonus_armor,d->bonus_move_speed);}
 printf("POWERUP\n"); for(int i=0;i<ARENA_POWERUP_COUNT;i++)printf("PU %d %.2f %.2f\n",i,arena_state.powerups[i].x,arena_state.powerups[i].z);
 return 0;}
