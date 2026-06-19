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

Shader programs are not shared across GL contexts, and menu windows (in the
default windowed mode) have their own contexts. The layer therefore keeps a
small program cache keyed by the current context identity, compiling its
shader lazily per context.

### Pure-ES2 geometry paths

`fg_geometry.c` already had ES2 (`*20`) draw paths, but they were guarded so
they only compiled when a GLES1 header was *also* present (the historical
assumption that GLES1 and GLES2 headers come together). Those guards were
widened to include `GL_ES_VERSION_2_0`, and the fixed-function fallbacks were
made no-ops on pure ES2. Similarly, `fg_window.c`'s `glDrawBuffer/glReadBuffer`
single-buffer path is skipped on pure ES2 (no such calls exist there), matching
how the EGL path already behaved.

## Pop-up menus

freeglut/GLUT renders each pop-up menu into its **own top-level window** with
its own GL context — that is how menus can extend past the edge of the parent
window like a native context menu. The SDL2 backend supports this directly,
and the ES2 compat layer makes the menu's drawing work on ES2.

But "menu = separate OS window" does not survive the move to a single drawing
surface (Emscripten has one canvas). So a second mode was added.

### `GLUT_MENU_IN_WINDOW` (overlay mode)

A new option, `glutSetOption(GLUT_MENU_IN_WINDOW, 1)` (enum `0x0208`,
queryable via `glutGet`), draws menus as an **overlay inside the parent
window** instead of in separate windows. It is **forced on under Emscripten**
(and cannot be disabled there) because the platform has a single canvas; it
is off by default elsewhere.

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
`FREEGLUT_GLES` on. When set:

- the four `src/sdl2/*.c` files are compiled, `TARGET_HOST_SDL2=1` and the
  public `FREEGLUT_SDL2` define are set;
- the menu/font sources plus `fg_gles2_compat.c` are compiled **instead of**
  `gles_stubs.c` (the stock GLES build stubs menus/fonts out; this backend
  enables them);
- the X11 dependency block and the EGL source/link paths are excluded (SDL2
  owns context creation);
- linking pulls in SDL2 and an `GLESv2` library. Under Emscripten the GLESv2
  link is skipped (provided implicitly). On macOS, if no `GLESv2` is found the
  configure step fails with a message pointing at ANGLE.

The public header `include/GL/freeglut_std.h` includes only `<GLES2/gl2.h>`
under `FREEGLUT_SDL2` (no `<EGL/egl.h>`, no `<GLES/gl.h>`), since those may
not exist with ANGLE on macOS.

## Platform notes and limitations

- **macOS** needs an ES2 driver, i.e. ANGLE; SDL2 will pick it up, and CMake
  errors with a hint if `GLESv2` is missing.
- **Emscripten** maps ES2→WebGL1, forces `GLUT_MENU_IN_WINDOW` on, and skips
  the GLESv2 link.
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
- `src/sdl2/fg_main_sdl2.c` — timing and SDL→GLUT event translation.
- `src/sdl2/fg_joystick_sdl2.c` — joystick support.
- `src/fg_gles2_compat.h` / `.c` — GUI-only GL 1.x→ES2 emulation.

Modified (shared core):

- `fg_internal.h` — `TARGET_HOST_SDL2` plumbing, `MenuInWindow` and
  `SwapsPerformed` state, `fgDisplayMenuInWindow` declaration.
- `fg_menu.c` — overlay positioning/coordinates/redisplay and the overlay
  renderer (the only GUI-logic file touched beyond the compat include).
- `fg_display.c` — overlay hook and swap counting in `glutSwapBuffers`.
- `fg_main.c` — present-on-behalf logic in `fghRedrawWindow`.
- `fg_state.c` / `fg_init.c` — `GLUT_MENU_IN_WINDOW` option get/set/default.
- `fg_window.c`, `fg_geometry.c` — pure-ES2 compile guards.
- `fg_font.c` — compat-layer include only.
- `CMakeLists.txt`, `include/GL/freeglut_std.h` — build and header wiring.
