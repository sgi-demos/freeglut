#!/bin/sh
# build-full-debug.sh — clean rebuild gl4es, freeglut, and newave in debug
set -o verbose
set -e

# 1) gl4es — Debug
cd ../gl4es && rm -rf build && mkdir build && cd build
cmake -DNOX11=ON -DNOEGL=ON -DDEFAULT_ES=2 -DSTATICLIB=ON -DCMAKE_BUILD_TYPE=Debug ..
make
cd ..

# 2) freeglut — Debug (Legacy gl4es config)
export GL4ES=../gl4es
cd ../freeglut-sdl2-ogles2 && rm -rf build && mkdir build && cd build
cmake -DFREEGLUT_SDL2_GL4ES=ON \
      -DFREEGLUT_BUILD_SHARED_LIBS=OFF -DFREEGLUT_BUILD_STATIC_LIBS=ON \
      -DFREEGLUT_REPLACE_GLUT=ON \
      -DGL4ES_LIBRARY=$GL4ES/lib/libGL.a -DGL4ES_INCLUDE_DIR=$GL4ES/include \
      -DFREEGLUT_BUILD_DEMOS=OFF -DCMAKE_BUILD_TYPE=Debug ..
make
cd ..

# 3) glues + newave — -O0 -g, absolute lib paths
cd ../freeglut-sdl2-ogles2/progs/demos/newave
make -f newave.mk clean
make -f newave.mk native OPT="-O0 -g" \
     GL4ES=$HOME/Github/gl4es GLUES=$HOME/Github/glues \
     OGL_FOR_MAC=$HOME/Github/opengl-for-mac
# (glues inherits OPT via the recursive make; this rebuilds libglues-native.a at -O0 too)
