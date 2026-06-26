# Makefile -- build gears_min, the GLU-free immediate-mode freeglut + gl4es
# demo, for native (Mac/Linux/Windows via ANGLE) and the browser (Emscripten).
#
# Unlike the flwbox-port demos, this links against a prebuilt FREEGLUT (built in
# its Legacy gl4es configuration, -DFREEGLUT_SDL2_GL4ES=ON) which itself wraps
# gl4es -- so this Makefile does NOT build gl4es; it points at the freeglut and
# gl4es you already built. No glues yet (the demo is deliberately GLU-free).
#
#   make            native build  -> ./bin-<os>-<arch>/gears_min
#   make run        build + run native
#   make browser    Emscripten    -> ./web/gears_min.html
#   make clean
#
# Point these at your trees (env or make VAR=...):
#   FREEGLUT  = your freeglut source/install (has include/GL/freeglut.h)
#   FREEGLUT_BUILD = freeglut build dir containing the built lib (libglut/libfreeglut)
#   GL4ES     = gl4es checkout (has include/ and lib/libGL.a)
#   OGL_FOR_MAC / GLES_DIR = ANGLE headers + libGLESv2/libEGL
#
# Examples:
#   make FREEGLUT=~/Github/freeglut-sdl2-ogles2 FREEGLUT_BUILD=~/Github/freeglut-sdl2-ogles2/build \
#        GL4ES=~/Github/gl4es OGL_FOR_MAC=~/Github/opengl-for-mac

APPNAME = gears_min
SRC     = gears_min.c

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
EM_APP  = $(WEB_DIR)/$(APPNAME).html

# ---- trees (override on the command line or via env) ----
FREEGLUT       ?= ../../..
FREEGLUT_BUILD ?= $(FREEGLUT)/build
GL4ES          ?= $(FREEGLUT)/../gl4es
OGL_FOR_MAC    ?= $(FREEGLUT)/../opengl-for-mac
GLES_DIR       ?= $(OGL_FOR_MAC)

# ---- SDL2 ----
SDL_INC  = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)

# ---- freeglut (Legacy gl4es config) ----
FREEGLUT_INC = -I$(FREEGLUT)/include
# freeglut's lib name varies (libglut / libfreeglut). Pick whichever exists.
FREEGLUT_LIBDIR = $(firstword $(wildcard $(FREEGLUT_BUILD)/lib) $(FREEGLUT_BUILD))
FREEGLUT_LIB    = $(firstword \
    $(wildcard $(FREEGLUT_LIBDIR)/libfreeglut.a) \
    $(wildcard $(FREEGLUT_LIBDIR)/libglut.a) \
    $(wildcard $(FREEGLUT_LIBDIR)/libfreeglut.$(DYL_EXT)) \
    $(wildcard $(FREEGLUT_LIBDIR)/libglut.$(DYL_EXT)))

# ---- gl4es (static; headers FIRST so <GL/gl.h> resolves to gl4es') ----
GL4ES_INC = -I$(GL4ES)/include
GL4ES_LIB = $(GL4ES)/lib/libGL.a

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
# -DFREEGLUT_SDL2_GL4ES: route <GL/freeglut.h> to gl4es' (mangled) GL headers,
#   matching how freeglut itself was built; without it the demo picks up the
#   platform desktop-GL headers and references unmangled gl* symbols that the
#   gl4es libGL (which exports mgl*) does not provide.
# -DFREEGLUT_STATIC: we link freeglut's static lib.
INCS = $(GL4ES_INC) $(FREEGLUT_INC) $(SDL_INC) -DFREEGLUT_SDL2_GL4ES -DFREEGLUT_STATIC

CC   = cc
EMCC = emcc -s WASM=1
OPT    = -O2 -g
EM_OPT = -O2
WARN = -Wall -Wextra -Wno-unused-parameter

# Emscripten: SDL2 + FULL_ES2; freeglut/gl4es-for-web would be needed for a real
# web build (see note below). Frames driven by glutMainLoop.
EM_SDL_FLAGS    = -s USE_SDL=2 -s FULL_ES2=1
EM_OUTPUT_FLAGS = -s ALLOW_MEMORY_GROWTH=1 -s EXIT_RUNTIME=0

all: native

native: $(APP)

$(BIN_DIR) $(WEB_DIR):
	mkdir -p $@

# ---- native link ----
# Order: app, freeglut, gl4es (static), then ANGLE GLES/EGL, SDL, libm.
$(APP): $(SRC) | $(BIN_DIR)
	@test -n "$(FREEGLUT_LIB)" || { echo "ERROR: freeglut lib not found under $(FREEGLUT_LIBDIR) -- build freeglut with -DFREEGLUT_SDL2_GL4ES=ON first, or set FREEGLUT_BUILD."; exit 1; }
	@test -f "$(GL4ES_LIB)" || { echo "ERROR: $(GL4ES_LIB) not found -- build gl4es first, or set GL4ES."; exit 1; }
	$(CC) $(OPT) $(WARN) $(INCS) $(SRC) \
		$(FREEGLUT_LIB) $(GL4ES_LIB) \
		$(GLES_LIBS) $(GLES_LINK) $(SDL_LIBS) -lm $(CONSOLE_FLAGS) -o $@
	$(call GLES_INSTALL)
	@echo "BUILT (native): $@"
	@echo "Run: DYLD_FALLBACK_LIBRARY_PATH=$(GL4ES)/lib:$(GLES_LIBS_PATH) $@   (mac)"

run: native
	DYLD_FALLBACK_LIBRARY_PATH=$(GL4ES)/lib:$(GLES_LIBS_PATH) \
	LD_LIBRARY_PATH=$(GL4ES)/lib:$(GLES_LIBS_PATH) $(APP)

# ---- emscripten (placeholder) ----
# A real web build needs freeglut and gl4es built for Emscripten too; wire those
# in once the native path is proven. Left here so the target exists.
browser: $(SRC) | $(WEB_DIR)
	@echo "NOTE: web build needs freeglut + gl4es compiled for Emscripten."
	@echo "      Native first; revisit per flwbox-port's EM_* recipe."

clean:
	rm -rf $(BIN_DIR)
	rm -f $(WEB_DIR)/$(APPNAME).html $(WEB_DIR)/$(APPNAME).js $(WEB_DIR)/$(APPNAME).wasm

.PHONY: all native browser run clean
