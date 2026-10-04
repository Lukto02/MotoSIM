# Modding MotoSim

Maps, bikes and objects are plain text files in the `mods/` folder next to the game. You don't need
to compile anything: write a file, save it, and the game rebuilds the map while you ride.

```
mods/
  base/                  the maps that ship with the game (a good reference)
    maps/motocross.json
    maps/favela.json
    bikes/motocross.json
    bikes/trilheira.json + trilheira.ini
  sandbox/               example mod: a physics park, a heightmap valley, a free-roam dune map and a custom bike
    maps/park.json
    maps/valle.json + valle.png
    maps/medanos.json    dunes and sculpted jumps, no track (terrain "dunes", "shapes", track "guide")
    bikes/dostiempos.json + dostiempos.ini
  your_mod/              <- yours: any folder name
    maps/*.json
    bikes/*.json
```

Every map in `mods/*/maps/` shows up in **Esc → Mapa**. A map's id is `folder/file`
(for example `sandbox/park`). That id is what travels over the network, and what you pass to
`--map` on the command line.

## Quick start

1. Copy `mods/sandbox` to `mods/my_mod` and rename `maps/park.json` to `maps/my_park.json`.
2. Change its `"name"` and start the game. Pick your map in **Esc → Mapa**.
3. Edit the file with the game open and save it. The map rebuilds at once, and your bike stays
   where it was. If the file has a mistake, the game shows the file, line and column on screen and in
   the map list (for example `my_mod/maps/my_park.json:12:5: falta ',' o ']' en la lista`) and keeps
   the previous version running. (The game's messages are in Spanish.)

| Key | What it does |
|---|---|
| save the current map's file | rebuilds that map |
| **Shift+F5** | re-reads every mod (new maps, new bikes) and rebuilds the current map |
| **F5** | reloads `tuning.ini` and the current bike's `.ini` (no rebuild) |

The files are JSON, with two relaxations: `// comments` and `/* comments */` are allowed, and so is
a trailing comma after the last item of a list. Text is UTF-8.

## Coordinates

- Meters. `x` points east, `z` points north and `y` points up. The map is a 255.5 m square
  centered on (0, 0), so x and z run from about -127 to 127.
- The ground rises near the border to keep riders in (see `terrain.edge`). Keep the track inside
  about ±110.
- Angles are in degrees. Yaw 0 faces north (+z), yaw 90 faces east (+x).

---

## Map file reference

Only `track.points` is required; every other field has a default.

```jsonc
{
  "name": "My park",                 // shown in the map list
  "kind": "sandbox · ramps",         // small colored line under the name (default: the mod's folder name)
  "description": "One line for the map list.",
  "hint": "Shown for a few seconds when the map loads.",
  "color": "#4fc3f7",                // color of the "kind" line (or [r, g, b])
  "order": 100,                      // place in the map list, lower first (the base maps use 1 and 2)
  "bike": "base/motocross",          // which bike this map is ridden with (see "Bike file reference")
  "bot_speed": 17,                   // m/s: the test bot's top speed on this map (--bot)
  "bot_lateral": 6,                  // m/s²: how hard the bot takes the corners (default: --bot-lat, 6)
  "bot_brake": 4,                    // m/s² it plans braking with (default 4, or 2.5 on street maps)
  "bot_lean_throttle": 0,            // 0..1: how much it closes the throttle leaned over (at 45°)

  "terrain": { ... },
  "track": { ... },
  "start": [x, z],
  "features": [ ... ],   // or "auto_features": [ ... ]
  "objects": [ ... ],
  "look": { ... },
  "terraces": { ... },
  "generator": "",
  "markers": true
}
```

### `terrain`: the ground

| Field | Default | Meaning |
|---|---|---|
| `type` | `"hills"` | `hills`, `flat`, `heightmap`, `favela` or `dunes` |
| `height` | 10 | hills: how tall the big hills are (m). heightmap: the height of pure white (m). dunes: the tallest dunes (m) |
| `scale` | 0.011 | hills: hill frequency (smaller = wider hills) |
| `detail` | 0.5 | hills: small bumps (m) |
| `image` | | heightmap: a grayscale PNG next to the .json. It is stretched over the whole map: the top of the image is north and white is high |
| `base` | 0 | heightmap: the height of pure black (m) |
| `edge` | 14 | how much the ground rises at the map border (m). 0 = no wall |
| `size` | 255.5 | width of the (square) map in meters, 64 to 4096 |
| `resolution` | 0.5 | meters between terrain samples, 0.25 to 4. At most 2048 samples per side: a big map needs a coarser grid (e.g. `"size": 1000, "resolution": 1`) |

`favela` is the Morro do Grau hillside, a slope of about 20% rising to the north.

`dunes` is a field of transverse sand dunes: rows of crests across the wind, a gentle windward side
that gets steeper up to the crest (a natural lip) and a steep lee face that starts flat at the crest
(land on it going down). The crests wander and break up into pieces, some areas have tall dunes and
others low ones (35% to 100% of `height`), and near the map border the dunes grow (a dune sea that
hides the border wall). It is always the same (no randomness).

| Field (dunes) | Default | Meaning |
|---|---|---|
| `wavelength` | 70 | m from crest to crest |
| `wind` | 0 | degrees, where the wind blows to (0 = north, 90 = east): the steep faces look that way |
| `meander` | 0.35 | how much the crests wander (in wavelengths) |
| `border` | 1.2 | how much taller the dunes get near the border (1.2 = 2.2 times as tall; 0 = the same) |
| `seed` | 0 | another number gives another dune field |
| `detail` | 0.5 | small bumps (m); sand is smooth, 0.1 is plenty |

#### `terrain.shapes`: sculpted ground

Hand-made jumps, hills, bowls and walls sculpted into the ground itself (any terrain type). They are
smooth ground, not objects: the wheels roll on them like on any hill, and the track (a `track` or
`street` style track) is stamped on top of them.

Each shape is a **profile**: a side view, a list of `[u, height]` points joined by straight lines
(`u` in meters, increasing). The corners are rounded over `smooth` meters. Then the profile is
stretched sideways (`"shape": "line"`) or spun around a center (`"shape": "round"`).

```jsonc
"terrain": {
  "type": "dunes", "height": 8,
  "shapes": [
    // a kicker ridden north: flat, 25° face up to 1.5 m, a 5 m table, a long landing back to 0
    { "shape": "line", "at": [0, -40], "yaw": 0, "width": 12, "edge": 8,
      "profile": [[-30, 0], [0, 0], [3.2, 1.5], [8.2, 1.5], [16, 0]] },
    // a bowl 20 m across: floor 2 m below the ground, 45° walls, rim at 3 m
    { "shape": "round", "at": [60, 20], "mode": "level", "base": 0,
      "profile": [[0, -2], [6, -2], [11, 3], [12, 3], [20, 0]] }
  ]
}
```

| Field | Default | Meaning |
|---|---|---|
| `shape` | `"line"` | `line`: the profile runs along `yaw` (u = 0 at `at`) and is `width` m wide. `round`: u is the distance from `at` (the profile is spun around it; u starts at 0) |
| `at` | | `[x, z]` |
| `profile` | | at least 2 points `[u, height]`, u increasing |
| `yaw` | 0 | line: the direction u grows (0 = north, 90 = east): the direction you ride it. round: the axis of `stretch` |
| `width` | 10 | line: meters at full height, across |
| `edge` | 6 | meters over which the shape fades into the ground around it (sides and ends) |
| `smooth` | 1 | meters over which the corners of the profile are rounded (0 = sharp) |
| `bend` | 0 | line: the ends move this many meters along u (a crescent, like a barchan dune: horns downwind) |
| `stretch` | `[1, 1]` | round: stretches the circle across and along `yaw` (an ellipse) |
| `arc` | whole circle | round: keeps only a slice, `[from, to]`: headings seen from `at` (0 = north, 90 = east), clockwise from `from` to `to` (`[90, 180]` is the south-east quarter). Past its ends it fades out over `edge` m. With a profile that rises at the outside radius it is a **berm** (a banked turn) |
| `mode` | `"add"` | `add`: the profile is added to the ground. `level`: the ground becomes `base` + profile (the dunes under it disappear; use it to carve flat corridors, run-ups and landings) |
| `base` | ground at `at` | level: the height it levels to |
| `dirt` | false | paints the shape with the track's dirt, as far as it reaches: its texture, its grip and the dust it throws (dirt jumps on a grass map read as jumps) |

```jsonc
// a berm for a left turn from east-bound to north-bound, 31 m in radius (the rider goes round the
// center at [109, -174]): flat inside, the bank rises from 28 m out to 3.4 m, only in the south-east quarter
{ "shape": "round", "at": [109, -174], "arc": [90, 180], "edge": 16, "mode": "level", "base": 0,
  "profile": [[0, 0], [28, 0], [31, 0.4], [34, 1.6], [37, 3.4], [38.5, 3.4], [45, 0]] }
```

Shapes are applied in order: a later one sits on top of (or levels) the earlier ones. How to make
jumps that land well (the landing slope has to follow the arc, and how far you fly grows with the
square of the speed) is in `mods/sandbox/maps/medanos.json` and in `tools/medanos.py`, which
generated it; `mods/sandbox/maps/park.json` (written by `tools/parque.py`) has dirt jumps, a pump
track with berms, a bowl and a quarter pipe on flat ground.

### `track`: the lap

```jsonc
"track": {
  "points": [[-80, -95], [0, -96], [80, -95, "asphalt"], ...],
  "width": 9,          // meters
  "scale": 1.0,        // multiplies every position in the map (points, start, features, objects...)
  "style": "track",    // "track" or "street"
  "bank_height": 1.9,  // outer berm height in meters (0..4); initial track uses 4.0
  "bank_gain": 34,     // curvature gain (0..100); initial track uses 100 (the berm is then smoothed over
                       // about +-20 m, so in practice bank_gain sets how high it gets)
  "banking": true      // berms on the outside of the corners (default: true for "track")
}
```

- **points**: at least 4 `[x, z]` control points. The track is a closed loop through them (a
  smooth centripetal Catmull-Rom spline) and runs in the order you list them. Space the points
  10–60 m apart. Tight corners need points close together.
- **surface**: an optional third value, `[x, z, "surface"]`, sets the surface from that point on,
  until another point names a different one. It can be `asphalt`, `concrete` or `dirt` (street
  style), or `track` (motocross dirt).
- **style `track`**: motocross. The track follows the terrain smoothed along its length, blends into
  the natural ground over long shoulders, and is dirt with grass around it.
- **style `street`**: streets cut level into a hillside. Where two streets cross they blend (the
  flatter one wins), and each point's surface is used (paved streets grip more and throw no dirt).
  Pair it with `"look": {"ground_textures": "street"}`.
- **style `guide`**: for free-roam maps. The lap is only a guide: it doesn't change the ground (no
  smoothing, no dirt strip, no berms) and there are no stakes or start gate (`markers` defaults to
  false). The bot, the respawn (R) and the lap timer still follow it, so make it a long loop over the
  flat corridors and the best jumps. Obstacles (`features`) are not built on a guide.

### `start`

`[x, z]`: the start/finish line goes on the track point closest to it. Without it, the line goes at
the beginning of the first straight that is at least 40 m long (or 14 m after the first point if
there is none).

### `features`: jumps and obstacles on the track

Place them by hand, each one centered on the track point closest to `at`:

```jsonc
"features": [
  { "kind": "tabletop", "at": [-23, -25] },
  { "kind": "kicker", "s": 120 }          // or starting at a distance along the track (m from the first point)
]
```

Or let the game place them on the straights with `auto_features`: one list per straight that is at
least 40 m long, in race order, starting from the straight with the start line. Obstacles that don't
fit in their straight are dropped.

```jsonc
"auto_features": [["rollers", "tabletop", "kicker"], ["stepup"], ["double"]]
```

| kind | Length | Shape |
|---|---|---|
| `rollers` | 24 m | three smooth 0.45 m bumps |
| `whoops` | 40 m | ten 0.32 m whoops |
| `tabletop` | 24.5 m | 1.7 m high, 9 m flat top |
| `double` | 35 m | two 2.3 m jumps with a 10 m gap |
| `stepup` | 24.5 m | steps up 1.4 m, long way down |
| `kicker` | 11 m | a 1.2 m kicker |
| `rampa` | 6.5 m | short, steep 1 m dirt ramp |
| `quebramola` | 1.8 m | speed bump (13 cm) |
| `escadaria` | 11.5 m | stairs. Only the `favela` generator builds the steps; on other maps it is flat |

### `objects`: boxes, ramps, barrels, balls

```jsonc
"objects": [
  { "shape": "ramp", "at": [0, 0, 45], "size": [7, 3.5, 9], "yaw": 0, "color": "#e0a040" },
  { "shape": "cylinder", "at": [30, 0, -40], "size": [0.6, 0.9], "dynamic": true, "mass": 20 },
  { "shape": "sphere", "at": [15, 0, 25], "size": 2.2, "dynamic": true, "mass": 45 }
]
```

| Field | Default | Meaning |
|---|---|---|
| `shape` | `"box"` | `box`, `ramp` (a wedge), `cylinder` or `sphere` |
| `at` | | `[x, y, z]`: y is the gap between the ground and the object's lowest point, so 0 means resting on the ground and a negative y buries it. `[x, z]` = on the ground |
| `absolute` | false | if true, y is an absolute height instead of relative to the ground |
| `size` | 1 | `[width, height, length]`. Cylinder: `[diameter, height]`. Sphere: `diameter`. A single number = a cube |
| `yaw`, `pitch`, `roll` | 0 | rotation in degrees (applied yaw, then pitch, then roll). A ramp rises towards its yaw direction |
| `color` | light gray | `"#rrggbb"` or `[r, g, b]` |
| `dynamic` | false | false = fixed. true = loose: it falls, rolls and gets knocked over |
| `mass` | 20 | kg (loose objects) |
| `friction` | 0.8 | |

- **Fixed objects** can be ridden: the wheels roll on them just as they do on the ground, so you can
  build ramps, tables, quarter pipes and walls. A cylinder lying down and half buried (`"roll": 90`,
  negative y) makes a pump-track roller.
- **Loose objects** are pushed and knocked over by the bike's body, and by the rider's body after a
  crash. The wheels don't climb on them. Each PC simulates its own: in multiplayer everyone sees their
  own barrels.

### `look`: light and sky

```jsonc
"look": {
  "preset": "day",            // "day" or "sunset"; the fields below override the preset
  "sun_dir": [-0.55, 0.62, -0.45],  // direction towards the sun (normalized for you)
  "sun_color": [2.35, 2.15, 1.85],  // linear, can go above 1
  "zenith": [78, 128, 196],         // sky color straight up (0..255)
  "horizon": [186, 206, 224],       // sky color at the horizon
  "ground": [92, 80, 64],           // light bounced from the ground
  "fog": 0.0042,                    // fog density
  "exposure": 1.0,
  "ground_textures": "dirt"         // "dirt" (motocross dirt and grass), "street" (pavement and red soil),
                                    // "circuit" (asphalt on the track, cut grass everywhere else)
                                    // or "sand" (golden sand with wind ripples, a few dry tufts)
}
```

The `sunset` preset also switches the ground to `street`; set `ground_textures` after it to change
that (for example `"preset": "sunset", "ground_textures": "sand"`). On sand the light bounced from
the ground is bright: a sand-colored `ground` (like `[160, 120, 88]`) keeps the shaded dune faces
warm instead of purple.

The game also applies a colour grade to the final image (a little more exposure, saturation and contrast,
and the player can pick the style in the menu), so do not brighten or saturate a `day` look to compensate:
the grade already does it. The `fog` of each map is its starting point; the player's fog level (25% to 150%)
multiplies it.

### Advanced

- **`terraces`**: turns a steep stretch of track into level plateaus at crossings, with lips (jumps)
  in between, like the favela's ladeira. It runs between the track points closest to `from` and `to`.

  ```jsonc
  "terraces": { "from": [0, 88], "to": [0, -80], "plateaus": [[0, 69], [0, 27]], "half": 4.5 }
  ```

- **`generator`**: `"favela"` fills the map with favela houses, poles and wires, signs, kites and the
  Rio skyline, all built around the track. It is always the same (fixed seed). Leave it out on other
  maps.
- **`generator`: `"circuit"`** builds a road-racing circuit around the track: kerbs from the curvature,
  edge lines, start grid and gantry with lights, pits, grandstands, gravel traps, walls with catch fence,
  billboards, trees. Use it with `"ground_textures": "circuit"` and `"track": {"style": "street"}`.
  Options go in a `"circuit"` section (all optional):

  ```jsonc
  "circuit": {
    "name": "SIERRA DE LOS VIENTOS",   // on the gantry and the pit building
    "pits": "auto",                    // "left", "right" or "auto" (outside the loop)
    "billboards": ["MOTOSIM", "CAFÉ LA CHICANA"]   // up to 12 texts
  }
  ```
- **`markers`**: the motocross stakes and start gate (default: true unless there is a generator).

---

## Bike file reference

`mods/<mod>/bikes/<id>.json`. A map picks it with `"bike": "<mod>/<id>"`.

```jsonc
{
  "name": "Dos tiempos 250",   // shown in the bike list
  "style": "mx",               // the parts and the rider pose (see below)
  "description": "One line under the name in the bike menu.",
  "tuning": "dostiempos.ini"   // optional, next to the .json
}
```

Styles: `mx` (motocross), `mx2t` (motocross with a two-stroke engine: expansion chamber, 90s colors),
`trail` (the Trilheira: street trail with an open pipe), `race` (a sport bike with fairing: the rider
tucks in behind the screen at speed and hangs off with the knee down in corners) and `trial` (tiny
tank, the rider always stands on the pegs). The
style is only the look and the pose: how the bike rides is all in `tuning`.

Every bike is listed in the menu (**Moto**) with its numbers, computed from its tuning: power, weight,
top speed, 0 to 100 km/h, cornering grip, suspension travel and tightest turn. The one you pick is
used on every map (the first entry, "La del mapa", goes back to each map's own `bike`) and is saved
for the next time. `--bike <mod>/<id>` picks it from the command line.

The `tuning` file is read **after** `tuning.ini` and overrides only the keys it contains. Any key of
`tuning.ini` can go there: mass and inertia, suspension, tires, engine and gearing, steering, the
air-control and wheelie assists, the grau (Brazilian wheelie) assist... Open `tuning.ini` to see them
all with comments, and `mods/base/bikes/trilheira.ini` for a full example. Press **F5** in game to
reload it.

Two engine keys that only make sense per bike:

| Key | Default | Meaning |
|---|---|---|
| `engine_rpm_scale` | 1 | stretches the torque curve in rpm: 1.5 puts the peak 50% higher (a high-revving race engine; raise `engine_rev_limit` with it) |
| `engine_cylinders` | 1 | how many cylinders the engine sound fires: 1 is a single, 2 a twin, 4 an inline four |
| `engine_two_stroke` | 0 | 1 = two-stroke sound: fires every turn, gets loud and raspy "on the pipe" (above ~60% of the rev limit), misfires off throttle |

Automatic gearbox: it shifts up at 9400 rpm and down at 4800, both multiplied by `engine_rpm_scale`.

| Key | Default | Meaning |
|---|---|---|
| `front_tire_loose_grip`, `rear_tire_loose_grip` | 1 | grip multiplier on dirt and grass (a racing slick: 0.35) |
| `front_tire_paved_grip`, `rear_tire_paved_grip` | 1 | grip multiplier on asphalt, concrete and objects |
| `lean_surface_grip` | 0 | 1 = where the tires grip sideways less than `max_lateral_accel` (ground × `*_loose/paved_grip` × `*_lat_grip`), the rider asks for that much and leans less (slicks on dirt). 0 = always asks for `max_lateral_accel` |
| `brake_align` | 0 | braking hard, the bike lines up with where it goes, also while turning (in a straight line the front wheel also follows its own path) (1/s; the race bike uses 8, the dirt bikes 4) |
| `brake_align_torque` | 1500 | how hard `brake_align` lines the bike up (Nm per rad/s; the others use 2000) |
| `brake_align_from`, `brake_align_full` | 0.2, 0.6 | front brake where `brake_align` starts and where it acts fully (the Trilheira: 0.55 and 0.95, only when braking really hard, like the S key does) |
| `brake_align_free` | 1 | braking straight with `brake_align`, how much the front wheel is let go to follow its own path (0 = not at all: only the bike lines up) |
| `brake_yaw_comp` | 0 | braking while leaned, how much of the outward yaw from the brake force at the contact patch is cancelled (1 = all; the race bike uses 1). Without it the bike stays leaned but runs wide |
| `brake_transition_release` | 0 | while the bike is changing lean a lot (what the rider asks vs what it has), the ABS eases the front brake by up to this much: stand it up first, then brake (the race bike uses 0.3) |
| `lean_sweep_comp` | 0 | 1 = while the bike stands up, the tires don't feel the sideways sweep of the contact patches (the lean pivots around the center of mass here, not around the contact line like a real bike). Without it, letting go of a hard turn to brake sends the bike toward the other side (the race bike uses 1) |
| `rear_lift_load`, `rear_lift_mitigation` | 300, 0.25 | the ABS eases the front brake when the rear carries less than this many N (straight up, by this much) |
| `slide_pivot` | 800 | below ~13 km/h, rear brake while turning: the rider pushes the tail out (Nm per rad; 0 = off) |
| `crash_impact_speed`, `crash_impact_vertical` | 7, 11 | m/s against something fixed (front/side, or falling flat) that throws the rider off |

Wheelies: while turning on the rear wheel the bike yaws along its path (`wheelie_turn`, Nm per rad/s,
default 900; `wheelie_align`, 1/s, default 4). 0 turns it off.

Deformable dirt (ruts, Settings > Terrain): what you see (mesh, ruts, berms) is the full rut, but the wheels
feel a smoothed version of it so a rut does not steer the bike. These go in `tuning.ini` (or a bike's `tuning`):

| Key | Default | Meaning |
|---|---|---|
| `rut_ride` | 1 | 0 = the wheels feel the rut as it is (the old behaviour: they get trapped in the walls); 1 = smoothed |
| `rut_ride_depth` | 0.02 | m: soft cap on how far a wheel sinks into a rut (0 = no cap) |
| `rut_ride_blur` | 0.25 | m: radius (sigma) of the average of the rut under the wheel |
| `rut_ride_sink` | 0.02 | m/s: how fast the ground under the wheels follows a new rut (0 = instantly) |

---

## Multiplayer

- Everyone in the game must have the same mod in their `mods/` folder (same folder name and file).
- Each player rides their own bike: the id travels with the player, and the others need that bike in
  their `mods/` too (if not, they see that player on their own bike).
  When the host picks a map that a player doesn't have, that player stays on their map and gets a
  message naming the missing one (`El anfitrión corre <id> y no lo tenés...`).
- Loose objects are simulated on each PC and are not sent over the network.

## Testing from the command line

```
MotoSim.exe --map sandbox/park            start on that map (id, file name or visible name)
MotoSim.exe --map sandbox/park --bot      the bot rides it (does the track work? are the jumps OK?)
MotoSim.exe --map sandbox/park --bike sandbox/dostiempos --bot
                                          the same with another bike
MotoSim.exe --menu bikes                  opens the bike menu
MotoSim.exe --headless --map sandbox/park --bot --time 120 --telemetry
                                          no window: prints lap times, errors and telemetry
```
