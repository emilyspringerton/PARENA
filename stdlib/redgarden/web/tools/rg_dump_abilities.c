#include <stdio.h>
#include "simulation/arena_game.h"
#include "simulation/arena_ai_bridge.h"
int main(void){
 for(int h=0;h<ARENA_HERO_COUNT;h++){
  printf("H %d",h);
  for(int s=0;s<3;s++){const char*n=arena_ability_name((ArenaHeroID)h,s);printf("|%s",n?n:"");}
  printf("\n");
  for(int s=0;s<3;s++){const char*d=arena_ability_description((ArenaHeroID)h,s);printf("D %d %d|%s\n",h,s,d?d:"");}
 }
 return 0;}
