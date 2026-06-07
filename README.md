# ExamplePlugin — POE2Fixer Plugin SDK Example

A reference plugin demonstrating all features of the POE2Fixer Plugin SDK (v6). Use this as a starting point for building your own plugins.

## What It Does

ExamplePlugin provides interactive demo tabs that showcase every SDK capability. When enabled in POE2Fixer, it adds a settings panel with the following example tabs:

### Buff Inspector
Displays the player's active buffs in real-time. Shows buff name, remaining time, charges, flask slot, and effectiveness. Includes text filtering and color-coded progress bars for buff duration.

### Entity Explorer
Lists all game entities (monsters, NPCs, chests, players, items) with their type, rarity, and nearby zone classification. Supports the entity watch mechanism — select any entity to inspect its full component data (Life, Render, Positioned, Targetable, Animated, Stats, Actor, Buffs) in a detailed tree view.

### Inventory Inspector
Accesses the ServerData debug interface to browse player inventories. Displays inventory slot grids with item details: base type, unique name, rarity, item level, mods (implicit, explicit, enchant, hellscape), identified/corrupted status, and stack sizes.

### Memory Viewer
Demonstrates direct game memory reading through the SDK. Includes a hex dump viewer at arbitrary addresses, typed `Read<T>` examples, pattern scan address resolution, and StdVector/StdList/StdMap container reading.

### UI Explorer
Navigates the game's full UI element tree. Shows element properties (position, size, flags, type, scale, StringId, text content), recursive parent chain, and child traversal. Includes search by StringId and visual highlighting of selected elements on screen.

### Component Reader (SDK v6)
Demonstrates the SDK v6 Component Reader API and UI Element API. Exercises all 22 component readers, 4 enumerators, and 10 convenience helpers directly from entity addresses without hardcoded offsets. Shows UI tree navigation using `GetUiChildren`, `ReadUiElement`, and `ComputeUiScreenRect`. Demonstrates helpers like `GetHealthPercent`, `IsAlive`, `GetPlayerName`, `GetItemRarity`, `IsItemIdentified`, `GetStackCount`.

### Render, Terrain, Events, Log tabs
Exercise the remaining service surface: WorldToScreen + map projection, walkable/height grid + TgtLocation enumeration, OnFrame/OnAreaChange/OnGameAttached/OnGameDetached event hooks with live counters, and Log.Debug/Info/Warn/Error fire buttons.

### SDK Coverage Summary
The settings tab shows a `SDK Coverage: N/10 services responding` banner above the tab bar. Hover for per-service status (Game, Entities, Components, Inventory, Ui, Render, Terrain, Memory, Log, Events).

## Build

**Requirements:** Visual Studio 2022 (MSVC v143), Windows SDK 10.0, C++20

Open `ExamplePlugin.sln` in Visual Studio and build **Release | x64**.

Or from command line:
```
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" ExamplePlugin.sln -p:Configuration=Release -p:Platform=x64
```

Output: `bin\Release\ExamplePlugin.dll`

## Install

Copy `ExamplePlugin.dll` to `Plugins/ExamplePlugin/` in your POE2Fixer directory, then enable it in the Plugins tab.

## Project Structure

```
sdk/            Plugin SDK headers (PluginAbi.h, PluginSDK.h)
imgui/          ImGui library (headers + sources, compiled into the DLL)
examples/       Example tab implementations
ExamplePlugin.cpp   Main plugin entry point — routes to example tabs
```

## Creating Your Own Plugin

1. Copy this project as a template
2. Rename the .sln, .vcxproj, and main .cpp
3. Subclass `PluginSDK::Plugin` and override the lifecycle hooks (see `ExamplePlugin.cpp`)
4. Use `ctx()->Service.Method(...)` to access game data (Game, Entities, Components, Inventory, Ui, Render, Terrain, Memory, Log, Events)
5. Build and copy the DLL to `Plugins/YourPlugin/`
