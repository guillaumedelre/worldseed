# VibeUE — le manuel des outils MCP

> **CE MANUEL DECRIT UNE VOIE QUI EXIGE L'EDITEUR LANCE**, ecoutant sur le port
> 8000. Ce projet travaille presque toujours dans l'autre voie — le commandlet,
> qui exige l'editeur ARRETE — et **les deux s'excluent**. Voir la section
> `A ter. AGIR SUR L'EDITEUR` de `CLAUDE.md`, qui porte la mesure et les
> invocations.
>
> **Quand le charger** : le jour ou le travail redevient lourd en assets —
> materiaux, Niagara, Blueprints, edition interactive — et que l'editeur tourne.
> Il est alors la bonne voie, et nettement plus rapide qu'un aller-retour de
> commandlet.
>
> ```
> grep -n '^## ' Docs/vibeue-mcp.md          # le sommaire
> grep -n -i 'capture_image\|discover_python' Docs/vibeue-mcp.md
> ```
>
> **D'OU IL VIENT, ET CE QUE CELA IMPLIQUE.** C'est le preambule et les sections
> 1 a 7 du guide genere par `VibeUE.GenerateAgentConfig`, sorties de `CLAUDE.md`
> le 29 septembre 2026 : 13 091 caracteres, ~3 300 tokens, pour un chemin pris
> zero fois sur les 22 scripts de `Tools/UE`. **Une regeneration du guide les
> reecrira dans `CLAUDE.md`** — c'est detectable (le fichier repasse a ~47 Ko et
> les sections 1 a 7 reapparaissent au-dessus de la section 8) et la decoupe se
> refait en une minute.
>
> `Build & launch` et `Critical rules` — les sections 8 et 9 — sont RESTEES dans
> `CLAUDE.md` : elles valent dans les deux voies. La section 10 a ete retiree,
> la section `A.` de `CLAUDE.md` la supplantant explicitement.

---

VibeUE **extends Unreal 5.8's native AI toolset system** — its services, tools, and skills register
into the engine's `ToolsetRegistry` and are reachable through the MCP tools you already have.

**ALWAYS use the MCP tools / Python API for Unreal operations — NEVER read `.uasset` files from disk.**

### Environment, evidence, and unattended work

- Call `unreal.WorkflowService.get_environment()` before claiming Unreal source, UBT, a compiler,
  or an engine installation is missing. Prefer the reported `engineSource.path` over memory or web
  guesses about engine behavior.
- C++ work is not complete without `lastBuild.status == "succeeded"` from a real build. A skipped,
  missing, running, failed, or stale result is not verification. Use the plugin's
  `BuildAndLaunchGame.ps1`/`.sh` when Live Coding is unsafe.
- Wrap unattended mutations with `start_run(name, metadata)` and `finish_run(run_id, outcome,
  summary)`. Attach scenario, build, and bulk-report artifacts; affected UObject packages are
  deduplicated automatically while the run is active.
- Use `run_scenario(spec_json)` for repeatable PIE input/assert/evidence loops and poll
  `get_scenario(id)` from separate calls. Use the `bulk-maintenance` skill for dry-run-first batches.

---

## 1. The efficient interaction model (read this first)

There are two ways to act on the editor. Pick the cheap one:

- **`execute_python_code` — your workhorse.** Runs an arbitrary Python script in the editor in **one
  round-trip**. Every VibeUE service is exposed to Python (`unreal.BlueprintService.build_graph(...)`)
  and sits next to the whole native `unreal.*` API in the same script. **Batch aggressively** — do a
  whole multi-step task (create + edit + compile + verify) in a single call, and `print()` only what
  you need back.
- **`call_tool` — one tool per round-trip.** Genuinely needed only for **skills**
  (`AgentSkillToolset`). **Everything else from Epic's engine toolsets is also reachable from
  inside `execute_python_code`** via `unreal.ToolsetRegistry.execute_tool(...)` (see §2), so batch
  engine-toolset calls with your Python instead of spending a round-trip. Don't use `call_tool`
  for work `execute_python_code` can batch.
- **`capture_image` — screenshots you can actually SEE.** Returns a real MCP image block the
  client renders (never a base64 text blob). `source="game"` captures the PIE viewport
  **including the Slate/UMG HUD** — the thing `CaptureViewport` cannot see (see §6).

**Speed + tokens:** **avoid `describe_toolset` as a habit** — it dumps the full JSON schema of every
tool in a toolset (the most token-heavy thing here); reach for a **skill** plus a narrow
`discover_python_class('unreal.BlueprintService', method_filter='variable')` instead.

---

## 2. Tool roster — what's where

**VibeUE MCP tools (call directly):**
- `execute_python_code` — run Python (must start with `import unreal`). The workhorse.
- `discover_python_module` / `discover_python_class` / `discover_python_function` — inspect the API
  (use `unreal` lowercase; narrow with `name_filter` / `method_filter`).
- `list_python_subsystems` — list editor subsystems.
- `capture_image` — screenshot as a real MCP image (see §6): `source` = `game` (PIE incl. UMG HUD),
  `window` (whole editor window), or `editor` (viewport scene). Also saves a PNG under
  `Saved/VibeUE/Captures` and returns the path.
- `deep_research` — web search / page fetch / geocode (see §5).
- `terrain_data` — real-world heightmaps + water splines (see §5).

**VibeUE services (call from Python inside `execute_python_code`):** `unreal.<Name>Service.<method>()`
— Blueprint, BlueprintGraph (via BlueprintService), Material(+Node), Widget, Skeleton, AnimSequence,
AnimMontage, AnimGraph, Landscape(+Material), Foliage, MetaSound, SoundCue, Niagara(+Emitter,
+ScratchPad), StateTree, BehaviorTree, Blackboard, Input, EnumStruct, UVMapping,
RuntimeVirtualTexture, MapBlockout, GameplayTag, Viewport, Actor, Engine/ProjectSettings,
**Performance** (`unreal.PerformanceService.frame_timing()`).
These overlap-trimmed services keep only what the engine lacks — for plain asset/actor/blueprint
basics the engine's own tools may be simpler (below).

**Calling Epic's engine toolsets from Python (`execute_tool`).** Epic's engine toolset *classes*
exist as `unreal.*` (e.g. `unreal.EditorAppToolset`) but their AICallable functions are **not**
exposed as Python methods — `unreal.EditorAppToolset.get_selected_assets()` fails. Invoke them
through the registry instead (same dispatch `call_tool` uses, but in-process and batchable):
```python
import unreal, vibeue   # vibeue ships in the plugin's Content/Python — always importable

out = vibeue.exec_tool("EditorToolset.EditorAppToolset", "GetSelectedAssets")
# exec_tool fills missing optional params from the tool's schema (so bare StartPIE/CaptureViewport
# calls just work), reports ALL missing required params in ONE error, and returns fully-decoded
# Python values (the raw execute_tool "returnValue" is sometimes double-encoded JSON).
```
Discover schemas by NAME with `vibeue.get_toolset_schema("EditorToolset.EditorAppToolset")` /
`vibeue.list_toolset_names()`. (The engine's own
`unreal.ToolsetRegistry.get_toolset_json_schema(...)` takes a **class object** like
`unreal.EditorAppToolset`, NOT a name string; `get_all_toolset_json_schemas()` dumps everything —
token-heavy.) Names are namespaced — `EditorToolset.EditorAppToolset`,
`NiagaraToolsets.NiagaraToolset_System`, etc. (a bare `"EditorAppToolset"` returns "Toolset not
found"). Raw `unreal.ToolsetRegistry.execute_tool(name, tool, args_json)` remains available; it
returns an **async** result — the editor tools above complete synchronously (`is_complete=True`),
but for a long-running tool check `is_complete` / bind `on_completed` rather than assuming `value`
is ready.

**Use the engine's native tools for these (VibeUE intentionally doesn't duplicate them):**
- **Assets** (find / save / move / delete / duplicate / metadata): native Python
  `unreal.EditorAssetLibrary` / `EditorAssetSubsystem` inside `execute_python_code` (batchable), or
  Epic's `AssetTools` toolset (via `execute_tool` in-Python, or `call_tool`).
- **Screenshots / vision**: use VibeUE's **`capture_image` MCP tool** (see §6) — it is the only
  path that returns a renderable image. Epic's `CaptureViewport`/`CaptureEditorImage`/
  `CaptureAssetImage` return base64 **inside a text block** on every route (including `call_tool`),
  which you cannot view and which can blow the client token limit; reach for `CaptureViewport` only
  when you need its world-grid/actor-label annotations, via `vibeue.exec_tool` (which fills its
  mandatory param shape for you).
- **PIE**: `EditorAppToolset.StartPIE` / `StopPIE` / `IsPIERunning` — via `vibeue.exec_tool`
  (bare `StartPIE` works; the raw path demands the full options shape), or `call_tool`.
- **Logs**: `LogsToolset.GetLogEntries` — via `execute_tool` or `call_tool` (or read the `.log` file).
- **DataTables / DataAssets / enum-struct basics**: Epic's `DataTableTools` / `DataAssetTools` /
  `ObjectTools` (via `execute_tool` or `call_tool`). (VibeUE keeps only `EnumStructService` for
  create/edit of user enums & structs.)

---

## 3. Skills — native `AgentSkill` (lazy domain knowledge)

VibeUE's ~88 skill packs are registered as Unreal **AgentSkills** and served by the engine's
`AgentSkillToolset`. Skills tell you **what to do and why**; they do **not** replace discovery of exact
signatures.

**Discover + load (both are `call_tool` on `ToolsetRegistry.AgentSkillToolset`):**
```
call_tool(tool_name="ListSkills", toolset_name="ToolsetRegistry.AgentSkillToolset")
  → { "/VibeUE/Python/init_unreal_PY.VibeUE_blueprints": "Create and modify Blueprint assets…", … }

call_tool(tool_name="GetSkills", toolset_name="ToolsetRegistry.AgentSkillToolset",
          arguments={"skillPaths": ["/VibeUE/Python/init_unreal_PY.VibeUE_blueprints"]})
  → full markdown for that pack
```
- `ListSkills` returns **summaries only** (cheap) — call it once per session to see what exists. VibeUE
  packs are `/VibeUE/Python/init_unreal_PY.VibeUE_<name>`; the engine's own skills appear alongside.
- `GetSkills` returns full instructions **lazily** — request only the packs you need.
- **Sub-docs are their own skill entries** (e.g. `…VibeUE_blueprint_graphs__build_graph`,
  `…VibeUE_state_trees__api_reference`) — load them by path the same way; no `skill/section` argument.

**When to load a skill:** the user names a domain ("create a blueprint", "build a state tree"), or you
hit a non-obvious workflow. Then: read the pack → `discover_python_class` the classes it names → write
the Python. Don't reload a pack you already loaded this session.

---

## 4. Python basics

```python
import unreal  # lowercase

# Editor subsystems:
sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# VibeUE services are static classes, called directly:
info = unreal.BlueprintService.get_blueprint_info("/Game/MyBP")

# Batch a whole task in ONE execute_python_code call, printing evidence as you go.
# Blueprint create / variables / compile are ENGINE-side (native libs + BlueprintTools toolset),
# NOT BlueprintService (it owns graphs/components/timelines — see discover_python_class):
import json
factory = unreal.BlueprintFactory(); factory.set_editor_property("ParentClass", unreal.Actor)
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("BP_Enemy", "/Game/Blueprints", unreal.Blueprint, factory); print("CREATED:", bp)
unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "add_variable",
    json.dumps({"blueprint": {"refPath": "/Game/Blueprints/BP_Enemy.BP_Enemy"},   # refPath = full object path
                "name": "Health", "type_name": "float"})); print("ADDED: Health")
unreal.BlueprintEditorLibrary.compile_blueprint(bp); print("COMPILED: BP_Enemy")
```

---

## 5. When to use `deep_research` and `terrain_data`

**`deep_research`** — when you need information that isn't in the editor:
- `action="search"` / `action="fetch_page"` — research a UE topic, API, or technique before writing code.
- `action="geocode"` / `action="reverse_geocode"` — turn a place name into lat/lng (feeds `terrain_data`).

**`terrain_data`** — when the user wants terrain from a **real-world location**:
- `preview_elevation` → use the suggested `base_level`/`height_scale` → `generate_heightmap`
  (`resolution` MUST match the landscape) → import via `unreal.LandscapeService` → `get_water_features`
  for rivers/lakes.

**The real-world-terrain chain:** `deep_research(geocode "Mount Fuji")` → `terrain_data(generate_heightmap, lng/lat)`
→ `LandscapeService` import → `terrain_data(get_water_features)` → landscape splines. Load the
`terrain-data` and `landscape` skills for the resolution formulas and water workflow.

---

## 6. See what you built (screenshots)

After any **visible** change, capture and actually look before claiming success:
```
capture_image                          # editor viewport scene (default when PIE is off)
capture_image {"source": "game"}       # PIE game viewport INCLUDING the Slate/UMG HUD
capture_image {"source": "window"}     # the whole editor window (any editor UI/panel)
```
`capture_image` returns a **real MCP image block** — you see the picture directly, no base64
decoding, no token blowout — and saves a PNG under `Saved/VibeUE/Captures` (path in the result).
`source="game"` is the ONLY way to screenshot the running game's HUD: Epic's `CaptureViewport`
sees the editor camera scene, never PIE or Slate UI. For a running game, `StartPIE` first.
**Look at the image, judge it against the request, fix, re-capture.** Reach for Epic's
`CaptureViewport` (via `vibeue.exec_tool`) only when you need its world-grid/actor-label overlay.

---

## 7. Diagnose performance

`PerformanceService` is VibeUE's net-new capability (the engine has no perf tooling). **STEP 0 is
always CPU-bound vs GPU-bound** — optimising the GPU does nothing on a CPU-bound frame:
```python
import unreal, json
print(unreal.PerformanceService.frame_timing())            # game/render/gpu ms + bound verdict — RUN FIRST
unreal.PerformanceService.start_trace("cap", "")           # Unreal Insights trace
# … reproduce the workload (ideally under PIE / standalone) …
unreal.PerformanceService.stop_trace()
print(unreal.PerformanceService.analyse("both", ""))       # frame stats + worst frames + log hitches
```
Load the `profiling` and `frame-rate` skills for the full drill-down.

**Unfocused editor = ~3 FPS.** Editor background throttling makes unattended PIE runs useless and
is NOT controlled by `t.IdleWhenNotInForeground` or `Slate.bAllowThrottling`. Disable it for the
session before automated verification — no window-focus (AppActivate) hacks needed:
```python
unreal.PerformanceService.set_background_throttling(False)   # full rate while unfocused/minimized
# ... run your PIE verification ...
unreal.PerformanceService.set_background_throttling(True)    # restore when done
```
To drive gameplay in PIE without OS focus or input-asset remapping:
`unreal.InputService.inject_action("/Game/Input/IA_Fire")` (Enhanced Input, one tick per call) and
`unreal.InputService.inject_key("SpaceBar")` (raw key via Slate).

---

## 10. Communication & working style

- **Be concise** — this is an IDE tool. Before each tool call, one sentence on what/why; after, 1–2 on
  the result. Execute multi-step tasks straight through — don't pause for "continue".
- **Discover before you call.** Method signatures come from `discover_python_class`, not memory or skill
  prose. Skills say *which* class and *why*; discovery gives the exact call shape.
- **Commit at milestones** if the project is a git repo, so a bad experiment reverts cleanly.
- **Living gotchas:** when you solve a real problem, append a one-line gotcha+fix to this file so the
  next session doesn't relearn it.

---

