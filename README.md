# PlagueCim — Terminal Disease Simulation in C

> Plague Inc-style disease simulator written in pure C.  
> Graded **100/100** as a university project at Holon Institute of Technology.

![PlagueCim demo](assets/demo.gif)

Real-time disease spread across a world map, with terrain, climate, and settlement modifiers affecting per-cell infection probability via **Bernoulli trials**. Port and ship system using **BFS sea pathfinding** and a gravity model to generate trade routes. Ships sail live across the map. Includes a symptom mutation tree, cure system, and a **flicker-free diff renderer**.

> **Platform:** Windows — build with Visual Studio 2022 (solution included)

---

## How to Build & Run

1. Open **`Plauge simulator.sln`** in Visual Studio 2022
2. Build: **Ctrl+Shift+B** (Debug | x64)
3. Run: **F5**

Visual Studio sets the working directory to the project root automatically, so `assets/map.txt` and `assets/diseases.txt` are found correctly.

---

## Gameplay

| Step | What happens |
|------|-------------|
| **1. Setup menu** | Configure infectivity, lethality, mutation chance, cure speed, awareness rate |
| **2. Pick Patient Zero** | Select any land cell on the ASCII world map |
| **3. Simulation** | Disease spreads tick by tick across land and sea; ships carry infection between ports |
| **4. End screen** | ASCII graph + full report showing how the outbreak evolved |

Controls: arrow keys + Enter in menus. During the simulation press **S** to stop early if the spread stalls.

---

## Project Structure

```
PlaugeCSim/
├── src/                        22 C source modules
│   ├── main.c                  Entry point, simulation loop, frame pacing
│   ├── world.c                 Grid state & per-cell infection spread (Bernoulli trials)
│   ├── disease.c               Disease struct, effective parameter computation
│   ├── symptoms.c              Mutation tree — symptoms unlock and modify spread params
│   ├── cure.c                  Awareness model and cure activation
│   ├── ships.c                 Ships carry infection across sea routes
│   ├── sea_path.c              BFS pathfinding for ship routes between ports
│   ├── ports.c                 Port tile management and gravity-model route generation
│   ├── render.c                Diff-only ANSI renderer — only redraws changed cells
│   ├── hud.c                   Status bar: population / infected / dead / cure %
│   ├── map_io.c                Loads world from assets/map.txt
│   ├── disease_io.c            Saves/loads named disease configs (INI format)
│   ├── setup.c                 Interactive pre-game configuration menu
│   ├── patient_zero.c          Map picker for selecting infection origin
│   ├── end_screen.c            Post-simulation results screen
│   ├── end_graph.c             ASCII line graph of infection/death over time
│   ├── end_condition.c         Win / lose / stall detection
│   ├── turnpoints.c            Records milestone events during the run
│   ├── ansi.c                  ANSI/VT escape code helpers
│   ├── console_win.c           Windows console buffer and VT mode setup
│   ├── rng.c                   Seeded pseudo-random number generator
│   └── utils.c                 Cross-platform sleep, timer, misc helpers
│
├── include/                    Header files (one per module above)
│
├── assets/
│   ├── map.txt                 ASCII world map — terrain, ports, populations
│   └── diseases.txt            Saved disease configurations (INI-style)
│
├── Plauge simulator.sln        Visual Studio 2022 solution  ← open this
└── Plauge simulator.vcxproj    Project file
```

---

## Technical Details

### Spread model — Bernoulli trials per cell
Each tick, every land cell runs an independent Bernoulli trial. Infection probability is the disease's effective infectivity multiplied by climate modifiers (hot/cold) and settlement modifiers (rural/urban). Dead cells reduce the susceptible pool.

### BFS sea pathfinding + gravity model
`sea_path.c` runs BFS on the world grid to find navigable sea routes between port tiles. `ports.c` applies a gravity model (population × inverse distance) to weight which ports generate active trade routes. Routes are precomputed once at startup; ships then sail along them in real time, carrying infection between continents.

### Symptom mutation tree
Symptoms are nodes in a DAG. Each tick a Bernoulli trial (configurable % chance per tick) determines whether a new symptom unlocks. Unlocked symptoms modify the disease's effective infectivity, lethality, and spread multipliers via `disease_refresh_effective_params_from_symptoms()`.

### Awareness / cure model
Awareness grows each tick proportional to the current infection count and death count (separate configurable coefficients). Once awareness saturates, cure progress begins. After cure activation, infected cells recover at a tunable gamma rate — the map visually fades from red to green.

### Flicker-free diff renderer
`render.c` maintains a per-cell cache (infection bin, death bin, glyph). Each frame only cells whose cached state differs from the current state are rewritten to stdout — eliminating full-screen flicker at high frame rates.

### File-based map format
`assets/map.txt` is plain ASCII. Each character encodes terrain and settlement:

| Glyph | Meaning |
|-------|---------|
| `*` | Dense urban land |
| `#` | Rural land |
| `P` | Port (land + sea connection) |
| ` ` | Sea |

Population values are encoded per-cell and parsed at load time into a heap-allocated `Cell**` grid.

### Saved diseases
Completed runs can be saved to `assets/diseases.txt` in INI-style format and reloaded in future sessions via the setup menu.
