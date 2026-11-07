static void test_entry_sentinels(const Vol *vol) {
 Context c={0}; c.city=calloc(1,sizeof *c.city); assert(c.city); c.city->vol=vol; city_activate(c.city);
 {
 ArmyHost h={0}; h.ctx=&c; h.failed=stopped;
 Army *a=army_new(&h); assert(a);
 c.city->load_failed=1;
 army_add_stats(a,0,(Group){0},0);
 assert(army_score_building(a,0,0,(Group){0},0,0,NULL,0,0,NULL,0,NULL)==0);
 assert(army_load_block(a,(Group){0})==0);
 assert(army_weapon_make(a,NULL,(Group){0})==0);
 army_weapon_tick(a,NULL,0,NULL,NULL,NULL,0,0,0);
 army_collide_add(a,NULL);
 army_impact(a,0);
 assert(army_spawn(a,NULL,NULL,0)==-1);
 assert(army_spawn_window(a,NULL,NULL)==-1);
 assert(army_drop(a,NULL,NULL)==-1);
 army_person_weapon(a,0,(Group){0},NULL,0);
 army_update(a,0,NULL,0,NULL,0);
 assert(army_hit(a,0,NULL,0,0,0)==0);
 army_grab(a,0,0,0);
 army_hold(a,0,NULL,0,0);
 army_throw(a,0,0,0,NULL,0);
 army_free(a); city_reset_load_failure(c.city);
 }
 {
 VehicleHost h={0}; h.ctx=&c; h.failed=stopped;
 Vehicles *a=vehicle_new(&h); assert(a);
 c.city->load_failed=1;
 assert(vehicle_load_traffic(a,0,(Group){0})==0);
 assert(vehicle_spawn(a,NULL,NULL,0,0)==-1);
 assert(vehicle_kind_of(a,NULL)==-1);
 assert(vehicle_level_start(a,0,NULL)==-1);
 vehicle_building_gone(a,0);
 vehicle_update(a,0,NULL,0,NULL,0);
 vehicle_shot(a,0,NULL);
 assert(vehicle_hit(a,0,NULL,0,0)==0);
 vehicle_collide_add(a,NULL);
 vehicle_grab(a,0,0,0);
 vehicle_hold(a,0,NULL,0,0);
 vehicle_throw(a,0,0,0,NULL,0);
 vehicle_free(a); city_reset_load_failure(c.city);
 }
 {
 RailHost h={0}; h.ctx=&c; h.failed=stopped;
 Rail *a=rail_new(&h); assert(a);
 c.city->load_failed=1;
 assert(rail_kind_of(a,NULL)==-1);
 assert(rail_spawn(a,NULL,NULL,0,(Group){0})==-1);
 assert(rail_load_block(a,(Group){0})==0);
 rail_update(a,0,NULL,0,NULL,0);
 assert(rail_hit(a,0,NULL,0,0)==0);
 rail_collide_add(a,NULL);
 rail_grab(a,0,0,0);
 rail_hold(a,0,NULL,0,0);
 rail_throw(a,0,0,0,NULL);
 rail_free(a); city_reset_load_failure(c.city);
 }
 {
 ParadeHost h={0}; h.ctx=&c; h.failed=stopped;
 Parade *a=parade_new(&h); assert(a);
 c.city->load_failed=1;
 parade_update(a,0,NULL,0,0);
 assert(parade_spawn(a,NULL,NULL,0)==-1);
 assert(parade_load_block(a,(Group){0})==0);
 assert(parade_hit(a,0,NULL,0,0)==0);
 parade_grab(a,0,0,0);
 parade_hold(a,0,NULL,0,0);
 parade_throw(a,0,0,0,0);
 parade_collide_add(a,NULL);
 parade_free(a); city_reset_load_failure(c.city);
 }
 {
 EdibleHost h={0}; h.ctx=&c; h.failed=stopped;
 Edibles *a=edible_new(&h); assert(a);
 c.city->load_failed=1;
 assert(edible_load_block(a,(Group){0})==0);
 assert(edible_add_building(a,0,(Group){0},(Group){0},0,0)==0);
 assert(edible_add_fixed(a,0,0,0,NULL)==-1);
 assert(edible_add_cryotube(a,0,0,0)==-1);
 edible_cell_broken(a,0,0,0,0,0);
 edible_floor_fell(a,0,0,0,0);
 edible_update(a,0);
 assert(edible_occupant(a,0,0,0)==0);
 assert(edible_hides(a,0,0,0)==0);
 assert(edible_grab(a,0,0,0,0,NULL,0,NULL)==0);
 assert(edible_monster_call(a,NULL,0,0,0,0,0,NULL)==-1);
 edible_free(a); city_reset_load_failure(c.city);
 }
 {
 BossHost h={0}; h.ctx=&c; h.failed=stopped;
 Boss *a=boss_new(&h); assert(a);
 c.city->load_failed=1;
 assert(boss_load_block(a,(Group){0})==0);
 assert(boss_load_waiting(a,(Group){0},0,0)==0);
 assert(boss_spawn(a,NULL,(Group){0},NULL)==-1);
 boss_update(a,0,NULL,0,NULL,0);
 assert(boss_hit(a,0,NULL,0,0,0)==0);
 boss_free(a); city_reset_load_failure(c.city);
 }
 {
 GameHost h={0}; h.ctx=&c; h.failed=stopped;
 GameConfig cfg={0};
 Game *a=game_new(&h,&cfg); assert(a);
 c.city->load_failed=1;
 assert(game_load_city(a,NULL)==-1);
 game_start(a);
 game_update(a,0,NULL);
 game_restart_shell(a);
 assert(game_block_str(a,NULL)==NULL);
 game_free(a); city_reset_load_failure(c.city);
 }
 {
 StreetHost h={0}; h.ctx=&c; h.failed=stopped;
 StreetObjs *a=streetobj_new(&h); assert(a);
 c.city->load_failed=1;
 assert(streetobj_add(a,(Group){0},(Group){0},NULL,0,0)==-1);
 assert(streetobj_add_attachment(a,0,(Group){0},NULL)==-1);
 streetobj_start(a,NULL,0);
 streetobj_update(a,0,NULL,0,NULL,0);
 assert(streetobj_hit(a,0,NULL,NULL,0,0,0)==0);
 streetobj_shot(a,0,NULL);
 assert(streetobj_touch(a,0,NULL,NULL,0)==0);
 assert(streetobj_take_impact(a,0)==0);
 assert(streetobj_impact(a,NULL,0)==0);
 assert(streetobj_force_damage(a,NULL,NULL)==-1);
 streetobj_pickup(a,0);
 streetobj_throw(a,0,0);
 streetobj_hit_ground(a,0);
 streetobj_grab(a,0,0);
 streetobj_hold(a,0,NULL,0,0);
 streetobj_fly(a,0,0,0,NULL);
 streetobj_free(a); city_reset_load_failure(c.city);
 }
 {
 People *a=people_new(NULL,NULL); assert(a);
 people_set_failure(a,stopped,&c);
 c.city->load_failed=1;
 assert(people_add(a,(Group){0},NULL,0)==-1);
 assert(people_add_window(a,(Group){0},NULL,0,0,0)==-1);
 assert(people_crowd(a,NULL,0,NULL)==0);
 people_add_swarm(a,(Group){0},NULL);
 { PeopleEaten z=people_hut_hit(a,0,NULL,NULL); assert(!z.health&&!z.energy&&!z.points&&!z.priest&&!z.fart&&!z.eat_fx&&!z.eat_node[0]); }
 assert(people_add_streaker(a,(Group){0},NULL,0)==-1);
 people_remove(a,0);
 people_collide_add(a,NULL);
 people_update(a,0,NULL,0);
 people_window_fall(a,0);
 people_grab(a,0,0);
 people_hold(a,0,NULL,0,0);
 { PeopleEaten z=people_eat(a,0,0); assert(!z.health&&!z.energy&&!z.points&&!z.priest&&!z.fart&&!z.eat_fx&&!z.eat_node[0]); }
 people_throw(a,0,0);
 assert(people_hit(a,0,NULL,0,0,NULL)==0);
 people_free(a); city_reset_load_failure(c.city);
 }
 city_activate(NULL);city_close(c.city);assert(!ledger_live&&!ledger_bytes);puts("all 97 public failure-entry return contracts: PASS; owners=0");
}
