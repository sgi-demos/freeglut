# freeglut SDL2 / OpenGL ES 2.0 backend — architecture

This document describes the SDL2 platform backend for freeglut and the
OpenGL ES 2.0 support that goes with it. It covers what was added, how it
fits into freeglut's existing structure, the design decisions behind the
non-obvious parts, and the known limitations.

## Goals

The target was a freeglut that runs with **no OS-specific windowing code**:
SDL2 for windowing, input, and context creation; OpenGL ES 2.0 for all
rendering, *including* the GUI (pop-up menus and bitmap/stroke fonts). The
two verification targets are **macOS** (ES2 via ANGLE) and **Emscripten**
(ES2 maps directly to WebGL1). The desktop X11/Win32/Cocoa backends are
untouched and continue to build and behave as before.

## Layering

```
   application  (talks raw GLES2 / WebGL2)
        |
   freeglut public API  (glut*, unchanged)
        |
   freeglut common core  (fg_*.c — window/menu/font/state logic, unchanged)
        |        \
        |         \—— immediate-mode GUI calls (menus, fonts)
        |                    |
        |             fg_gles2_compat  (tiny GL 1.x→ES2 shim, GUI-only)
        |                    |
   fgPlatform* contract      |
        |                    |
   src/sdl2/  (SDL2 backend) |
        |                    |
       SDL2 ————————————— OpenGL ES 2.0
```

Two distinct pieces were added:

1. **`src/sdl2/`** — a platform backend implementing freeglut's
   `fgPlatform*` contract on top of SDL2 (windowing, events, timing,
   cursors, joystick, game mode).
2. **`src/fg_gles2_compat.{c,h}`** — a minimal immediate-mode emulation
   layer that lets freeglut's *stock* menu and font code run on ES2.

These are independent in principle (the compat layer could serve any pure
GLES2 build), but were designed and tested together.

## Did we use gl4es?

**No.** No part of this backend depends on gl4es, glues, ANGLE-as-a-source,
or any other general-purpose GL translation library. The application's
rendering path talks ES2/WebGL directly with nothing in between.

The only legacy-GL emulation is `fg_gles2_compat`, and it is deliberately
*not* a general translator. It implements only the ~20 fixed-function entry
points that `fg_menu.c` and `fg_font.c` actually call, and only those files
ever see it. The reasoning: those two files use a tiny, fixed, fully-known
subset of GL 1.x (matrix stacks, `glBegin/glEnd` with a handful of
primitives, `glBitmap`, a few enables). Emulating exactly that subset is a
few hundred lines with no external dependency, no build integration, and no
behavior to debug beyond what the menus/fonts exercise. Pulling in gl4es
would mean vendoring and maintaining a whole library to cover calls we don't
make. (gl4es remains the right tool in other contexts — e.g. porting
applications whose own draw code is dense GL 1.x — but freeglut's GUI is not
that.)

## The fgPlatform* contract

freeglut's core is platform-agnostic. Each backend lives in `src/<platform>/`
and implements a set of `fgPlatform*` functions plus a platform header that
defines a handful of types. The SDL2 backend provides:

| File | Responsibility |
|------|----------------|
| `fg_internal_sdl2.h` | Platform types: `SFG_WindowHandleType` = `SDL_Window*`, `SFG_WindowContextType` = `SDL_GLContext`, joystick/display/window-state structs, menu font + pen colors. |
| `fg_init_sdl2.c` | `SDL_Init`/`SDL_Quit`, screen metrics, `glutGet`/`glutDeviceGet` state queries, proc-address lookup, cursor mapping, game mode, cursor-position query, colormap + serial-device stubs. |
| `fg_window_sdl2.c` | Window open/close, per-window ES2 context creation, title/show/hide, deferred position/size/z-order/visibility work, first-frame init. |
| `fg_main_sdl2.c` | Timing, event wait, and the SDL→GLUT event translation loop. |
| `fg_joystick_sdl2.c` | Joystick enumeration and polling via `SDL_Joystick`. |

### Context and display mode

`fghApplyContextAttributes()` requests an ES 2.0 context
(`SDL_GL_CONTEXT_PROFILE_ES`, major 2 / minor 0) and maps freeglut's
`GLUT_*` display-mode bits to `SDL_GL_SetAttribute` calls (alpha, depth,
stencil, double-buffer, multisample). Each regular window gets its own GL
context, matching classic GLUT semantics; context sharing is requested so
resources can be shared where the platform allows it.

### Event translation

`fg_main_sdl2.c` polls SDL events and dispatches them through freeglut's
window callbacks. Notable mappings:

- Window lookup goes `SDL_GetWindowFromID` → `fgWindowByHandle`.
- Keyboard is split the way GLUT expects: printable characters arrive via
  `SDL_TEXTINPUT` (correct layout/shift handling), while control characters
  and `KeyboardUp` are derived from raw keycodes. Special keys (arrows, F-keys,
  modifiers) use a keycode→`GLUT_KEY_*` table.
- Mouse wheel maps to the `MouseWheel` callback, or falls back to the classic
  buttons 3/4 convention when only a `Mouse` callback is registered.
- **Mouse coordinates are scaled to drawable pixels at ingestion.** SDL reports
  pointer positions in logical points, but freeglut works in drawable pixels
  everywhere (`State.Width`, `GLUT_WINDOW_WIDTH`, and the GL viewport all come
  from `SDL_GL_GetDrawableSize`). On HiDPI/Retina the two differ by the window's
  backing-scale factor, so motion and button coordinates are multiplied by
  `drawable/window` before they are stored or dispatched. This keeps the GLUT
  coordinate space uniformly in pixels; without it, menu placement and
  hit-testing — and any app that compares the mouse to a window dimension — are
  off by the scale factor.
- `SDL_WINDOWEVENT_SIZE_CHANGED`/`MOVED`/visibility events feed
  `fghOnReshapeNotify`/`fghOnPositionNotify` and the work-mask system.

### Event ordering bug (resolved)

The first implementation of `fgPlatformSleepForEvents` waited for an event,
then *re-pushed* it so the normal poll loop could process it. That moved the
event to the back of the queue and reordered input, which manifested as menus
appearing to mis-handle a press/release pair. Fixed by waiting with a NULL
event pointer (`SDL_WaitEventTimeout(NULL, …)`), which blocks without
dequeuing anything, preserving order. This is exactly the kind of bug the
headless smoke test exists to catch.

## The ES2 immediate-mode compat layer

`fg_menu.c` and `fg_font.c` are written against fixed-function GL 1.x. Rather
than rewrite them (which would diverge from upstream and complicate merges),
they `#include "fg_gles2_compat.h"` under `FREEGLUT_GLES`, which `#define`s
the GL 1.x names they use onto `fghCompat*` implementations. Upstream sources
stay byte-for-byte intact except for that one include.

What the layer implements:

- **Matrix stacks** — modelview and projection stacks (column-major 4×4),
  with `MatrixMode/PushMatrix/PopMatrix/LoadIdentity/Ortho/Translatef`.
- **Immediate mode** — `Begin/End` accumulate vertices into a buffer flushed
  at `End`. `GL_QUADS` is converted to triangles; `GL_QUAD_STRIP` maps to
  `GL_TRIANGLE_STRIP` (identical vertex order). A single internal shader
  draws them with a uniform color.
- **`glBitmap`** (the bitmap fonts) — each glyph's packed 1-bpp bitmap is
  decoded once into an 8-bit alpha texture, cached keyed by the bitmap
  pointer (font glyph data is static, so pointers are stable). The glyph is
  drawn as a textured quad in a pixel-space ortho derived from the current
  viewport; the fragment shader discards texels below 0.5 alpha, reproducing
  glBitmap's "set pixels where bits are 1" semantics without blending. The
  raster position advances by the glyph's `xmove`.
- **Raster position** — `glRasterPos2i` is transformed through the current
  MVP and viewport into window coordinates, where the glyph quads are placed.
- **State** — `Enable/Disable` filter out non-ES2 capabilities (e.g.
  `GL_LIGHTING`); `PushAttrib/PopAttrib` save/restore the ES2-relevant state
  (depth/cull/blend); `PixelStorei/GetIntegerv` emulate the classic
  `GL_UNPACK_*` parameters as inert state since ES2 only has
  `GL_UNPACK_ALIGNMENT`.

Every emitted draw saves and restores the surrounding GL state (program,
buffer bindings, bound texture, attribute-array enables, depth/cull/blend) so
the GUI never disturbs application rendering.

### Per-context program cache

Shader programs are not shared across GL contexts, and a freeglut program can
have several top-level windows, each with its own context. The layer therefore
keeps a small program cache keyed by the current context identity, compiling
its shader lazily per context. (Menus no longer factor in here: on the SDL2
backend they are always drawn as an in-window overlay in the parent's context,
never in a window of their own — see *Pop-up menus* below.)

### Pure-ES2 geometry paths

`fg_geometry.c` already had ES2 (`*20`) draw paths, but they were guarded so
they only compiled when a GLES1 header was *also* present (the historical
assumption that GLES1 and GLES2 headers come together). Those guards were
widened to include `GL_ES_VERSION_2_0`, the fixed-function fallbacks were made
no-ops on pure ES2, and the legacy immediate-mode `*10` helpers (which
reference `glBegin`/`glEnd`/`glVertex3fv`) were excluded on pure ES2 — they are
only ever called from the no-op fallback there, so leaving them compiled
relied on dead-code elimination to avoid undefined `glBegin` references and
broke unoptimized builds. Similarly, `fg_window.c`'s
`glDrawBuffer/glReadBuffer` single-buffer path is skipped on pure ES2 (no such
calls exist there), matching how the EGL path already behaved.

## Pop-up menus

freeglut/GLUT classically renders each pop-up menu into its **own top-level
window** with its own GL context — that is how a menu can extend past the edge
of the parent window like a native context menu. That model does not survive
this backend:

- On **Emscripten** there is only one canvas, so a separate menu window is
  impossible.
- In the **gl4es configuration** a separate menu window actively *crashes*:
  the menu's `glBitmap` text is accumulated by gl4es against one framebuffer's
  dimensions, then blitted into the small menu window's mismatched framebuffer,
  walking off the end of a texture upload deep inside ANGLE.

So separate-window menus are **not used on the SDL2 backend at all**. Menus are
always drawn as an in-window overlay (below), which is forced on for every
SDL2 build.

### `GLUT_MENU_IN_WINDOW` (overlay mode)

A new option, `glutSetOption(GLUT_MENU_IN_WINDOW, 1)` (enum `0x0208`,
queryable via `glutGet`), draws menus as an **overlay inside the parent
window** instead of in separate windows. On the SDL2 backend it is **forced on
and cannot be disabled**, in both the raw-ES2 and gl4es configurations, for the
reasons above (Emscripten's single canvas; the gl4es separate-window crash).
Forcing it also means unmodified apps — which never call `glutSetOption` — get
working menus with no source change. The desktop X11/Win32/Cocoa backends are
unaffected: the option remains opt-in and off by default there.

The key observation that made this cheap: all of freeglut's menu *logic*
(hit-testing, highlight tracking, submenu cascade, selection, dismissal) is
arithmetic on `menu->X/Y` and a per-menu `State.MouseX/Y`. The coordinate
*space* those live in is chosen at activation time. Overlay mode simply
chooses **parent-window coordinates**, and all the existing logic works
unchanged.

What overlay mode changes:

- **Activation/positioning** — the menu is placed at the mouse cursor in
  parent-window coordinates, then shifted left/up as needed so the whole menu
  fits inside the framebuffer (the requested "move it so it fits" behavior,
  rather than classic GLUT's screen-relative flip). Submenus get the same
  fit-to-window treatment.
- **No platform windows** — in overlay mode the SDL2 backend skips window and
  context creation for menus entirely (the handle stays `NULL`; all
  `fgPlatform*` entry points are NULL-guarded).
- **Rendering** — `fgDisplayMenuInWindow()` draws the active menu chain (root
  plus any open submenus, each translated to its position) into the parent
  window's framebuffer. It is invoked from inside `glutSwapBuffers()`, just
  before the platform swap, so the menu lands on top of whatever the app drew,
  in the app's own context, with viewport and GL state saved/restored around it.
- **Redisplay** — highlight changes and open/close post a redisplay on the
  *parent* window (not the now-nonexistent menu window) so the overlay updates
  in event-driven apps.
- **Event coordinates** — in overlay mode the menu's mouse position is
  computed from the parent window's client coordinates rather than the global
  screen position.

### Presenting when the app doesn't swap

Because the overlay is drawn inside `glutSwapBuffers`, an application whose
display callback never swaps (e.g. an empty callback, or one that draws to the
front buffer) would never present the menu. To handle this, `fgState` keeps a
monotonic `SwapsPerformed` counter. `fghRedrawWindow` snapshots it before
calling the display callback; if an overlay menu is active and the counter did
not advance during the callback, freeglut calls `glutSwapBuffers()` itself.
That both draws the overlay and presents the frame, and it works for
single-buffered windows too (the overlay is drawn before the double-buffer
early-return, followed by `glFlush`).

A residual quirk remains for apps that *never* repaint: after a menu is
dismissed, the last presented frame still contains the menu until the app
draws something, because freeglut cannot repaint content the app never
rendered. Any app that draws anything in its display callback is unaffected,
since dismissal posts a redisplay. Fully covering the pathological case would
require freeglut to snapshot the framebuffer under the menu and blit it back
on dismissal — deliberately not done, as it is a lot of machinery for an app
pattern that does not occur in practice.

## Build integration

`CMakeLists.txt` gains a `FREEGLUT_SDL2` option (default OFF) which forces
`FREEGLUT_GLES` on (the raw-ES2 "Modern configuration" above), and a
`FREEGLUT_SDL2_GL4ES` option (default OFF) which selects the SDL2 backend with
freeglut's desktop-GL drawing code for use with gl4es (the "Legacy
configuration" above);
the latter turns on `FREEGLUT_SDL2` but leaves `FREEGLUT_GLES` off. When
`FREEGLUT_SDL2` is set (either mode):

- the four `src/sdl2/*.c` files are compiled, `TARGET_HOST_SDL2=1` and the
  public `FREEGLUT_SDL2` define are set;
- (raw-ES2 mode only) the menu/font sources plus `fg_gles2_compat.c` are
  compiled **instead of** `gles_stubs.c` (the stock GLES build stubs
  menus/fonts out; this backend enables them). In gl4es mode the stock
  desktop `fg_menu.c`/`fg_font.c` are compiled instead and the shim is not;
- the X11 dependency block and the EGL source/link paths are excluded (SDL2
  owns context creation);
- linking pulls in SDL2 and, in raw-ES2 mode, an `GLESv2` library; in gl4es
  mode, gl4es's `libGL` (via `GL4ES_LIBRARY`, falling back to the system GL
  for build validation). Under Emscripten the GLESv2 link is skipped
  (provided implicitly). On macOS, if no `GLESv2` is found the configure step
  fails with a message pointing at ANGLE.

The public header `include/GL/freeglut_std.h` includes only `<GLES2/gl2.h>`
under `FREEGLUT_SDL2` in raw-ES2 mode (no `<EGL/egl.h>`, no `<GLES/gl.h>`),
since those may not exist with ANGLE on macOS. In gl4es mode `FREEGLUT_GLES`
is off, so the header takes its normal desktop path (`<GL/gl.h>`/`<GL/glu.h>`),
which gl4es provides.

## Application rendering on ES2 — and running unmodified legacy apps

The overriding goal of this port is to run **old, unmodified GLUT
applications** — programs written against 1990s GLUT that use fixed-function
immediate mode (`glBegin`/`glVertex3f`/`glRotatef`/`glLightfv`), GLU
(`gluPerspective`, `gluLookAt`), and the GLUT shapes API
(`glutSolidTeapot`, `glutWireSphere`, …) with no shaders and no buffer
objects. None of that exists in OpenGL ES 2.0. There are two distinct
configurations of this backend, and which one you want depends entirely on
whether you are willing to touch the application's source.

### Where freeglut's own drawing fits

It is worth separating three sources of GL calls:

1. **The application's draw code** — whatever the GLUT program itself issues.
2. **freeglut's GUI** — menus and fonts (`fg_menu.c`, `fg_font.c`).
3. **The GLUT shapes API** — `glutSolid*`/`glutWire*`, implemented in
   `fg_geometry.c`.

The compat layer described earlier handles (2) only, and only in the raw-ES2
configuration. (1) and (3) are the application's concern, and they are what
the gl4es configuration exists for.

### The GLUT shapes API

`fg_geometry.c` already contains a modern ES2 draw path
(`fghDrawGeometrySolid20`): it builds VBOs/IBOs and issues `glDrawElements`
with vertex attributes — no fixed function. Every shape dispatches through:

```c
if (fgState.HasOpenGL20 && (attribute_v_coord != -1 || attribute_v_normal != -1))
    /* modern ES2 path */
else
    /* fixed-function path */
```

On a pure-ES2 build `HasOpenGL20` is forced on, but the ES2 path **only runs
if the application has told freeglut which shader attribute locations to feed
geometry into**, via `glutSetVertexAttribCoord3()` /
`glutSetVertexAttribNormal()`. If it hasn't (and a legacy app never will,
because those entry points postdate it), `attribute_v_coord` stays `-1`, the
condition fails, and control falls to the fixed-function path — which on pure
ES2 is a **no-op** (there is no `glBegin` to call), so the shape silently does
not draw.

So on the raw-ES2 build, the shapes API requires the application to: bind a
shader exposing position (and normal) attributes, call
`glutSetVertexAttribCoord3`/`Normal` once, and supply its own MVP uniform
(freeglut emits geometry in object space and does not touch matrices). This
is the same contract freeglut's existing Android/EGL GLES2 builds document.
It is a source change, so it does **not** serve the unmodified-app goal — it
is only relevant if you are modernizing the app anyway.

### Modern configuration — raw OpenGL ES 2.0 (`-DFREEGLUT_SDL2=ON`)

This is the configuration the rest of this document describes: freeglut talks
ES2 directly, menus/fonts go through the `fg_gles2_compat` shim, and the
application is expected to be ES2-native (its own shaders, and the
`glutSetVertexAttrib*` contract for shapes). Use this for new or modernized
applications. It has no external dependency beyond SDL2 and an ES2 driver.

### Legacy configuration — gl4es, for unmodified legacy apps (`-DFREEGLUT_SDL2_GL4ES=ON`)

[gl4es](https://github.com/ptitSeb/gl4es) is a library that implements the
full desktop GL 1.x/2.x API — `glBegin`, the matrix stack, fixed-function
lighting, everything — and translates it to GLES2 underneath. It is the
mechanism by which an unmodified immediate-mode app runs on an ES2/WebGL
device.

The important architectural point: **gl4es is not integrated into freeglut.**
It is a *link-time substitution* of the GL library, sitting between both the
application and freeglut on one side, and the real GLES2 driver on the other:

```
   unmodified GLUT app  (glBegin, glRotatef, gluPerspective, glutSolidTeapot…)
        |   \
        |    \—— freeglut GUI + shapes (also desktop-GL calls)
        |    /
       gl4es   (implements the desktop GL API)
        |
   OpenGL ES 2.0 context  (created by the SDL2 backend)
```

Because gl4es presents *as* desktop GL, freeglut in this configuration uses
its **standard desktop-GL code**, not the ES2 compat shim:

- `FREEGLUT_GLES` is **off**. `fg_menu.c`/`fg_font.c` are compiled in their
  normal form and call real `glBegin`/`glBitmap` (resolved by gl4es). The
  `fg_gles2_compat` shim is not compiled at all.
- `fg_geometry.c`'s **fixed-function fallback is compiled** (the ES2 no-op is
  only emitted when `GL_ES_VERSION_2_0` is defined, which it is not here), so
  `glutSolidTeapot()` and friends work through gl4es with **no
  `glutSetVertexAttrib*` calls** — exactly what a legacy app needs.
- The SDL2 backend still creates a **GLES2 context** (`SDL_GL_CONTEXT_PROFILE_ES`,
  2.0), because that is what gl4es needs underneath. The desktop-vs-ES2
  question is about which *API* freeglut's code calls (desktop, via gl4es),
  not which *context* exists (ES2).

#### Build recipe

```sh
# 1. Build gl4es (produces a libGL that exports the desktop GL API).
#    See the gl4es README; for an ES2 backend the common settings are
#    LIBGL_ES=2 and pointing it at the system EGL/GLESv2.

# 2. Build freeglut in gl4es mode, pointing it at gl4es's libGL:
cmake -S . -B build \
      -DFREEGLUT_SDL2_GL4ES=ON \
      -DGL4ES_LIBRARY=/path/to/gl4es/lib/libGL.so \
      -DGL4ES_INCLUDE_DIR=/path/to/gl4es/include
cmake --build build

# 3. Build/link the unmodified app against this freeglut and the SAME
#    gl4es libGL — NOT the system libGL or libGLESv2. Link order matters:
#    the app's glBegin etc. and freeglut's must resolve to gl4es.
```

If `GL4ES_LIBRARY` is not set, the build falls back to the system GL library
and emits a warning; this lets the configuration compile and link for
validation, but a real gl4es `libGL` is required at runtime to perform the
translation.

#### Runtime: context hand-off

The one genuinely fiddly part is making gl4es adopt the GLES2 context that
SDL2 created, rather than trying to create its own. The SDL2 backend creates
and makes-current a GLES2 context before any GL call; gl4es then latches onto
the current EGL context on first use. In practice this is controlled by
gl4es's environment/build options (e.g. `LIBGL_ES=2`, and the no-init/late
binding behavior that uses the already-current context). On a normal native
ES2 stack this is straightforward; on Emscripten gl4es has a WebGL path but it
is a heavier lift, since it stacks gl4es's translation on top of the browser's
own GLES2→WebGL mapping.

#### GLU

Legacy apps almost always use GLU (e.g. `gluPerspective`, `gluLookAt`,
`gluBuild2DMipmaps`). GLU sits *above* GL — every GLU routine just emits
ordinary GL calls, which gl4es then translates to GLES2 — so GLU never talks to
GLES directly. The only question is whose GLU implementation runs: gl4es's own
bundled GLU (thin — matrix helpers, basic mipmap/error), or a fuller standalone
GLU built against gl4es's headers.

**This project vendors glues (GLU-ES)** in
`glues/`, rather than leaning on gl4es's bundled GLU. A demo museum keeps
surfacing GLU-dependent Red Book demos (quadrics, tessellator, NURBS, image
scaling), so we want the complete API; keeping the implementation in-tree also
versions it with the demos and keeps it identical across native and web. The
integration cost is paid once and is now ~zero per demo.

Integration facts worth remembering:

- glues is built into `libglues-native.a` / `libglues-web.a` and linked
  **before** gl4es (its `gluPerspective` calls gl4es's `glFrustum`/
  `glMultMatrixf`): `app → freeglut → glues → gl4es → ANGLE`.
- glues includes gl4es's `<GL/gl.h>` **only** (selected by `-DGLUES_GL4ES`, via
  an added platform branch in `glues.h`), *not* gl4es's `<GL/glu.h>` — gl4es's
  GLU prototypes would clash with glues' own. glues is the sole GLU provider;
  never link both GLUs for the same symbols.
- On Apple/web, gl4es mangles `glu*` → `mglu*`. glues is compiled with
  `-include GL/glu_mangle.h` so its *definitions* get the same mangling the
  app's *calls* do (the calls mangle via freeglut's `FREEGLUT_SDL2_GL4ES`
  header routing to gl4es's `<GL/glu.h>`). Both sides mangle, so they match; on
  Linux neither mangles. A one-sided mangle is an undefined-symbol link error.

#### What this configuration was tested against

This configuration has been run **end-to-end on macOS with ANGLE** — gl4es
providing the desktop GL API over an SDL2/ANGLE ES2 context. Two unmodified
demos are verified:

- **gears_min** — immediate-mode spinning gears with a flat right-click menu.
- **newave** — env-mapped wave mesh using `gluPerspective` (resolved through
  vendored glues), SGI `.rgb` textures with `GL_SPHERE_MAP`, fixed-function
  lighting, and a multi-level submenu cascade.

Both run with no source changes: textures, lighting, immediate-mode geometry,
and in-window menus all work. The library is confirmed to use real desktop
immediate mode (`glBegin`/`glBitmap`/`glMatrixMode`) resolved by gl4es, with
the ES2 compat shim and the GLES stubs both absent.

This covers the **macOS/ANGLE** runtime specifically. The Emscripten gl4es path
(heavier, as noted above) and other platforms have not yet been exercised
end-to-end here. Beyond the runtime, freeglut's desktop drawing code in this
mode is the same code the X11 build exercises, which the X11 regression build
covers.

### Summary

| | Modern config: raw ES2 | Legacy config: gl4es |
|---|---|---|
| CMake | `-DFREEGLUT_SDL2=ON` | `-DFREEGLUT_SDL2_GL4ES=ON` |
| `FREEGLUT_GLES` | on | off |
| Menus/fonts | `fg_gles2_compat` shim | stock desktop code via gl4es |
| Shapes API | needs `glutSetVertexAttrib*` + shader | works unmodified via gl4es |
| App's own immediate mode | not supported | works unmodified via gl4es |
| External dependency | SDL2 + ES2 driver | SDL2 + ES2 driver + gl4es + vendored glues (GLU) |
| Intended for | new / modernized apps | **unmodified legacy GLUT apps** |

For the stated goal — old GLUT apps running unmodified — **the Legacy
configuration (gl4es) is the path.** The Modern configuration exists for the
case where the application is being brought up to native ES2.

## Platform notes and limitations

- **macOS** needs an ES2 driver, i.e. ANGLE; SDL2 will pick it up, and CMake
  errors with a hint if `GLESv2` is missing.
- **Emscripten** maps ES2→WebGL1 and skips the GLESv2 link.
- **`GLUT_MENU_IN_WINDOW` is forced on for every SDL2 build** (both
  configurations, all platforms) and cannot be disabled — menus are always
  in-window overlays here, never separate windows.
- **`glutCreateSubWindow` is not supported** — SDL2 has no child windows; the
  call warns and creates a top-level window instead.
- **Color-index mode is unsupported** on ES2; the colormap entry points are
  stubs that warn.
- **`glRasterPos` is not available to applications on ES2** — there is no such
  entry point in the app-facing API. The compat layer's raster position exists
  only inside the menu/font code. Bitmap text drawn directly by an app appears
  at the window origin. (Menus position text internally and are unaffected.)
  A small `glutRasterPos`-style extension would be the natural follow-up if
  app-side text positioning is needed.
- **Dial/button-box and spaceball devices** are stubbed (no serial input).

## Testing

All testing runs headless under Xvfb with Mesa's `llvmpipe` software ES
driver (`SDL_VIDEODRIVER=x11`, `LIBGL_ALWAYS_SOFTWARE=1`), verifying actual
rendered pixels via `glReadPixels` rather than just checking that calls
return.

- **`smoke.c`** — windowed mode: an ES2 shader triangle and a line of bitmap
  text are each confirmed by pixel readback; a menu is opened and closed via
  injected SDL events with open/close confirmed through `glutMenuStatusFunc`;
  zero GL errors. (This test is what caught the event-ordering bug.)
- **`menu_overlay_test.c`** — `GLUT_MENU_IN_WINDOW`: a menu opened near the
  bottom-right corner is confirmed by readback to be drawn inside the
  framebuffer, fully clamped within bounds and adjacent to the click; a held
  mouse move is confirmed to move the highlight; release over an entry fires
  the callback with the correct ID and the overlay fully disappears.
- **`menu_noswap_test.c`** — same as above but with a **completely empty**
  display callback (no clear, no swap), verifying the library presents the
  menu overlay on the application's behalf.

The stock X11 desktop build is also rebuilt as a regression check to confirm
the shared-core changes do not affect existing backends.

## File summary

New:

- `src/sdl2/fg_internal_sdl2.h` — platform types and menu appearance macros.
- `src/sdl2/fg_init_sdl2.c` — init, state queries, cursor, game mode, stubs.
- `src/sdl2/fg_window_sdl2.c` — window and context lifecycle, deferred work.
- `src/sdl2/fg_main_sdl2.c` — timing and SDL→GLUT event translation
  (incl. HiDPI logical-point → drawable-pixel mouse scaling).
- `src/sdl2/fg_joystick_sdl2.c` — joystick support.
- `src/fg_gles2_compat.h` / `.c` — GUI-only GL 1.x→ES2 emulation.

Modified (shared core):

- `fg_internal.h` — `TARGET_HOST_SDL2` plumbing, `MenuInWindow` and
  `SwapsPerformed` state, `fgDisplayMenuInWindow` declaration.
- `fg_menu.c` — overlay positioning/coordinates/redisplay and the overlay
  renderer (the only GUI-logic file touched beyond the compat include).
- `fg_display.c` — overlay hook and swap counting in `glutSwapBuffers`.
- `fg_main.c` — present-on-behalf logic in `fghRedrawWindow`.
- `fg_state.c` / `fg_init.c` — `GLUT_MENU_IN_WINDOW` option get/set, and the
  default (forced on for the SDL2 backend via `FREEGLUT_SDL2`).
- `fg_window.c`, `fg_geometry.c` — pure-ES2 compile guards.
- `fg_font.c` — compat-layer include only.
- `CMakeLists.txt`, `include/GL/freeglut_std.h` — build and header wiring
  (`FREEGLUT_SDL2` raw-ES2 mode and `FREEGLUT_SDL2_GL4ES` gl4es mode).
