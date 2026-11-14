# Prototype, game side: how the per-frame work is done

Read from `RampageD.xbe` (debug build, October 2005) through the Ghidra dump with `tools/re_query.py`.
Addresses are virtual addresses in `RampageD.xbe`. `Class::vfN` is slot N of that class's virtual table.
The decompiler often drops the `this` argument of member calls, so some objects are identified from context;
where that matters the text says "inferred". Nothing here was run; it is all static reading.

## Summary

1. Properties are looked up by string (case-insensitive compare, linear under 10 entries, binary search above,
   parent followed by pointer), but only when an object or its type descriptor is created. Per-step code reads
   plain struct members. The only per-step paths that reach a property getter are spawn events.
2. The world steps at a fixed 30 Hz (25 Hz PAL). One accumulator drives everything; at most two steps run per
   rendered frame and the leftover is clamped to one step. Effects and managers step inside the same world step.
3. Objects are updated from an "updating" list. An object's update returns flags; one flag takes it off the list
   (asleep) until an event puts it back. An idle building is not updated at all.
4. There is no grid or octree for object collision. Objects register once (add at spawn, remove at delete) and
   queries walk the object list with flag masks and a bounding-sphere reject. It is cheap because almost nothing
   queries: each monster does one sphere query per step and reuses the result, a projectile does one ray per
   step, thrown physics objects test only the ground plane in their sub-steps.
5. Names are resolved at load or setup: bones to indices, moves to pointers, script members to an index, script
   event handlers to an integer id, effect definitions to a held handle. By-name effect and resource lookups
   that remain go through a 97-bucket hash table.
6. A building is a grid of cells per face and floor with dirty bits per vertex stream. A damage event flips cell
   flags and dirty bits; the streams are refilled only where dirty, from the first dirty floor up, into buffers
   that already exist. A collapse is one progress number per building.
7. World objects come from four fixed pools (128 + 128 + 16 + 4 objects in total); nothing is allocated in play.
8. Spawning is rationed: traffic releases at most one vehicle per step, window occupants one per 0.75 s, effects
   are refused by priority above a live-count limit (lower limits with two or more players).
9. Heavy render-side rebuilds (building lighting, shadow shapes, ground shadow) go through a work queue that
   grants one job per rendered frame, with a five-frame starvation limit.
10. Caveat: this build is an early slice (16 buildings in two rows, people as window sprites, vehicles in
    lanes). Its patterns transfer; its small counts do not.

## 1. Properties (prop_set)

Mechanism:

- A property set is `{parent pointer +0, name +4, count +0xc, records +0x10}`; a record is 12 bytes: type byte
  (1 float, 2 string), name pointer +4, value +8.
- `FUN_001ed230` finds a name in one set: fewer than 10 records is a linear `_stricmp` scan, otherwise a binary
  search (`FUN_001ef6d0`) over sorted names. No hashing and no interned ids: names are compared as strings.
- `FUN_00014810` is find-with-inheritance: on a miss it calls itself on the parent pointer (`*this`). The chain
  is walked at each lookup; it is not flattened.
- `ParentPropertySet` is resolved once when a book is loaded: `FUN_001ed920` splits `"<set>:<book>"`, finds the
  book (`FUN_001e94b0` / `FUN_001e9960`), finds the set (`FUN_00014950`: binary search `FUN_001eeca0` ->
  `FUN_001efe20`, comparator `FUN_001f0050` = `_stricmp`) and stores the pointer.
- `FUN_000146e0` is the front door (handles `.a.b.c` paths into nested sets); the typed getters sit on it:
  `FUN_00017f40` float with default, `FUN_00017ee0` int with default, `FUN_00014680` string, `FUN_00025330`
  float pointer, `FUN_000252d0`, `FUN_00025390`, `FUN_000488b0`.
- The property names in engine code are a table of global string pointers (`PTR_s_CollisionRadius_005eb8c8`,
  `PTR_s_Restitution_005eb8e0`, `PTR_s_SlidingFriction_005eb8e4`, Mass at `005eb8d8`), still plain strings.
- `pwk_string_table.h` (`FUN_000516d0`) is something else: a bundle file's string table, index to string
  (`table + table[index + 1]`), used by loaders to turn stored indices into names at load.

Who calls the getters (about 100 functions in total), by kind:

- Type descriptors, read once per type: `WDesc::vf0` 0x1d5660 (StartAmmo, FiringOffset, RefireDelay, Gravity,
  Velocity, MaxFiringRange, DamageType, WeaponNode, three effects), `WSDesc::vf0` 0x4dbf0 and `WBDesc::vf0`
  0x1d71c0 (CollisionRadius, Duration, DamagePerSec...), `WPDesc::vf0` 0x1d6be0, `RPDesc::vf0` 0xa2c60,
  `RBDesc::vf0` 0xa2e00, `WWeaponCluster::vf0` 0x1e1090.
- Object initialisation (`vf5` of world objects), once per object: `WorldObject::vf5` 0x1d93e0 (XForm,
  Location, ZRotation, Name, TemplateName), `WSimpleObject::vf5` 0x1d85f0 (Shape, UseAlphaRender, RenderRadius,
  AutoCollisionBox, CollisionRadius, CollisionBox, turned into the object's collision shape there),
  `Building::vf5` 0x88aa0, `Actor::vf5` 0xab750, `AttackObject::vf5` 0xb50d0.
- Mover initialisation, once per mover: `WMover::vf7` 0x1d2790 stores Gravity (+0x10), Mass (+0x14),
  Restitution (+0x24), SlidingFriction (+0x28) with defaults 9.81 / 1.0 / 0.8 / 0.4; also
  `SpherePhysicsMover::vf14` 0x1d3640, `AttackMover::vf14` 0xb3a10, `HelicopterMover::vf14` 0xb7a50.
- Managers and level setup: `FUN_000b0130` (traffic), `FUN_000afc50`, `FUN_000ba8d0` / `FUN_000bac60`
  (occupants), `RWorld::vf9` 0xa38d0, `RSceneryMgr::vf0` 0x308d0, `RCameraMgr::vf7` 0x255f0, building state
  setup `FUN_00099770`, `FUN_0009a240`.

Check that nothing reads properties per step: a walk of the direct call graph from the per-step functions (the
world update list `WorldBase::vf7`, `RWorld::vf7`, every object `vf4`, every mover `vf12`; 1,362 functions
reached) hits a getter only through

- `FUN_000b0450` (AI manager step): inside the branch that spawns an object;
- `FUN_000bbd10` (occupant manager step): `DefaultAnim` of an actor it has just spawned;
- `Building::vf4` -> `FUN_00088600` -> `FUN_00093550` / `FUN_0008a960`: building state changes (collapse end).

Virtual calls are not followed by that walk, so this is strong evidence, not proof.

An object keeps two set pointers, instance (+0x80) and template (+0x84, found from `TemplateName` by
`FUN_001c5ab0`); `FUN_001d92f0` looks in the instance first, then the template.

## 2. The world update

Timing (`RWorldPackage::vf5` 0xa5480, profile label "DrawAndUpdate"):

- Step length is `world+4`, set in `RWorld::vf0` 0xa37a0: 1/30 s, or 1/25 s when the value from `FUN_000a4ab0`
  is under 51 (the display refresh rate, inferred: PAL). (Debug flag QuarterSpeed makes it a quarter.)
- An accumulator at `package+0x310` takes the real frame time (`FUN_00189990`). Then:
  - less than one step: render only, `FUN_001dddd0(update=0, render=1)`;
  - one step: subtract, update and render `(1,1)`;
  - two or more: subtract twice, `(1,0)` then `(1,1)`, and clamp what is left to at most one step.
  So never more than two world steps per rendered frame; beyond that the game runs slow rather than catching up.
  The debug flag `ForceNoInterleave` (0x5f5e35) forces one update per render.
- World time is `tick * step` (`WorldBase::vf8` 0x1c63a0), not a sum of frame times.

One step (`RWorld::vf12` 0xa4360, label "Update"; `WorldBase::vf12` 0x1c6160), in order:

1. "MonsterCommands": `FUN_000a4160` hands each monster its controller input (`Monster::vf25`).
2. `vf6` pre-update (0x1c61c0 / 0xa3c70: pause state, debug tools).
3. `FUN_000f5e20`: steps a manager's object list once with the same step (it sits with the effects manager code
   at 0xf5f70; identification by neighbourhood, uncertain).
4. `WorldBase::vf7` 0x1c6280: walk the updating list (`FUN_000a4b90` first, `FUN_000a4b60` next), call each
   object's `vf4`, act on the returned flags: 4 = take off the updating list (`FUN_001c6f80`, clears flag
   0x200), 8 = remove from the world, 0x10 = delete.
5. `RWorld::vf7` 0xa40b0 after the objects: per-monster collision `FUN_00065050`, `FUN_000a6fa0` (per-player
   indicators), occupant manager `FUN_000bbd10(dt)`, AI manager `FUN_000b0450(dt)`, `FUN_00085ee0`.
6. `WorldBase::vf8`: tick counter and world time.

Everything above runs once per world step with the same fixed dt. There are no per-subsystem catch-up loops.

Sleeping:

- `Building::vf4` 0x87cd0 starts with its return value at 4 (sleep) and only changes it to "keep updating" when
  the building is collapsing, has timed cell animations running, has effects running, or is in a state above 2.
- Wake-up is `FUN_0008c7d0` (asserts not already listed, appends to the updating list, sets flag 0x200). Callers:
  `Building::vf14` 0x89560 (damage), `FUN_00086d10`, `FUN_00087b60`, `FUN_0008a330`, `FUN_0008a470`,
  `FUN_0008a900`, `FUN_0008a960`, `FUN_00093200`, `FUN_00093600` (building effects), and the generic
  `WorldObject::vf23` 0x1db0f0.
- `RSimpleObject::vf4` 0x4cd90 and `Actor::vf4` 0xabf00 return 1 OR-ed with what their script handlers return,
  so a script can put its object to sleep. `Actor::vf4` also stops calling the script's per-step handler once
  that handler has returned flag 4 (`actor+0x100`).

Not found: any distance or off-screen test in the update loop, and any staggering of objects across steps.
`WorldBase::vf7`, `Actor::vf4` and `RSimpleObject::vf4` contain none. Awake objects update every step.

RWorkQueue (render side, listed here because the task asked): `FUN_0004c590(queue, item)` ->
`FUN_0004c630(queue, item, maxWait)` with maxWait = 5 (`DAT_0053aea0`). An item asking for the first time is
appended with the current frame number (`FUN_0004c5f0`). One item is granted per rendered frame; others are
refused unless they have waited 5 frames or are marked urgent. `FUN_000a7af0` (start of
`RWorldRenderer::vf12` 0xa66f0) advances the scheduled item, `FUN_000a7b70` (end) bumps the frame counter. Debug
flag DisableWorkQueue (0x5f5e2d) grants everything. What is queued: building lighting `FUN_00086830` ->
`FUN_0009d9c0` ("ApplyLighting") and `FUN_0009de00`; building shadow shape rebuild `FUN_00097c40`; shadow vertex
refresh `FUN_00097d50`; ground shadow `FUN_0004b750` ("RenderGroundShadow"). No game-logic work is queued.

## 3. Collision

Registry (`SpatialDataMgr`, table 0x501304; the game's `RGridMgr` derives from it; global `_DAT_005f7ff4`):

- `vf9` 0x1cef30 appends `{object, id}` to an array; `vf10` 0x1cefd0 removes it. Called from
  `WorldBase::vf13` 0x1c6410 (object enters the world) and `WorldBase::vf14` 0x1c64f0 (leaves). Nothing is
  rebuilt per step.
- Queries walk the world's list of all objects (`FUN_00085090` / `FUN_00085060`) and keep those that pass an
  include mask and an exclude mask on the object flags and have a collision shape (`FUN_00048a90`):
  - `vf5` 0x1cf320 ray: ground first (scenery manager `vf7`), then each object's shape (`FUN_001d5050`), nearest
    hit, optional any-hit early out;
  - `vf8` 0x1cf520 sphere overlap: centre distance against sum of radii, fills a caller array, asserts "Limit of
    %d overlapping objects has been reached";
  - `vf7` 0x1cf680 box overlap; `vf11`-`vf16` 0x1cf960... are iterator forms of the same walk.
- The object count is bounded by the pools (section 7), so the walk is at most 276 objects.

The ground is a plane: `SceneryMgr::vf7` 0x1d05a0 (ray against z = 0), `SceneryMgr::vf9` 0x1d0660 (sphere
against z = 0).

`Octree_tem.h` is used only by cloth setup (`FUN_0021fcc0` <- `FUN_002164b0`, cloth.cpp). It has no part in
world collision.

`RGridMgr` (RGridMgr.cpp) is the building table, not an object grid:

- `FUN_0002c570` (from `Building::vf6`) registers a building: up to 16 in total, in two rows of up to 8.
- `FUN_0002c6d0` works out, per building and side, whether a neighbour covers it; cached behind a valid flag
  (`+0xa8`) that is cleared when the set changes.
- `FUN_0002d040`, `FUN_0002d680`, `FUN_0002daa0`, `FUN_0002df00`, `FUN_0002e0f0` are the monster's questions
  (nearest climbable face left/right/front/top, building at a point); each loops the 16 entries with box
  arithmetic. Callers are monster code (`FUN_0006fef0`, `FUN_000699e0`, `FUN_00069a80`, `FUN_00070910`...).
- A hit point is turned into face / column / floor by arithmetic in `FUN_0008a3f0`; there is no building mesh
  collision.

Who queries objects:

- Monsters: `FUN_00065050`, once per monster per step after all objects moved. One `vf8` sphere query (radius
  35, up to 64 results) and that one list is passed to the handlers `FUN_00064f50` and `FUN_000654b0`.
- Projectiles: `Projectile::vf4` 0x1dc470 does one ray per step along its travel (a swept test by ray).
- Beam weapons: `WBeamWeapon::vf29` 0x1d75c0, one ray. Radial effects: `WorldObject::vf16` 0x1d97b0, one sphere
  query per event.
- `AttackMover::vf12` 0xb3c80 makes no spatial query; it loops a short list (the monsters, inferred).
- `LineMover::vf12` 0xb93c0 (lane vehicles) makes no spatial query.

Thrown physics objects (`SpherePhysicsMover::vf12` 0x1d2f20):

- Speed is clamped to `0.75 * radius * 50 / dt`; the step is cut into pieces of `min(0.75 * radius / speed,
  time left)`, so at most 50 pieces.
- Each piece integrates, then tests the sphere against the ground plane only (`_DAT_005f9364` `vf9`), applies
  the bounce (`FUN_001d28a0`, Restitution / SlidingFriction from the mover's members) and tells the owner.
- No object query happens inside the pieces.

## 4. Names

- Monster bones: `FUN_00059360` (MonsterXFormTree.cpp, called once from monster setup `FUN_0005f270`) looks up
  "Bip01 Head", "Bip01 Tail", "Bip01 R Foot", "Lift_Node", "Bip01 Spine1"... and keeps the node indices in
  members. The tree is a fixed 68-node instance.
- Weapon node: `WDesc::vf0` 0x1d5660 turns `WeaponNode` into an index (tree `vf19`) when the descriptor loads.
- Monster data: `FUN_0007c2b0` (MonsterData.cpp) resolves its 18 string-table entries (skeleton, animations,
  effects, sounds) once at load.
- Moves: `FUN_0006d0c0` (name -> move pointer through a map, `FUN_00073660`) has four callers, all setup or
  script entry: monster setup `FUN_00072120`, transition goal setup `FUN_00077c80` (three moves kept in
  members +0x70..+0x78), `FUN_000778e0` (PlayMoveGoal keeps the pointer at +0x20), script call `FUN_00036520`.
  This build has no move graph with named conditions; there is nothing to compare with the port's condition
  dispatch directly.
- Script calls: the `Type...Interface::vf7` functions (e.g. 0x1bed60, 0x3f430) are `strcmp` chains from member
  name to descriptor, but they run when a script is compiled: `PMemberAccessNode::vf1` 0x252560 stores the
  descriptor and member index in the node, `PFunctionCallNode::vf1` 0x2529c0 stores the function. At run time
  `PMemberAccessNode::vf10` 0x2526e0 / `PFunctionCallNode::vf10` 0x252e50 call through the stored index/pointer.
- Script events: the engine raises events by integer id (`FUN_001d1520(id)`, `FUN_001d1800(id, arg)`; 3 = per
  step, 0xc = damage, 0xd = timer, 0xe = mover event) and fetches the handler from a per-type table by that id
  (`FUN_00048e30`). The table being filled when the script type loads is inferred, not traced.
- Effects: an owner holds a counted handle to the effect definition (`FUN_00080e60`), filled at descriptor load
  from `FUN_001e8b90(name)`. By-name spawning still exists for scripts and building effects (`FUN_001c8020`,
  `FUN_00093200` "BuildingFxMgr::startEffect"): `FUN_001e8b90` hashes the lowercased name (16-bit,
  `h = h * 131 + c`, `FUN_001d2630`) and walks one of 97 buckets comparing hash, type, then `_stricmp`
  (`FUN_001d20c0`). The same cache serves every resource type.
- Sounds: scripts have a `SoundEffect` variable type (`ScriptSoundEffect`, table 0x4cd888) and a `loadSounds`
  call; `playSound` takes the variable. So a sound is bound once and played by handle (inferred from the types;
  the play path was not traced).
- Animations on actors: the current animation is an index (`actor+0xe8`) into the actor's animation array
  (`Actor::vf4` 0xabf00).

## 5. Buildings

Data (BuildingState.cpp, BuildingFloor.cpp; offsets in the state object):

- `+0x10` columns, `+0x18` floors still standing, `+0xc` floor records; `+0x44/+0x48[i]` maps standing floor i
  to its floor record, so removing a floor shifts indices, not data.
- A floor record (`FUN_0008b880(i)`): flags `+4` (2 destroyed, 4 collapsing), dirty mask `+8`, current level
  `+0x10`, cells `+0x2c` (16 bytes each, indexed `face * width + column`, state bits in the ushort at +0xe),
  and per stream its vertex offset (`+0x34 + kind * 0xc + face * 4`).
- Vertex streams per building: 3 kinds ("Window", "Shell", "Crack") x 3 faces (left, right, front). One dirty
  bit each in the building mask `+0x1e4`, mirrored per floor.
- Collapse: `+0x1cc` = floor being crushed (0x7fffffff = none, 0 = last sink into the ground), `+0x1d0` =
  progress, advanced by `dt * rate(+0xd8)`; `+0x38` = current height.
- Sizes used in the arithmetic: a cell is 5.0 wide, a floor 3.0 high.

Per damage event (`Building::vf14` 0x89560):

1. Hit point -> face, column, floor (`FUN_0008a3f0`).
2. `FUN_0009f230` (addVertRadialDamage) raises damage on grid corners around the hit, searching rings of
   1 / 8 / 12 / 16 offsets; `FUN_00089ea0` handles the cell itself; `FUN_0009c840` clears cell bits and sets the
   dirty bits of that floor and of the building for the affected face.
3. The building is put back on the updating list if it was asleep (`FUN_0008c7d0`).
4. The script's damage handler runs (`FUN_001d1300(0xc, ...)`).

Per step while something is happening (`Building::vf4` 0x87cd0):

- Collapse progress: `FUN_000991c0` -> `FUN_00099300` (a floor) or `FUN_00099220` (final sink). A floor collapse
  marks the two floors involved and the building fully dirty each step; at progress 1 the floor record is
  flagged destroyed, the index map shifts, the floor count drops. The final sink only lowers the height.
- While collapsing, the collision box is refreshed (`FUN_001d46a0`) and the shadow manager told (`FUN_0004ba40`).
- Timed cell animations (list `FUN_0008bc10`) advance; building effects update (`FUN_00092df0`).
- A small state machine at `building+0x150` (states 2..6: `FUN_000875c0`, `FUN_00088210`, `FUN_00088160`,
  `FUN_00088600`).
- When none of that applies the function returns "sleep".

Per rendered frame (`FUN_0009b100` "UpdateStreams", called per kind and face from `FUN_0009afc0`):

- If the building's dirty bit for that stream is clear: nothing.
- Otherwise find the first dirty floor, lock the existing vertex array at that floor's stored offset
  (`FUN_0009f9b0`), re-emit that floor and every floor above it with the emit function for kind/face
  (table `PTR_FUN_004c2f00`: the "EL_Window", "ER_Shell", "EF_WindowNight"... functions 0x8e300-0x90530), store
  the new offsets, unlock (`FUN_0009fab0`). No buffer is freed or allocated.
- Lighting and shadow rebuilds go through the work queue (section 2).

## 6. People and AI

What exists in this build:

- AI manager (AIManager.cpp, `FUN_000b0450` per step, setup `FUN_000b0130`): lane traffic from the level's
  `TrafficDef` (MaxVehicles default 7, MinReleaseTime, a list of `VehicleDef` with MaxCount and Frequency). At
  start each lane is filled. After that each lane has two timers; when one expires it is reset to 2..5 s
  (2 + rand * 3), one vehicle is spawned (`FUN_000b0d60`: weighted pick, object from the pool, a LineMover
  given start, end and speed) and the function returns: at most one traffic spawn per step. It also releases
  scripted delayed spawns and the one-off "MonsterUpgrade" pickup.
- Occupant manager (OccupantManager.cpp, `FUN_000bbd10` per step, setup `FUN_000ba8d0` / `FUN_000bac60`):
  every 0.75 s it picks one free window slot at random and spawns one `Actor` there. People in this build are
  window occupants drawn as animated bitmaps (`ActorAnimBitmap`) or small shapes (`ActorAnimShape`).
- Per actor per step (`Actor::vf4` 0xabf00): mover step, current animation advance, optional timer event,
  optional script step. Every awake actor, every step; no stagger, no distance test.
- Both managers and the building effect starters are skipped entirely when the debug setting `HighFrameRate`
  ("Drop stuff to improve framerate", 0x5f5e2c) is on.

How many: bounded by the pools. Actors (0x104 bytes), simple objects, attack objects, projectiles, weapons,
cloth and water all share the 128-slot pool of 512-byte objects; movers, triggers and follow effects share the
128-slot pool of 256-byte objects. The final game's street crowds, army units and rail are not in this build,
so it gives no evidence for how those were budgeted.

## 7. Other deliberate budgeting

- Object pools (`FUN_001c9b80`, TypedObjectBase.cpp; table for the game world at 0x4c3f5c): 128 x 256 bytes,
  128 x 512, 16 x 2176, 4 x 51072, one allocation at world start. A type goes to the smallest size class that
  fits (types registered by `FUN_001db330`: Monster 0xc768, Building 0x80c, Actor 0x104, RSimpleObject 0x110,
  AttackObject 0x184, movers 0x3c-0xac...). `FUN_001ca340` hands out an object or reports "Exhausted supply of
  size %i for type %s objects" and returns nothing; callers treat that as "not spawned".
- Effect budget (`FUN_001c7800`, counts kept by `FUN_001c78b0` / `FUN_001c79c0`): each effect definition has a
  priority 0-3. Priority 0 always starts and 3 is not counted; priorities 1 and 2 are refused when the live
  count is over a limit ("gFollowEffectMgr nixed creation of %s", "BuildingFxMgr nixed creation of %s"). Limits
  are set in `RWorld::vf11` 0xa3b10 by `FUN_001c77e0`: 35 and 50 with one player, 30 and 40 with more. Which
  limit belongs to which priority is my reading of the argument order (priority 2 against 35, priority 1
  against 50) and is uncertain.
- Query result caps: 64 objects per monster query; the sphere query asserts when its array fills.
- One spawn per step / per 0.75 s (section 6); one heavy rebuild per rendered frame (section 2).
- World objects cannot be deleted while a query result is in use (`FUN_0004e940` / `FUN_0004e970` lock,
  "Attempt to delete world objects while they are locked"): deletion is deferred through the update flags.

## What the port should copy

Mapped to the port's slow spots. Each point keeps behaviour; where the prototype's own behaviour differs from
the final game the point says so.

(1) Property lookups every frame (`C:\Programming\GitHub\RampageRE\src\prb.c`)

- The prototype's lookup is no faster than the port's (string compares, parent walk). Its speed comes from
  never calling it in a step. Do the same: read CollisionRadius, Mass, Restitution, SlidingFriction, Gravity
  and the rest into the object's own fields (or a per-template record shared by all instances) when the person,
  vehicle, rail car, parade object, shadow or army unit is created, as `WMover::vf7` 0x1d2790 and
  `WSimpleObject::vf5` 0x1d85f0 do, and have the step code read the fields.
- Keep the same defaults at the read site, so a missing property gives the same number as today.
- The existing answer memo in `prb.c` then only serves creation and can stay as it is.
- The per-step call-graph check in section 1 is the test to repeat on the port: no path from a step function to
  `find` except through a spawn.

(2) Fixed-step catch-up loops

- One accumulator for the world, all subsystems stepped inside the same step, in a fixed order (section 2).
  Vehicles, rail, street objects, particles, water and flags should not each run their own catch-up loop.
- Bound: at most two steps per rendered frame, leftover clamped to one step (`RWorldPackage::vf5` 0xa5480).
  This is exactly item 9 of `C:\Programming\GitHub\RampageRE\docs\XBOX_PERF_PLAN.md`, now confirmed as what the
  developers shipped in this build.
- Sleep: let an update say "nothing to do until something touches me" and keep such objects off the update
  list; wake them from the events that can change them (damage, a push, a script). Only for objects whose step
  is provably a no-op at rest (a settled street object, an undamaged building, still water), so results do not
  change.

(3) Collision world rebuilt every frame, linear queries, 50 sub-steps

- Register once, update in place: add at spawn, remove at delete, refresh the stored bound when the object
  moves. Do not rebuild the 768-entry world each frame.
- Query once per querier per step and reuse the list: the monster does one sphere query and feeds every later
  test from that candidate list (`FUN_00065050`). In the port, give each monster, and each flying object, one
  candidate list per step.
- Sub-steps: the prototype's thrown-object sub-steps test only the ground plane. The port's mover (transcribed
  from the PS2 game) also exchanges with other objects, which this build does not show, so the prototype is not
  a model for that part. The behaviour-preserving equivalent: before the sub-step loop, collect the objects
  whose bounds touch the swept bound of the whole step (start to end, plus radius), and run the up-to-50
  sub-steps against that short list only. Anything outside the swept bound cannot be hit in any sub-step, so
  results are identical.
- Flag masks first, bounding sphere second, shape test last (the order in `SpatialDataMgr::vf5/vf8`).
- Buildings: keep them in their own small per-row table with cached neighbour information behind a dirty flag
  (`FUN_0002c6d0`), and turn points into face/column/floor by arithmetic.
- The prototype has no grid or tree for objects. If the port still needs one for 768 objects, that is the
  port's own design; the prototype's answer is fewer queries, not a faster structure.

(4) Name lookups

- Bones: resolve every bone name the game code uses to an index once at model setup and keep the indices in the
  object (`FUN_00059360`).
- Move-graph condition calls: resolve the condition name to a function pointer or enum when the graph is
  loaded and store it in the node, the way the script compiler stores the member index in
  `PMemberAccessNode` (0x252560) and calls by index at run time (0x2526e0). No `strcmp` in play.
- Effects: owners hold the definition handle from load (`WDesc::vf0` 0x1d5660). For spawn-by-name calls that
  must remain, use a hash table keyed on the lowercased name (`FUN_001d2630` / `FUN_001d20c0`), not a scan.
- Sounds and animations: bind to a handle or index at load, play by handle.

(5) Building facades freed, reallocated and rebuilt

- Allocate each building's vertex buffers once at load, sized for the undamaged building. Never free or
  reallocate them in play.
- Keep dirty bits per (stream kind x face) on the building and per floor. A damage event sets bits; the rebuild
  runs once at draw time, only for dirty streams, starting at the first dirty floor and writing in place at
  that floor's stored offset (`FUN_0009b100`).
- A building that moves as a whole (the final sink in `FUN_00099220`) changes one height value and rebuilds
  nothing. Only a floor being crushed re-emits, and only from that floor up.
- Ration the expensive follow-up work (lighting, shadow shapes) to one job per rendered frame with a
  starvation limit of 5 frames, as RWorkQueue does (`FUN_0004c630`). This delays a lighting refresh by a few
  frames after damage; the prototype accepts that, the port should confirm it is not visible.
- An undamaged, idle building does no per-step work at all.

Also worth copying, outside the five spots:

- Fixed pools per size class with a clean "could not spawn" result instead of allocation in play.
- Spawn rationing (one traffic spawn per step, one occupant per 0.75 s) and the priority-based effect limit
  with lower limits for two or more players. These change what appears on screen under load, so they are
  behaviour changes unless the PS2 game does the same; check the PS2 code before adopting them.

## Not established

- Whether the script event-handler tables are filled at script load (assumed).
- The sound play path (inferred from script types only).
- Which effect limit applies to which priority.
- Identity of `FUN_000f5e20` (stepped once per world step before the objects).
- How crowds, army units and rail were budgeted: they are not in this build.
- The prototype was only read, never run; nothing here is a measurement.
