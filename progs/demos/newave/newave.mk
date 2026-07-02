# newave.mk -- build newave (GLUT 3.7 wave-mesh demo) for native (Mac/Linux/Win
# via ANGLE) and the browser (Emscripten), on the freeglut SDL2/GLES2/gl4es port.
#
# Same shape as gears_min.mk, with ONE addition: newave calls gluPerspective, so
# this links GLU-ES (glues). glues' glu* are compiled with the SAME mgl* mangling
# gl4es applies on Apple/web, so newave's gluPerspective -> mgluPerspective resolves
# against glues' mgluPerspective. (On Linux both stay unmangled -> still matches.)
#
# Links a prebuilt FREEGLUT (Legacy gl4es config, -DFREEGLUT_SDL2_GL4ES=ON) + the
# gl4es you already built; builds glues on demand. Does NOT build freeglut/gl4es.
#
#   make            native build  -> ./bin-<os>-<arch>/newave
#   make run        build + run native (cwd = this dir, so texmap.rgb resolves)
#   make browser    Emscripten    -> ./web/newave.js  (loaded by your ./web/newave.html)
#   make clean
#
# Point these at your trees (env or make VAR=...):
#   FREEGLUT       = freeglut source (has include/GL/freeglut.h)
#   FREEGLUT_BUILD = freeglut build dir with the built lib (libglut/libfreeglut)
#   GL4ES          = gl4es checkout (include/ + lib/libGL.a)
#   GLUES          = glues checkout (source/ + Makefile; built into lib/libglues-native.a)
#   OGL_FOR_MAC / GLES_DIR = ANGLE headers + libGLESv2/libEGL
#
# Example:
#   make FREEGLUT=~/Github/freeglut-sdl2-ogles2 FREEGLUT_BUILD=~/Github/freeglut-sdl2-ogles2/build \
#        GL4ES=~/Github/gl4es GLUES=~/Github/glues OGL_FOR_MAC=~/Github/opengl-for-mac
#
# FIRST LIGHT: newave starts in WIREFRAME (displayMode=WIREFRAME, envMap=false), so
# the GL_SPHERE_MAP env-map path is NOT exercised at startup -- good for bring-up.
# But loadImageTexture() runs at init, so texmap.rgb + spheremap.rgb MUST be in the
# cwd at launch (they already live next to this Makefile). `make run` runs from here,
# so they resolve. Enable Textured/Environment Map from the right-click menu only
# after the untextured modes (Wireframe/Hidden Line/Flat/Smooth) look correct.

APPNAME = newave
SRC     = newave.c texture.c

# ---- platform detection (mirrors flwbox platform.mk) ----
OS := $(shell uname -s)
HW := $(shell uname -m)
ifeq ($(OS),Darwin)
	OS = mac
	DYL_EXT = dylib
else ifeq ($(OS),Linux)
	OS = linux
	DYL_EXT = so
else ifneq ($(findstring MINGW64_NT,$(OS)),)
	OS = win
	DYL_EXT = dll
	CONSOLE_FLAGS = -mconsole
else ifneq ($(findstring MSYS_NT,$(OS)),)
	OS = win
	DYL_EXT = dll
	CONSOLE_FLAGS = -mconsole
endif

BIN_DIR = ./bin-$(OS)-$(HW)
WEB_DIR = ./web
APP     = $(BIN_DIR)/$(APPNAME)
# emcc emits the JS module (+ .wasm + .data); the HTML page that loads ./newave.js
# is hand-maintained (WEB_PAGE) and intentionally NOT generated/overwritten here.
EM_OUT   = $(WEB_DIR)/$(APPNAME).js
WEB_PAGE ?= $(WEB_DIR)/$(APPNAME).html

# ---- trees (override on the command line or via env) ----
FREEGLUT       ?= ../../..
FREEGLUT_BUILD ?= $(FREEGLUT)/build
GL4ES          ?= $(FREEGLUT)/../gl4es
GLUES          ?= $(FREEGLUT)/../glues
OGL_FOR_MAC    ?= $(FREEGLUT)/../opengl-for-mac
GLES_DIR       ?= $(OGL_FOR_MAC)

# ---- SDL2 ----
SDL_INC  = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)

# ---- freeglut (Legacy gl4es config) ----
FREEGLUT_INC = -I$(FREEGLUT)/include
FREEGLUT_LIBDIR = $(firstword $(wildcard $(FREEGLUT_BUILD)/lib) $(FREEGLUT_BUILD))
FREEGLUT_LIB    = $(firstword \
    $(wildcard $(FREEGLUT_LIBDIR)/libfreeglut.a) \
    $(wildcard $(FREEGLUT_LIBDIR)/libglut.a) \
    $(wildcard $(FREEGLUT_LIBDIR)/libfreeglut.$(DYL_EXT)) \
    $(wildcard $(FREEGLUT_LIBDIR)/libglut.$(DYL_EXT)))

# ---- gl4es (static; headers FIRST so <GL/gl.h>/<GL/glu.h> resolve to gl4es') ----
GL4ES_INC = -I$(GL4ES)/include
GL4ES_LIB = $(GL4ES)/lib/libGL-native.a

# ---- glues (GLU-ES: provides gluPerspective implementation) ----
# newave gets the glu* *prototypes* from gl4es' <GL/glu.h> (via freeglut's
# FREEGLUT_SDL2_GL4ES header routing), so no glues include is needed here; glues
# only supplies the *definitions*. glues is built on demand below, passing GL4ES
# through so its mangling matches.
GLUES_LIB = $(GLUES)/lib/libglues-native.a

# ---- ANGLE GLES2 / EGL (static gl4es needs these supplied by the exe) ----
GLES_LIBS_PATH = $(GLES_DIR)/lib
GLES_LIBS      = -L$(GLES_LIBS_PATH) -lGLESv2 -lEGL
GLES_DYLIBS    = libGLESv2.$(DYL_EXT) libEGL.$(DYL_EXT)

ifeq ($(OS),mac)
	GLES_LINK = -Wl,-rpath,$(GLES_LIBS_PATH)
define GLES_INSTALL
	for dylib in $(GLES_DYLIBS); \
		do install_name_tool -change ./$$dylib @rpath/$$dylib $@ 2>/dev/null || true; \
	done;
endef
else ifeq ($(OS),linux)
	GLES_LINK = -Wl,-rpath,$(abspath $(GLES_LIBS_PATH))
else ifeq ($(OS),win)
define GLES_INSTALL
	for dylib in $(GLES_DYLIBS); \
		do cp $(GLES_LIBS_PATH)/$$dylib $(BIN_DIR)/$$dylib; \
	done;
endef
endif

# gl4es include must precede SDL/system includes.
# -DFREEGLUT_SDL2_GL4ES routes <GL/freeglut.h> to gl4es' (mangled) GL+GLU headers.
# -DFREEGLUT_STATIC: we link freeglut's static lib.
INCS = $(GL4ES_INC) $(FREEGLUT_INC) $(SDL_INC) -DFREEGLUT_SDL2_GL4ES -DFREEGLUT_STATIC

CC   = cc
EMCC = emcc -s WASM=1

OPT_ZERO = -O0 -g
OPT_TWO = -DNDEBUG -O2
OPT = $(OPT_ZERO)
EM_OPT = $(OPT_TWO)

# newave is vintage code; relax a couple of warnings so the build stays quiet.
WARN = -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
       -Wno-implicit-function-declaration

EM_SDL_FLAGS    = -s USE_SDL=2 -s FULL_ES2=1
EM_OUTPUT_FLAGS = -s ALLOW_MEMORY_GROWTH=1 -s EXIT_RUNTIME=0

# ---- web (Emscripten) libs ----
# freeglut + gl4es are built for wasm by build-full-web.sh (freeglut -> build-web/,
# gl4es -> lib/libGL-web.a); glues' web lib is built on demand below.
# SDL2 comes from emcc's port (-s USE_SDL=2), so no SDL include/lib here.
EM_FREEGLUT_BUILD ?= $(FREEGLUT)/build-web
EM_FREEGLUT_LIBDIR = $(firstword $(wildcard $(EM_FREEGLUT_BUILD)/lib) $(EM_FREEGLUT_BUILD))
EM_FREEGLUT_LIB    = $(firstword \
    $(wildcard $(EM_FREEGLUT_LIBDIR)/libfreeglut.a) \
    $(wildcard $(EM_FREEGLUT_LIBDIR)/libglut.a))
EM_GL4ES_LIB = $(GL4ES)/lib/libGL-web.a
EM_GLUES_LIB = $(GLUES)/lib/libglues-web.a
# Same defines as native; gl4es' headers self-mangle glu*->mglu* under Emscripten,
# matching glues-web (built with -include GL/glu_mangle.h).
EM_INCS = $(GL4ES_INC) $(FREEGLUT_INC) -DFREEGLUT_SDL2_GL4ES -DFREEGLUT_STATIC

all: native

native: $(APP)

$(BIN_DIR) $(WEB_DIR):
	mkdir -p $@

# Build glues on demand (passes GL4ES so its glu* mangling matches gl4es').
$(GLUES_LIB):
	@test -d "$(GLUES)" || { echo "ERROR: GLUES dir '$(GLUES)' not found -- set GLUES=/path/to/glues."; exit 1; }
	$(MAKE) -C $(GLUES) native GL4ES=$(GL4ES)

# ---- native link ----
# Order: app, freeglut, glues, gl4es (static), then ANGLE GLES/EGL, SDL, libm.
# glues precedes gl4es because glues' gluPerspective calls gl4es' (m)glMultMatrixf
# etc.; both freeglut and glues resolve their GL into gl4es, which comes last.
$(APP): $(SRC) $(GLUES_LIB) | $(BIN_DIR)
	@test -n "$(FREEGLUT_LIB)" || { echo "ERROR: freeglut lib not found under $(FREEGLUT_LIBDIR) -- build freeglut with -DFREEGLUT_SDL2_GL4ES=ON first, or set FREEGLUT_BUILD."; exit 1; }
	@test -f "$(GL4ES_LIB)" || { echo "ERROR: $(GL4ES_LIB) not found -- build gl4es first, or set GL4ES."; exit 1; }
	$(CC) $(OPT) $(WARN) $(INCS) $(SRC) \
		$(FREEGLUT_LIB) $(GLUES_LIB) $(GL4ES_LIB) \
		$(GLES_LIBS) $(GLES_LINK) $(SDL_LIBS) -lm $(CONSOLE_FLAGS) -o $@
	$(call GLES_INSTALL)
	@echo "BUILT (native): $@"
	@echo "Run: (cd $(CURDIR) && DYLD_FALLBACK_LIBRARY_PATH=$(GL4ES)/lib:$(GLES_LIBS_PATH) $(APP))   # mac; needs texmap.rgb in cwd"

# Run from THIS dir so texmap.rgb / spheremap.rgb (next to the Makefile) resolve.
run: native
	DYLD_FALLBACK_LIBRARY_PATH=$(GL4ES)/lib:$(GLES_LIBS_PATH) \
	LD_LIBRARY_PATH=$(GL4ES)/lib:$(GLES_LIBS_PATH) $(APP)

# ---- emscripten ----
# Compiles + links newave for wasm: freeglut-web + glues-web + gl4es-web, SDL2 via
# emcc's port, GLES2 via FULL_ES2. The two SGI .rgb textures are baked into the
# wasm VFS at root so newave's fopen("texmap.rgb") (cwd = "/") resolves.
# Build the wasm deps (gl4es-web + freeglut-web) with build-full-web.sh first;
# glues-web is built on demand here.
browser: $(EM_OUT)

$(EM_GLUES_LIB):
	@test -d "$(GLUES)" || { echo "ERROR: GLUES dir '$(GLUES)' not found -- set GLUES=/path/to/glues."; exit 1; }
	$(MAKE) -C $(GLUES) browser GL4ES=$(GL4ES)

# emcc emits newave.js/.wasm/.data ONLY -- never newave.html -- so your
# hand-maintained page ($(WEB_PAGE), which loads ./$(APPNAME).js) survives both
# rebuilds and `make clean`.
$(EM_OUT): $(SRC) $(EM_GLUES_LIB) | $(WEB_DIR)
	@test -n "$(EM_FREEGLUT_LIB)" || { echo "ERROR: web freeglut lib not found under $(EM_FREEGLUT_LIBDIR) -- build freeglut for Emscripten first (build-full-web.sh)."; exit 1; }
	@test -f "$(EM_GL4ES_LIB)" || { echo "ERROR: $(EM_GL4ES_LIB) not found -- build gl4es for web first (build-full-web.sh)."; exit 1; }
	$(EMCC) $(EM_OPT) $(WARN) $(EM_INCS) $(EM_SDL_FLAGS) $(SRC) \
		$(EM_FREEGLUT_LIB) $(EM_GLUES_LIB) $(EM_GL4ES_LIB) \
		$(EM_OUTPUT_FLAGS) \
		--preload-file texmap.rgb@/texmap.rgb \
		--preload-file spheremap.rgb@/spheremap.rgb \
		-lm -o $@
	@echo "BUILT (web): $@"
	@test -f "$(WEB_PAGE)" || echo "NOTE: page $(WEB_PAGE) not found -- add an HTML page that loads ./$(APPNAME).js"
	@echo "Serve/run: emrun $(WEB_PAGE)   (or: make -f newave.mk run-browser)"

run-browser: browser
	emrun $(WEB_PAGE)

clean:
	rm -rf $(BIN_DIR)
	# Generated wasm artifacts only -- the hand-maintained $(APPNAME).html page
	# (and anything else under web/) is preserved.
	rm -f $(WEB_DIR)/$(APPNAME).js $(WEB_DIR)/$(APPNAME).wasm $(WEB_DIR)/$(APPNAME).data

.PHONY: all native browser run run-browser clean
