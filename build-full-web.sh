#!/bin/sh
# build-full-web.sh — clean rebuild gl4es, freeglut, glues, and newave for the
# web (Emscripten/wasm). Run from the freeglut repo root, same as
# build-full-debug.sh. Requires the freeglut CMake Emscripten/SDL2 patch applied
# and an active emsdk (emcc / emcmake / emmake on PATH).
set -o verbose
set -e

# 1) gl4es — web (wasm). Rename the static lib so it can't clobber the native
#    libGL.a / libGL-native.a (gl4es always writes to the source-tree lib/).
cd ../gl4es && rm -rf build-web && mkdir build-web && cd build-web
emcmake cmake -DNOX11=ON -DNOEGL=ON -DDEFAULT_ES=2 -DSTATICLIB=ON ..
emmake make
cd ..
mv lib/libGL.a lib/libGL-web.a

# 2) freeglut — web (Legacy gl4es config). Under emcc SDL2 comes from the port
#    (-sUSE_SDL=2), so the Emscripten/SDL2 CMake patch must be applied.
export GL4ES=../gl4es
cd ../freeglut-sdl2-ogles2 && rm -rf build-web && mkdir build-web && cd build-web
emcmake cmake -DFREEGLUT_SDL2_GL4ES=ON \
      -DFREEGLUT_BUILD_SHARED_LIBS=OFF -DFREEGLUT_BUILD_STATIC_LIBS=ON \
      -DFREEGLUT_REPLACE_GLUT=ON \
      -DGL4ES_LIBRARY=$GL4ES/lib/libGL-web.a -DGL4ES_INCLUDE_DIR=$GL4ES/include \
      -DFREEGLUT_BUILD_DEMOS=OFF ..
emmake make
cd ..

# 3) glues + newave — web. newave.mk's browser target builds libglues-web.a on
#    demand and links freeglut-web + glues-web + gl4es-web into newave.html.
#    FREEGLUT / EM_FREEGLUT_BUILD default to ../../.. and $(FREEGLUT)/build-web.
cd ../freeglut-sdl2-ogles2/progs/demos/newave
make -f newave.mk browser \
     GL4ES=$HOME/Github/gl4es GLUES=$HOME/Github/glues
# -> web/newave.html ; run with: emrun web/newave.html
