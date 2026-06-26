#!/bin/sh
# build-sdl2-gl4es.sh — configure & build freeglut in the SDL2/gl4es Legacy
# configuration (static lib, for unmodified immediate-mode GLUT apps over GLES2).
set -e

GL4ES="${GL4ES:-../gl4es}"

# Fail early if gl4es isn't built, so we don't bake a bad path into the cache.
[ -f "$GL4ES/lib/libGL.a" ] || {
    echo "ERROR: $GL4ES/lib/libGL.a not found. Build gl4es and set GL4ES." >&2
    exit 1
}

cd "$(dirname "$0")"    # run from the freeglut top level regardless of cwd
rm -rf build && mkdir build && cd build

cmake -DFREEGLUT_SDL2_GL4ES=ON \
      -DFREEGLUT_BUILD_SHARED_LIBS=OFF \
      -DFREEGLUT_BUILD_STATIC_LIBS=ON \
      -DFREEGLUT_REPLACE_GLUT=ON \
      -DGL4ES_LIBRARY="$GL4ES/lib/libGL.a" \
      -DGL4ES_INCLUDE_DIR="$GL4ES/include" \
      -DFREEGLUT_BUILD_DEMOS=OFF \
      ..

grep GL4ES CMakeCache.txt     # sanity: should show full paths, not /lib/...

make
echo "Built: $(pwd)/lib/libglut.a"
