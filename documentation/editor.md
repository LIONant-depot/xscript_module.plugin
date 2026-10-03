# Script modules and the game DLL

A script module is a ScriptModule resource: source files plus a **descriptor** that says what the module is made of. The descriptor (`Descriptor.txt`) is the single source of
truth: the resource pipeline compiles each module into its CMake file, and the project's **Game resource** (`plugins/xgame.plugin`, see its `documentation/game.md`) turns the
CMake files of the modules it lists into the game's project. A module is part of the game when the Game resource lists it.

```
<resource>.desc/info.txt         what every resource has
<resource>.desc/Descriptor.txt   the descriptor: Files, Libraries, Defines
<resource>.desc/source_db/       the files, subfolders allowed
<resource>.desc/module.cmake     (optional) the module exported for a project without the editor
```

## The descriptor

| Property | Meaning |
|---|---|
| `Files[]` | each `Path` (relative to `source_db`, forward slashes, no `..`) and `Exclude` (kept in the module, left out of the build). **Only what is listed is built.** |
| `Libraries[]` | third-party libraries: `Name`, `IncludeDirs`, `LibDirs`, `Libs`, `Defines`, `RuntimeFiles` (DLLs copied next to the loaded `Game.dll`). Every path is relative to the project; absolute paths are refused. |
| `Defines[]` | preprocessor defines for the whole module (`NAME` or `NAME=value`) |

A module that has no `Descriptor.txt` (it predates the descriptors) gets one written from its `source_db` folder the first time the editor or the generator needs it. A file that
is in the folder and not in the descriptor is shown as *not in the module* and is not built until it is added (Rescan, or *Include in Module*); a listed file that is not on disk is
*missing*, reported in the Logs and never dropped silently.

## The generated game project

```
<project>/Cache/Script/CMakeLists.txt        made by the compile of the Game resource, from the CMake files of its modules; never checked in
<project>/Cache/Script/runtime_files.txt     the DLLs the libraries bring (written by the configure; the loader copies them next to the loaded Game.dll)
<project>/Cache/Script/Build/                the Visual Studio solution (Script.sln) and its intermediate files
<project>/Cache/Script/Loaded/               the copies of Game.dll that are actually loaded (Game_loaded_<n>.dll, Game.pdb, the runtime files)
<project>/Cache/Resources/Platforms/WINDOWS/Game.dll    the compiled resource
```

The editor does not make the project: the resource pipeline does, in the background, when a module's descriptor or the Game's list changes (and after a cleared cache, with or
without the editor). The editor builds `Game.dll` when it is older than what it is built from (the descriptors, the files they list, the project, or the two files in `source/Runtime`)
at startup, when the window gets the focus back and when Play is pressed; the build first waits for the pipeline to have made the project (and says so when a compile failed).
It configures CMake only when the project changed - a new, removed or renamed file, a library, a module - so editing a `.h` or a `.cpp` only runs MSBuild. It never loads
`Game.dll` itself but a numbered copy, so a rebuild never fights a file that is mapped. Builds run in the background and a failed one leaves the loaded generation untouched.

## The compiler of the module (Descriptor.txt to CMake)

A ScriptModule is a resource like any other, so the resource pipeline compiles it: `xscript_module_compiler.exe` (built from `source/Compiler`, by `build/CreateAndBuildProject.bat`, and
by the root `CMakeLists.txt` of xLION when it is missing) reads `Descriptor.txt` and writes the module's CMake file as the compiled resource:

```
<project>/Cache/Resources/Platforms/WINDOWS/ScriptModule/xx/yy/<guid>      the CMake file: <MODULE>_SOURCES, _HEADERS, _PCH_HEADERS, _INCLUDE_DIRS, _LIB_DIRS, _LIBS,
                                                                           _DEFINES, _RUNTIME_FILES and the function <module>_apply(target)
<project>/Cache/Resources/Logs/ScriptModule/xx/yy/<guid>.log/Log.txt       what the compile said (a listed file that is not on disk is a warning; a descriptor that does not validate fails)
```

It is the same text *Export CMake* writes (`xscript_module_export.h`), so there is one way to turn a descriptor into CMake. `<module>_apply(target)` also puts the files in a folder of the module's name in Visual
Studio's Solution Explorer, with the module's own subfolders (`source_group(TREE ...)`).

**What the compile depends on is the descriptor and nothing else.** The pipeline recompiles a resource when its descriptor, or one of the files its `dependencies.txt` lists, is newer than the compiled resource. The
compiler lists no dependency, and the files of `source_db` are not inputs of it (what the descriptor *lists* is the input, and that is in `Descriptor.txt`), so editing a `.h` or a `.cpp` never compiles the
module and never writes the CMake again. Adding, removing, renaming, moving or excluding a file, or changing a library or a define, changes the descriptor and does.

The compiled resource is stamped with the time the compile *began*, not the time it ended: a descriptor saved while the compile runs is newer than it and is compiled after it.

The Compile button of the module editor (and `Compile`) saves the descriptor and queues this compile, like it does for every other resource; `CompileStatus` says how it went.


## The module editor

Open a ScriptModule asset (double click in the asset browser). Windows, all docked inside the editor:

- **Sources** - the tree, like Solution Explorer. Folders first; each row has its source control state (clean, modified, untracked, conflicted; a folder shows a dot when something under it
  changed) and lock. *Add > New File / New Folder*, *Rename* (F2), *Delete* (Del, with confirmation), drag to move, *Exclude from Build*, *Include in Module*, *Open in Visual Studio*, *Show in
  Explorer*, *Copy Path*, *Source Control > Stage / Revert / Lock / Unlock*. A double click or Enter opens the file. The box filters; *Changes only* shows what source control or the module
  says is not in step.
- **one window per open file** - read-only C++ with line numbers and highlighting; the lines the compiler complained about are marked (the problems of the Logs whose site is the file),
  and the viewer shows the file again when Visual Studio saves it. **A file has one viewer, ever**: opening an open file brings its tab to the front. **Ctrl + the mouse wheel** over the code zooms its text (one pixel for each notch, 6 to 64; each viewer keeps its own size). The code is written in Visual Studio.
  The Logs' *Open source* and F8 land here, at the line, for the files of an open module.
- **Overview** - what the module is made of, what is wrong with it (missing and unlisted files, validation errors), whether it is part of the game, how the last build went, *Export CMake*.
- **Libraries** - the libraries and defines (the descriptor's own properties), with *+ Add library* and *+ Add define*.

### Commands

Run as `<module name>\Command` (see `list`); every edit is undoable, and the descriptor and the disk change together.

| Command | |
|---|---|
| `AddFile -Path p [-Template header\|source\|empty]` | a new file (a header starts with `#pragma once`, a source file includes its own header), or a file of the folder that was not listed |
| `RemoveFile -Path p` / `RemoveFolder -Path p` | delete (undo restores the exact bytes and the place in the list; a folder takes everything in it) |
| `RenameFile -Path p -To q` / `RenameFolder -Path p -To q` | rename or move; the viewers follow |
| `NewFolder -Path p` | a folder |
| `ExcludeFile -Path p [-Value true\|false]` | keep a file in the module but out of the build |
| `Rescan [-RemoveMissing true]` | add the files that are in the folder and not in the module; drop the ones that are gone |
| `ListFiles` | path, kind, excluded, state (`ok` `missing` `unlisted`), source control, lock, open, bytes |
| `OpenFile -Path p [-Line n]` / `CloseFile -Path p` / `ListOpenFiles` | the viewers (one per file); the list says the text size and where the code is on the screen |
| `ZoomFile -Path p [-By notches] [-Size pixels]` | the text size of a viewer, as the wheel does (`-Size 0` = the default) |
| `ListTree` / `SelectFile -Path p` | the rows the tree drew (with their screen positions), and select one |
| `ExportCMake [-File path]` | `module.cmake`: include it in a project that builds the game without the editor and call `<module>_apply(<target>)`; paths in it are relative to the file |
| `SetProperty`, `ListOp`, `ListProperties`, `Save`, `Undo`, `Redo` | the descriptor's own properties (libraries, defines): `SetProperty -Path "ScriptModule/Libraries[G:0]/Name" -Value "Box2D"` |

The workspace has the same file operations for a module that is not open, addressed by `-Library` and `-Asset`: `AddScriptSourceFile`, `RemoveScriptSourceFile`,
`RenameScriptSourceFile`, `SetScriptSourceFileContent`, `ListScriptSourceFiles`, `RescanScriptModule`.

## source/Runtime

What the generated project compiles in, and what module authors include:

| File | What it is |
|---|---|
| `xscript_registration.h` | `XSCRIPT_REGISTER_COMPONENT(type, category, priority)` and `XSCRIPT_REGISTER_SYSTEM(type)`: a module announces its components and systems through a self-registering list, so it is registered just by being compiled into the DLL. `XSCRIPT_USES_COMPONENT(type)` declares a component of another binary (the engine's `xlioncore::transform`) that the module's systems query: component type information is per binary, so the entry syncs this DLL's copy before the systems are registered (forget it and the first system that queries the type crashes the editor); the entry also syncs the engine's built-in components (`entity`, ...) for every module. To use the TYPES of an engine component that registers itself (the physics ones), include its header between `#define XSCRIPT_IMPORT_ONLY` and `#undef XSCRIPT_IMPORT_ONLY`: the module gets the type and its reflection but does not register it a second time, then name each one with `XSCRIPT_USES_COMPONENT` |
| `xscript_game_entry.cpp` | the DLL's entry points (`XecsPlugin_RegisterComponents`, `XecsPlugin_RegisterSystems`, `XecsPlugin_Unregister`) and `XScript_GetComponentDisplayInfo`, which hands the editor each component's guid, name, category and priority |

A module includes `plugins/xscript_module.plugin/source/Runtime/xscript_registration.h`. The DLL links the xECSV2 import
library of the editor's own build, so it shares the editor's one component registry.

## Where the code is

`source/Module` - the descriptor, the path rules, the folder model and the operations (no UI: the generator, the editor and the commands share them). `source/Editor` - the module editor,
its commands and the file viewer. The generator of the game project and the loader are in the Level plugin (`game_module/LevelEditor_GameModuleSources.h`, `LevelEditor_GamePluginLoad.h`).

## Which module defines a component or a system

All the modules of a Game compile into one `Game.dll`, so the editor has to be told which module each component and system belongs to. `XSCRIPT_REGISTER_COMPONENT` and `XSCRIPT_REGISTER_SYSTEM` record
`__FILE__` - the header or source file that *defines* the type, which is the same for every module that includes it - and the DLL exports it (`XScript_GetRegistrations`, next to
`XScript_GetComponentDisplayInfo`; a DLL built before it existed has none and its modules are unknown). The path of a module's file runs through
`.../ScriptModule/xx/yy/<guid>.desc/source_db/...` (`xscript::module::ModuleGuidFromSourcePath` folds `..` and checks that the two byte folders belong to the guid), so the module is read from it; the project is
compiled with `/FC` so the compiler gives absolute paths. A type whose file is in no module (an engine header included without `XSCRIPT_IMPORT_ONLY` registers its components a second time) belongs to none; the editor says so in
the log, and also when the DLL defines types of a module the Game does not list (a module includes one of its headers: list it).

`ListModuleRegistrations [-Module <asset guid>]` shows it: kind, guid, name, module, file relative to `source_db`.

The editor spends this where a person looks (all of it comes from one place, `xscene::type_source`, filled from the registrations):
- the **component header** of the entity inspector carries the module's name as a quiet tag next to the buttons; hover says the module and the file, a click opens the file in the module's editor;
- the **Add Component** list shows the module and the file in the hint of each component, and its search finds components by the name of their module;
- the **System Registry** shows the module at the right of each system, in the system's hint, and "Open <file>" in the right-click menu; the chips of the systems running on an entity and the tag components say it in their hints;
- `OpenTypeSource -Guid <hex16> [-System true]` does what the click does, for the AI and the tests. A type of the engine (no module) says it is built in.

## Scenes need modules

A scene's `ComponentDeps.txt` has one row per component it uses, with the module that defines it (`Module`: the script module's guid, `0` for the engine's and the editor's own components, `FFFFFFFFFFFFFFFF` when nobody
could say). The editor writes it on every save of the scene; a file written before modules were tracked has no column and reads as unknown. A Game is *compatible* with a scene when it lists every module the scene needs
(and the scenes it depends on need): a comparison of files, so it works before anything is built.

| command | |
| --- | --- |
| `ListSceneModules -Scene <hex16> [-Own true]` | the modules a scene needs and the components it uses from each |
| `CheckGameCompatibility (-Scene <hex16> \| -Level <hex16>) [-Game <asset guid>]` | does the Game list them? Without `-Game`, every Game of the project answers; the message names the module and the components |
| `ListScenesUsingModule -Module <asset guid>` | the scenes that use a module's components |

`RemoveProjectModuleReference` is refused when a scene that is open uses the module's components; it no longer builds the game to find out. A save of a scene keeps the entities that could not be loaded (their component is not
registered because the module is not loaded) instead of forgetting them.
