# RCA — The Plugin That Compiled Perfectly and Refused to Load

**Milestone:** M1 (the throwaway FFmpeg spike)
**Date:** 2026-09-09
**Status:** Resolved
**Base document for a devlog write-up — not a spec.**

---

## TL;DR

An Unreal plugin that wraps FFmpeg built cleanly, linked cleanly, and then
failed to load at editor startup with `Missing import: avutil-61.dll`. Every
layer of the build reported success. The delay-loading mechanism the whole
design depended on had silently never engaged — because the FFmpeg
distribution's `.lib` files are **GNU-format import libraries** (BtbN
cross-compiles with MinGW), and MSVC's `/DELAYLOAD` can only transform
**MSVC short-import-format** libraries. The linker was handed the correct
flags, accepted them, and quietly ignored them. No warning, no error.

Fix: regenerate the four import libraries from the `.def` files that ship
alongside them, using MSVC's own `lib.exe`. Four commands. The delay-load
design was never wrong — it just wasn't running.

---

## 1. Context — why delay loading was in the design at all

The plugin ships FFmpeg as **shared DLLs, dynamically linked** (LGPL
compliance — dynamic linking satisfies the library-substitution condition
automatically, static linking does not). Those DLLs live inside the plugin's
own `ThirdParty/FFmpeg/bin/Win64/` folder rather than being installed
system-wide, because a reviewer cloning the repo should be able to build and
run without a separate install step.

That creates a problem: Windows doesn't know to look inside a plugin folder
for DLLs. The standard Unreal answer, decided during concept prep weeks
before any code was written, is:

1. Mark the DLLs as **delay-loaded** (`PublicDelayLoadDLLs` in the module's
   `.Build.cs`), so Windows' loader does *not* try to resolve them when the
   module binary loads.
2. At module startup, explicitly push the plugin's DLL directory onto the
   search path and call `FPlatformProcess::GetDllHandle` on each one — taking
   manual control of *when* and *from where* they load.

Without step 1, step 2 can never run: you cannot execute startup code inside
a DLL that Windows refused to load in the first place. That dependency is
the entire shape of this bug.

---

## 2. The symptom

The project compiled with zero errors or warnings. Launching the editor threw
a dialog saying the `IPStreamMedia` plugin could not be loaded. Relaunching
in `DebugGame Editor` for a full log gave:

```
LogWindows: Failed to load '.../UnrealEditor-IPStreamMedia-Win64-DebugGame.dll' (GetLastError=126)
LogWindows:   Missing import: avutil-61.dll
LogWindows:   Missing import: avcodec-63.dll
LogWindows:   Missing import: avformat-63.dll
LogWindows:   Missing import: swscale-10.dll
LogWindows:   Looked in: ../../../Engine/Binaries/Win64
LogWindows:   Looked in: F:\...\Plugins\IPStreamMedia\Binaries\Win64
   ... (~170 more search paths)
LogPluginManager: Error: Plugin 'IPStreamMedia' failed to load because module
                  'IPStreamMedia' could not be loaded.
```

`GetLastError=126` is `ERROR_MOD_NOT_FOUND`.

**Why this error is misleading in the most expensive possible way:** it names
four DLLs as missing, and those four DLLs are *sitting right there on disk*,
exactly where the build system was told to find them. The natural response is
to go hunting for the DLLs, or for a path bug. Both are dead ends. The error
is telling the literal truth — Windows really can't find them *on the paths
it searches* — while implying entirely the wrong question. The right question
is not "where are the DLLs" but **"why is Windows looking for them at load
time at all, when they're supposed to be delay-loaded?"**

That reframing took the longest, and it's the actual lesson of the whole
exercise.

---

## 3. Investigation, including the wrong turns

### 3.1 First theory: stale binary (wrong)

`.Build.cs` changes don't always force a relink; UBT's own log said
`Target is up to date` right after re-running the build rules. Plausible
theory: the DLL on disk predated the delay-load configuration.

**Refuted by file timestamps.** After a clean rebuild (deleting the plugin's
`Binaries/` and `Intermediate/`), the module DLL's mtime was ~30 minutes
*newer* than the captured log — the binary was fresh, the log was stale. Worth
noting as its own small lesson: when someone says "same error," confirm the
evidence was re-captured after the change, not just that the symptom looked
identical.

### 3.2 Two reasonable experiments that couldn't have helped

Changing the factory module's `LoadingPhase`, and adding `AdditionalDependencies`
to the `.uplugin`. Neither made a difference, and in hindsight neither could:
both operate at the plugin/module *orchestration* layer, while the failure was
in the OS loader resolving a PE import table — several layers below. Reverted
afterward. (The factory's `PostConfigInit` phase in particular is a deliberate
design decision about player-registration ordering and should not have been
collateral damage of a debugging session.)

### 3.3 Adding logging to the `.Build.cs` — useful, but not the way expected

Printing every `PublicAdditionalLibraries.Add` / `PublicDelayLoadDLLs.Add` /
`RuntimeDependencies.Add` call proved the build script executed correctly with
the right values. That **eliminated an entire hypothesis class** (typo, wrong
path, block not executing) without finding the bug. Worth doing; worth knowing
what it did and didn't prove.

### 3.4 Reading the engine's own diagnostic code

Rather than guessing what "Missing import" meant, the check was to read the
engine source that emits it —
`Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformProcess.cpp`:

```cpp
// ReadLibraryImportsFromMemory(), ~line 1932
IMAGE_DATA_DIRECTORY *ImportDirectoryEntry =
    &NtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
```

It reads `IMAGE_DIRECTORY_ENTRY_IMPORT` — the **regular** import directory —
and never touches `IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`.

This is the pivot point of the whole investigation. It means the message is
not a generic "couldn't find a DLL" complaint: for `avutil-61.dll` to appear
in that list *at all*, it must be a **regular, load-time import** of the
built binary. If delay loading were working, those DLLs would be in a
different PE directory and this diagnostic could not have printed them.

So: either the linker never applied `/DELAYLOAD`, or it was never asked to.

### 3.5 The linker response file — it *was* asked to

UBT writes the exact link command line to a `.rsp` file under
`Intermediate/Build/.../IPStreamMedia/`:

```
/DELAYLOAD:"avutil-61.dll"
/DELAYLOAD:"avcodec-63.dll"
/DELAYLOAD:"avformat-63.dll"
/DELAYLOAD:"swscale-10.dll"
"...\ThirdParty\FFmpeg\lib\Win64\avutil.lib"
...
"delayimp.lib"
```

Correct flags, correct DLL names, `delayimp.lib` (the delay-load helper)
linked. So the `.Build.cs` → UBT → linker chain is entirely correct — which
means the bug is *below* the build system, in the linker's own behavior.

### 3.6 The binary itself — the linker ignored them

```
> dumpbin /DEPENDENTS UnrealEditor-IPStreamMedia-Win64-DebugGame.dll

  Image has the following dependencies:
    avutil-61.dll
    avcodec-63.dll
    avformat-63.dll
    swscale-10.dll
    UnrealEditor-Core.dll
    ...
```

No `Image has the following delay load dependencies:` section at all. The
linker received `/DELAYLOAD`, linked `delayimp.lib`, emitted no warning, and
produced ordinary load-time imports anyway.

### 3.7 Ruling out the usual suspect

MSVC genuinely *cannot* delay-load **data** imports (only functions), and a
single imported variable is enough to force a DLL back into the regular
import table. Checked:

```
> dumpbin /IMPORTS ...

    avcodec-63.dll
        4F av_packet_alloc
        52 av_packet_free
        67 av_packet_unref
        81 avcodec_alloc_context3
        93 avcodec_find_decoder
        ... (functions only)
```

Every import is a function, by name. Not the cause. No `LNK4199`
("/DELAYLOAD:x ignored; no imports found") appeared in the build log either —
the failure was completely silent.

### 3.8 Root cause — the import libraries are the wrong format

The last remaining suspect was the import libraries themselves:

```
> dumpbin /ARCHIVEMEMBERS avutil.lib

  libavutil_avutil_lib_t.o
  libavutil_avutil_lib_h.o
  libavutil_avutil_lib_s00638.o
```

Those `_t.o` / `_h.o` / `_s#####.o` names are GNU **`dlltool`** output —
tail, head, and stub objects. This is a MinGW/GCC-produced import library
containing full COFF objects with explicit jump thunks in `.text`.

An MSVC import library is structurally different: it's built from **short
import records**, a compact per-symbol format the MSVC linker recognises and
can rewrite into delay-load thunks. Counting them:

| Library | Short-import records | Recorded DLL name |
|---|---|---|
| `avutil.lib` as shipped (BtbN) | **0** | — |
| Regenerated via `lib.exe /DEF:` | **639** | `avutil-61.dll` |

**That is the root cause.** BtbN's FFmpeg Windows builds are cross-compiled
with MinGW/GCC, so their `.lib` files are GNU-format import libraries. MSVC's
`/DELAYLOAD` transformation only applies to short-import records; handed
GNU-style thunk objects, the linker links them as ordinary imports and
ignores the flag — without a diagnostic.

*(Stated at the level the evidence supports: this was demonstrated
empirically on this project — same `.def`, same linker, GNU-format library →
no delay load; regenerated short-import library → delay load works. The
mechanism explanation is consistent with MSVC's documented behaviour, but
the empirical result is the load-bearing part.)*

### 3.9 The failure chain, end to end

1. Vendor import libraries are GNU-format.
2. MSVC silently ignores `/DELAYLOAD` for them.
3. The four FFmpeg DLLs end up as regular, load-time imports.
4. Windows therefore demands all four *before* it will load the module DLL.
5. They live in the plugin's `ThirdParty/` folder, which is on none of the
   loader's search paths.
6. `LoadLibrary` fails with `ERROR_MOD_NOT_FOUND`.
7. The module DLL never loads, so `StartupModule()` never runs —
8. — and the `PushDllDirectory` + `GetDllHandle` code written specifically
   to solve step 5 never gets the chance to execute.

The design was correct. It just never ran.

---

## 4. The fix

Regenerate MSVC-format import libraries from the `.def` files that ship in
the same folder as the `.lib` files, using MSVC's `lib.exe`:

```
cd /d "...\Plugins\IPStreamMedia\ThirdParty\FFmpeg\lib\Win64"
lib /DEF:avutil-61.def   /OUT:avutil.lib   /MACHINE:X64 /NAME:avutil-61.dll
lib /DEF:avcodec-63.def  /OUT:avcodec.lib  /MACHINE:X64 /NAME:avcodec-63.dll
lib /DEF:avformat-63.def /OUT:avformat.lib /MACHINE:X64 /NAME:avformat-63.dll
lib /DEF:swscale-10.def  /OUT:swscale.lib  /MACHINE:X64 /NAME:swscale-10.dll
del *.exp
```

**`/NAME:` is not optional.** The shipped `.def` files contain a bare
`EXPORTS` list with no `LIBRARY` statement, so without `/NAME:` `lib.exe`
derives the DLL name from the `/OUT:` filename and records `avutil.dll` — a
DLL that does not exist. The result would link and then fail at runtime in a
new and more confusing way.

No source changes were needed. `FFmpeg.Build.cs`, the module startup code,
and the plugin descriptor were all already correct.

### Verification

```
> dumpbin /DEPENDENTS UnrealEditor-IPStreamMedia-Win64-DebugGame.dll

  Image has the following delay load dependencies:
    avutil-61.dll
    avcodec-63.dll
    avformat-63.dll
    swscale-10.dll
```

The four DLLs moved out of the regular dependency list into a delay-load
section — checked *before* relaunching the editor, so a pass/fail was known
without spending a launch cycle on it. Editor then loaded the plugin cleanly.

---

## 5. Alternatives considered and rejected

**B — Abandon delay loading; copy the DLLs next to the module binary.**
Unreal's loader does search the plugin's own `Binaries/Win64` folder (visible
in the failure log's search-path list), so switching `RuntimeDependencies` to
copy the DLLs there at build time would have made the load-time imports
resolve, no import-library work required. Rejected because it works *around*
a deliberate architectural decision rather than fixing it: `PublicDelayLoadDLLs`
would become a no-op, the explicit startup-loading code would become dead and
have to be deleted, and two design documents would have to be rewritten to
describe a weaker mechanism. It is a legitimate approach that many shipping
plugins use — it was simply the wrong trade here, given the fix for the real
cause was four commands.

**C — Runtime-only linking: `LoadLibrary` + `GetProcAddress` for every
function.** Bypasses import libraries entirely, so the format problem
disappears. Rejected: ~21 manually declared function pointers for the spike
alone, growing with every FFmpeg call added later, to buy nothing the fixed
import libraries don't already provide. (This option had already been
considered and rejected during concept prep, on the assumption delay loading
worked. The assumption turned out to be false; the conclusion still holds.)

---

## 6. What generalises

- **Configuration is not behaviour.** Every layer reported success — the build
  script ran with correct values, UBT emitted correct flags, the linker
  accepted them, the build succeeded. The mechanism still never engaged.
  Verifying that a setting was *applied* is not the same as verifying it
  *took effect*, and on this bug those two things diverged silently.
- **Read the tool's source before trusting your reading of its output.**
  Knowing that Unreal's diagnostic inspects only the regular import directory
  converted a vague "missing DLL" message into a precise statement: *these are
  regular imports*, which is what made the contradiction visible.
- **Inspect the artifact, not just the recipe.** The response file and the
  built DLL disagreed. Only `dumpbin` on the actual binary settled it.
- **Third-party binaries carry their build system's assumptions with them.**
  A prebuilt library isn't neutral: this one was cross-compiled with a
  different toolchain than the one consuming it, and the incompatibility
  surfaced not at build time but three layers away at runtime, in a
  completely unrelated-looking symptom.
- **A silent ignore is worse than an error.** One `LNK4199`-style warning
  would have collapsed this entire investigation into about two minutes.

---

## 7. Follow-up actions

- [x] Regenerate the four import libraries; verify delay-load section present.
- [x] Record provenance in `ThirdParty/FFmpeg/NOTICE.md` — the shipped import
      libraries were GNU-format and were regenerated with `lib.exe`, including
      the exact commands, so that re-downloading the same FFmpeg build does not
      silently reintroduce this.
- [x] Revert debugging scaffolding: `Logger.LogInformation` calls and the
      commented-out platform guard in `FFmpeg.Build.cs`; `AdditionalDependencies`
      and the factory `LoadingPhase` change in the `.uplugin`.
      *(One cosmetic leftover: an unused `using Microsoft.Extensions.Logging;`
      in `FFmpeg.Build.cs`.)*
- [x] Note in `Architecture.md §9` that delay loading depends on the import
      library format, with a pointer to this document.
- [ ] **Still untested:** the spike's actual pass criterion. The plugin now
      loads; `GrabOneFrame` has never been run against the camera. M1 is not
      passed until a frame is on a plane within 8 seconds and stopping PIE
      returns cleanly.
