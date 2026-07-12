# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A PS Vita "soloader" port of **Zenonia 3** (Android, ARMv6, Gamevil Nexus2/"Clet" engine). This is not a
reimplementation of the game: `loader/` builds a native Vita executable that loads the original
`libgameDSO.so` in memory, relocates it, and feeds it a minimal hand-built JNI environment (FalsoJNI) plus
GL/libc shims so the unmodified ARM game code runs directly on the console. There is no Android/Dalvik
layer at all — every Java-side behavior the engine depends on (asset loading, UI state callbacks, font
rendering, menu overlays) is reimplemented in C.

This project is a fork/port of a sibling `Zenonia2-vita` codebase for the same engine — `port_progress.md`
and `plan_zenonia3_port.md` explicitly diff against it for which bugs/fixes are expected to reappear vs.
which parts of the ABI actually changed between the two games.

## Build

```bash
./build.sh
```

This is the only supported build entry point (not bare `cmake`). It copies the repo to `/tmp/zenonia3-src`
before building — **required** because the real project directory has a space in its path
(`PSVITA Develop`), which breaks the VitaSDK toolchain/`vita-mksfoex`. The copy is an `rsync -a`, so it
always picks up local edits; the output VPK is copied back to `build/zenonia_3.vpk`. `build.sh` requires
`VITASDK` to be set (or installed at `/usr/local/vitasdk` or `~/vitasdk`) and ends with interactive
prompts for Vita3K/FTP install — safe to let those hit EOF/skip when running non-interactively, the VPK is
already written by that point.

Useful CMake options (pass as `-D...` to a manual `cmake` invocation, or edit the `option()` default in
`CMakeLists.txt`):
- `HIDE_VIRTUAL_GAMEPAD` (default `ON`) — hides the game's on-screen D-pad/action-button cluster (Vita
  physical buttons are already mapped 1:1). Toggling this only changes whether
  `zenonia_install_hide_dpad_hook()` (`loader/dynlib.c`) installs its hook.
- `ENABLE_VERBOSE_JNI_LOG` (default `OFF`) — logs every single FalsoJNI call. Only turn on to trace a
  specific JNI call; at `FALSOJNI_DEBUGLEVEL=0` the vast majority of the log becomes `[JNI]` noise and
  boot takes minutes (each line is a blocking `sceIoOpen`+`write`+`close`). Default level 2 still logs
  `[JNI WARN]`/`[JNI ERR]`, which is what actually diagnoses bugs.

If you edit or add any PNG under `zenonia3/res/drawable/` that feeds the Android-UI overlay system, rerun:
```bash
python3 tools/build_android_ui_assets.py
```
This regenerates the raw RGBA8888 `androidui/*.rgba` files that `CMakeLists.txt` packages into the VPK
(same mechanism as `font.ttf`). It requires Pillow.

## Deploy / debug loop

`manage_vita.py` is an interactive menu (no CLI flags) for the FTP-based deploy/debug cycle against a real
console running VitaShell's FTP server: upload the compiled VPK to `ux0:downloads/`, pull down the latest
`.psp2dmp` crash dump + log file, disconnect Proton VPN (routing conflicts with the console's local IP),
and run `clean_macos.sh` (strips macOS `._*` AppleDouble files, which break VPK installs). It hardcodes
`VITA_IP` at the top of the file — update it to match the actual console's address.

There is no emulator/CI test suite. Verification happens by installing the VPK on real hardware (or
Vita3K) and reading the resulting log file (`ux0:data/zenonia3/logs/log_<timestamp>.txt`) and/or
`.psp2dmp` crash dump. See the `so-crash-triage` skill for the exact cross-referencing procedure
(log → `.psp2dmp` via `vita-parse-core` → symbol via `objdump -T` → disassembly via
`arm-vita-eabi-objdump -M force-thumb` → matching pseudo-C in `decompiled_so/out_ghidra.c`).

## Repository layout and what's actually tracked

`.gitignore` deliberately excludes the original game's copyrighted material: `zenonia3/` (extracted APK
assets/libs), most of `zenonia3_java/` (jadx-decompiled Java sources — only kept for reference in a local
working copy), `decompiled_so/` (Ghidra output), `*.apk`/`*.zip`, `lib/` (vendored FalsoJNI/stb), and
`font.ttf`. A fresh clone of this repo is **not** buildable as-is — those trees must be reconstructed
locally from a legally-obtained copy of the game (see README's "Setup Instructions") before `build.sh`
will succeed. When working in this checkout they're already present; don't assume they'll survive being
committed or that a diff/`git status` will show changes inside them.

- `loader/` — the actual Vita executable source (see Architecture below).
- `lib/falso_jni/` — vendored FalsoJNI (fake JNI environment), `lib/stb/` — stb_truetype for font
  rasterization.
- `decompiled_so/out_ghidra.c` — Ghidra headless decompilation of `libgameDSO.so`. The primary source of
  truth for "what does the engine actually do here" — cited constantly in code comments by line number.
- `zenonia3_java/sources/` — jadx-decompiled Java (`com/gamevil/nexus2/Natives.java`,
  `com/gamevil/zenonia3/ui/ZenoniaUIControllerView.java`, `com/gamevil/nexus2/NexusHal.java`, etc.). The
  ground truth for JNI method names/signatures, UI state numbers, and Android `res/layout/main.xml` +
  `res/drawable/` positions that get replicated natively.
- `tools/build_android_ui_assets.py` — PNG → raw RGBA8888 converter for the Android-UI overlay system.
- `port_progress.md` — chronological log of every real bug found and fixed **on physical hardware**,
  written as it happens, with root-cause analysis (often down to a specific instruction or struct offset).
  This is the single most valuable file for understanding *why* the code looks the way it does — read it
  before assuming something is a bug rather than a deliberate workaround. `plan_zenonia3_port.md` is the
  upfront static-analysis plan (ABI diff vs. Zenonia 2) written before hardware testing began.
- `.claude/skills/` (via `psvita-port-toolkit/`) — `psvita-porting` (general Android→Vita soloader
  porting guidance) and `so-crash-triage` (the log+dump+decompile cross-referencing methodology). Both
  activate automatically for relevant work; `psvita-port-toolkit/PORTING_GUIDE.md` is the phase-by-phase
  checklist they're built from.

## Architecture

**Boot sequence** (`loader/main.c`): load `libgameDSO.so` via `so_file_load`/`so_relocate`/`so_resolve`
against `default_dynlib[]` (the libc/GL/pthread shim table in `loader/dynlib.c`) → apply one binary patch
(a `CMvLayerData::PreLoad` opcode fix, `ble`→`beq`, inherited as a known engine-class bug from Zenonia 2)
→ init vitaGL/audio/splash/Android-UI assets → `jni_init()` (FalsoJNI) →
`zenonia_install_array_hooks()`/`zenonia_install_hide_dpad_hook()` (runtime hooks into the *loaded game
binary*, not the loader itself — see below) → resolve the game's JNI entry points by name
(`Java_com_gamevil_nexus2_Natives_*`) → call them in a specific, hardware-crash-confirmed order:
`NativeInitWithBufferSize` **before** `NativeInitDeviceInfo` (the former triggers the engine's own
`Gcx_MM_Init` custom heap that the latter's buffer allocation depends on — calling them in Java's
declaration order crashes) → main loop: poll pad/touch → translate to the engine's own event protocol →
`handleCletEvent` → `NativeRender` → draw whatever Android-overlay art `androidui.c` owns for the current
UI state → `vglSwapBuffers`.

**No JNI method registration exists in the game binary** (`JNI_OnLoad` only stores the `JavaVM*`) — every
native method the engine calls into Java is resolved by name convention through FalsoJNI
(`loader/java.c`'s `nameToMethodId[]`), matching how the real Dalvik runtime would `dlsym` it. A method
simply *not being in that table* is a valid, common failure mode: FalsoJNI returns a type-appropriate
default (`-1` for un-registered int methods, `NULL` for objects, `false`/no-op otherwise) — sometimes
that's harmless, sometimes it's a real crash waiting one call away (see `port_progress.md`'s
`GFA_CreateFont`/`GFA_DrawFont` writeups for a worked example of tracing exactly which case applies by
reading the decompiled caller, not guessing).

**The engine draws almost none of its own menu/UI chrome.** In the original APK, the logo, title screen,
main menu, ABOUT/HELP/reply-page panels, and the on-screen D-pad/action-button cluster are all Android
`ImageView`/`ImageButton`/`WebView` widgets layered *above* the `GLSurfaceView` in `res/layout/main.xml` —
the native `.so` only ever draws a game-logic-relevant background fill underneath them. Since this loader
has no Android/View layer, `loader/androidui.c` reimplements each of those screens as hand-drawn GL quads,
keyed off the engine's own `g_ui_status` (`OnUIStatusChange` JNI callback in `java.c`) using the exact
same position math as the original Java (`Natives.java`/`ZenoniaUIControllerView.java`), and
`loader/main.c`'s touch-dispatch intercepts taps against those same synthetic hitboxes instead of
forwarding raw pointer events to the engine (which never had a listener registered on the real widget in
the first place). `loader/htmltext.c` (a small special-purpose HTML→plain-text extractor, *not* a general
parser — tuned to the specific `<tr><td>` markup of the two real `about.html`/`help_eng.html` assets) and
`loader/htmlview.c` (word-wraps and rasterizes that text once into a single scrollable GL texture using
the GFA font backend) exist to reproduce what was originally rendered by a `WebView` on those two screens.
Extending this system to a new UI state means: find the matching `Natives.show*Component()`/
`ZenoniaUIControllerView` code in `zenonia3_java/sources/`, get real pixel positions from there (never
invent them), and add a branch in `androidui_draw()`/the touch dispatch.

**Runtime hooking of the loaded game binary** (as opposed to the loader's own shim functions) uses
`so_util.c`'s `hook_thumb`/`hook_arm`/`hook_addr` — a full function-body replacement (patches the target's
first instructions to jump into a C function), not a trampoline. The one non-trivial example is
`zenonia_install_hide_dpad_hook()` in `loader/dynlib.c`: it hooks `GVUIPlayerController`'s constructor
(found via `so_symbol()` against the *dynamic* symbol table — this stripped binary still exports enough
C++ mangled symbols for this to work), lets the real constructor run, then zeroes the "active object
count" field the *unmodified, un-hooked* `GVUIController::Draw()`/`PointerPress()`/etc. use as their loop
bound — disabling draw+touch for that one controller instance without touching any other UI controller or
any GL state. This pattern (find the real struct offset/behavior in `decompiled_so/out_ghidra.c`, prefer
a targeted state mutation over patching shared code) is the preferred approach over hooking a function
that's shared across multiple unrelated call sites.

**GLES1 fixed-function compatibility shims** live in `loader/dynlib.c`: RGB565→RGBA8888 texture upload
conversion, forced min/mag texture filters (engine sometimes leaves textures "incomplete"), and deferred
`GL_FIXED` (Q16.16)→float conversion for vertex/color/texcoord arrays (the engine, being a very old
NDK build, still emits fixed-point vertex data that vitaGL doesn't accept directly). These wrapper
functions are what actually get linked in via `default_dynlib[]` in place of the real GL entry points the
`.so` imports.

**Everything is driven by real evidence, not assumption** — this is a stated project convention, not just
a description: `port_progress.md` repeatedly emphasizes fixing "one confirmed bug per hardware log" rather
than batching speculative fixes. When touching engine-interaction code (JNI stubs, UI state handling,
input mapping, memory layout assumptions), cross-reference `decompiled_so/out_ghidra.c` and
`zenonia3_java/sources/` rather than porting a fix from Zenonia 2 (or elsewhere) unverified — the ABI diff
table in `plan_zenonia3_port.md` exists precisely because several things *look* identical between the two
games' engines but aren't.
