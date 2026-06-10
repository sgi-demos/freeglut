/*
 * fg_gles2_compat.c
 *
 * Implementation of the minimal GL 1.x emulation used by freeglut's menu
 * and font code when running on OpenGL ES 2.0.  See fg_gles2_compat.h.
 *
 * Design notes:
 *  - Two 4x4 matrix stacks (modelview/projection), a current color, and an
 *    immediate-mode vertex accumulator flushed at fghCompatEnd().
 *  - GL_QUADS / GL_QUAD_STRIP are converted to triangles.
 *  - glBitmap is emulated with one small GL_ALPHA texture per glyph,
 *    cached by bitmap pointer (the glyph data lives in static font tables,
 *    so pointers are stable).  The fragment shader discards texels whose
 *    alpha is below 0.5, matching glBitmap's "set pixels only where bits
 *    are 1" behavior without requiring blending.
 *  - Shader programs are not shared between GL contexts, so a small
 *    per-context program cache is kept (menu windows have own contexts).
 *
 * License: MIT (same as freeglut)
 */

#define FREEGLUT_BUILDING_LIB
#include <GL/freeglut.h>
#include "fg_internal.h"

#include <string.h>
#include <stdlib.h>

/* We want the *real* GLES2 functions here, so do not include
   fg_gles2_compat.h's macro section.  Declare our entry points manually. */
#include <GLES2/gl2.h>

void fghCompatMatrixMode( GLenum mode );
void fghCompatPushMatrix( void );
void fghCompatPopMatrix( void );
void fghCompatLoadIdentity( void );
void fghCompatOrtho( double l, double r, double b, double t, double n, double f );
void fghCompatTranslatef( GLfloat x, GLfloat y, GLfloat z );
void fghCompatColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a );
void fghCompatColor4fv( const GLfloat *c );
void fghCompatBegin( GLenum mode );
void fghCompatEnd( void );
void fghCompatVertex2f( GLfloat x, GLfloat y );
void fghCompatVertex2i( GLint x, GLint y );
void fghCompatRasterPos2i( GLint x, GLint y );
void fghCompatBitmap( GLsizei width, GLsizei height, GLfloat xorig,
                      GLfloat yorig, GLfloat xmove, GLfloat ymove,
                      const GLubyte *bitmap );
void fghCompatPushAttrib( GLbitfield mask );
void fghCompatPopAttrib( void );
void fghCompatEnable( GLenum cap );
void fghCompatDisable( GLenum cap );
void fghCompatPixelStorei( GLenum pname, GLint param );
void fghCompatGetIntegerv( GLenum pname, GLint *params );

#ifndef GL_MODELVIEW
#define GL_MODELVIEW             0x1700
#define GL_PROJECTION            0x1701
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING              0x0B50
#endif
#ifndef GL_QUADS
#define GL_QUADS                 0x0007
#define GL_QUAD_STRIP            0x0008
#endif
#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES     0x0CF0
#define GL_UNPACK_LSB_FIRST      0x0CF1
#define GL_UNPACK_ROW_LENGTH     0x0CF2
#define GL_UNPACK_SKIP_ROWS      0x0CF3
#define GL_UNPACK_SKIP_PIXELS    0x0CF4
#endif

/* -- MATRIX MATH ----------------------------------------------------------- */

#define MTX_STACK_DEPTH 8

typedef struct { GLfloat m[16]; } fghMat4;  /* column-major, like GL */

static fghMat4 fghStacks[2][MTX_STACK_DEPTH]; /* 0: modelview, 1: projection */
static int     fghStackTop[2] = { 0, 0 };
static int     fghCurStack = 0;
static int     fghInited = 0;

static void fghMatIdentity( fghMat4 *m )
{
    memset( m->m, 0, sizeof m->m );
    m->m[0] = m->m[5] = m->m[10] = m->m[15] = 1.f;
}

static void fghMatMul( fghMat4 *out, const fghMat4 *a, const fghMat4 *b )
{
    fghMat4 r;
    int i, j, k;
    for( i = 0; i < 4; i++ )
        for( j = 0; j < 4; j++ )
        {
            GLfloat s = 0.f;
            for( k = 0; k < 4; k++ )
                s += a->m[ k*4 + j ] * b->m[ i*4 + k ];
            r.m[ i*4 + j ] = s;
        }
    *out = r;
}

static void fghLazyInit( void )
{
    if( !fghInited )
    {
        fghMatIdentity( &fghStacks[0][0] );
        fghMatIdentity( &fghStacks[1][0] );
        fghInited = 1;
    }
}

static fghMat4 *fghCur( void )
{
    fghLazyInit();
    return &fghStacks[ fghCurStack ][ fghStackTop[ fghCurStack ] ];
}

void fghCompatMatrixMode( GLenum mode )
{
    fghLazyInit();
    fghCurStack = ( mode == GL_PROJECTION ) ? 1 : 0;
}

void fghCompatPushMatrix( void )
{
    int s = fghCurStack;
    fghLazyInit();
    if( fghStackTop[s] < MTX_STACK_DEPTH - 1 )
    {
        fghStacks[s][ fghStackTop[s] + 1 ] = fghStacks[s][ fghStackTop[s] ];
        fghStackTop[s]++;
    }
}

void fghCompatPopMatrix( void )
{
    int s = fghCurStack;
    if( fghStackTop[s] > 0 )
        fghStackTop[s]--;
}

void fghCompatLoadIdentity( void )
{
    fghMatIdentity( fghCur() );
}

void fghCompatOrtho( double l, double r, double b, double t,
                     double n, double f )
{
    fghMat4 o;
    fghMatIdentity( &o );
    o.m[0]  = (GLfloat)( 2.0 / ( r - l ) );
    o.m[5]  = (GLfloat)( 2.0 / ( t - b ) );
    o.m[10] = (GLfloat)( -2.0 / ( f - n ) );
    o.m[12] = (GLfloat)( -( r + l ) / ( r - l ) );
    o.m[13] = (GLfloat)( -( t + b ) / ( t - b ) );
    o.m[14] = (GLfloat)( -( f + n ) / ( f - n ) );
    fghMatMul( fghCur(), fghCur(), &o );
}

void fghCompatTranslatef( GLfloat x, GLfloat y, GLfloat z )
{
    fghMat4 t;
    fghMatIdentity( &t );
    t.m[12] = x; t.m[13] = y; t.m[14] = z;
    fghMatMul( fghCur(), fghCur(), &t );
}

/* -- SHADER PROGRAM (per-context) ------------------------------------------ */

static const char *fghVertSrc =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "    gl_PointSize = 1.0;\n"
    "}\n";

static const char *fghFragSrc =
    "precision mediump float;\n"
    "uniform vec4 u_color;\n"
    "uniform bool u_useTex;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    if (u_useTex && texture2D(u_tex, v_uv).a < 0.5) discard;\n"
    "    gl_FragColor = u_color;\n"
    "}\n";

typedef struct
{
    void   *ctxKey;             /* opaque current-context identity */
    GLuint  prog;
    GLint   aPos, aUV, uMVP, uColor, uUseTex, uTex;
} fghProgramSlot;

#define MAX_PROGRAM_SLOTS 16
static fghProgramSlot fghProgs[ MAX_PROGRAM_SLOTS ];
static int fghProgCount = 0;

/* The current GL context identity.  On freeglut, the current window's
   context serves; fall back to a single slot if unknown. */
static void *fghContextKey( void )
{
    if( fgStructure.CurrentWindow )
        return (void *)fgStructure.CurrentWindow->Window.Context;
    return NULL;
}

static GLuint fghCompileShader( GLenum type, const char *src )
{
    GLuint sh = glCreateShader( type );
    GLint ok = 0;
    glShaderSource( sh, 1, &src, NULL );
    glCompileShader( sh );
    glGetShaderiv( sh, GL_COMPILE_STATUS, &ok );
    if( !ok )
    {
        char log[512];
        glGetShaderInfoLog( sh, sizeof log, NULL, log );
        fgWarning( "fg_gles2_compat: shader compile failed: %s", log );
    }
    return sh;
}

static fghProgramSlot *fghGetProgram( void )
{
    void *key = fghContextKey();
    int i;
    fghProgramSlot *slot;
    GLuint vs, fs, prog;

    for( i = 0; i < fghProgCount; i++ )
        if( fghProgs[i].ctxKey == key )
            return &fghProgs[i];

    if( fghProgCount >= MAX_PROGRAM_SLOTS )
        fghProgCount = 0;     /* recycle; stale programs leak but bounded */

    vs = fghCompileShader( GL_VERTEX_SHADER, fghVertSrc );
    fs = fghCompileShader( GL_FRAGMENT_SHADER, fghFragSrc );
    prog = glCreateProgram();
    glAttachShader( prog, vs );
    glAttachShader( prog, fs );
    glLinkProgram( prog );
    glDeleteShader( vs );
    glDeleteShader( fs );

    slot = &fghProgs[ fghProgCount++ ];
    slot->ctxKey = key;
    slot->prog   = prog;
    slot->aPos   = glGetAttribLocation( prog, "a_pos" );
    slot->aUV    = glGetAttribLocation( prog, "a_uv" );
    slot->uMVP   = glGetUniformLocation( prog, "u_mvp" );
    slot->uColor = glGetUniformLocation( prog, "u_color" );
    slot->uUseTex= glGetUniformLocation( prog, "u_tex" ) >= 0
                   ? glGetUniformLocation( prog, "u_useTex" ) : -1;
    slot->uTex   = glGetUniformLocation( prog, "u_tex" );
    return slot;
}

/* -- DRAW-STATE SAVE/RESTORE ------------------------------------------------ */

typedef struct
{
    GLint   prog, arrayBuf, elemBuf, activeTex, tex2D;
    GLint   posEnabled, uvEnabled;
    GLboolean depth, cull, blend;
} fghSavedState;

static void fghSaveState( fghSavedState *s, GLint aPos, GLint aUV )
{
    glGetIntegerv( GL_CURRENT_PROGRAM, &s->prog );
    glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &s->arrayBuf );
    glGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, &s->elemBuf );
    glGetIntegerv( GL_ACTIVE_TEXTURE, &s->activeTex );
    glActiveTexture( GL_TEXTURE0 );
    glGetIntegerv( GL_TEXTURE_BINDING_2D, &s->tex2D );
    glGetVertexAttribiv( aPos, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                         &s->posEnabled );
    if( aUV >= 0 )
        glGetVertexAttribiv( aUV, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                             &s->uvEnabled );
    s->depth = glIsEnabled( GL_DEPTH_TEST );
    s->cull  = glIsEnabled( GL_CULL_FACE );
    s->blend = glIsEnabled( GL_BLEND );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}

static void fghRestoreState( const fghSavedState *s, GLint aPos, GLint aUV )
{
    glBindBuffer( GL_ARRAY_BUFFER, (GLuint)s->arrayBuf );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, (GLuint)s->elemBuf );
    glBindTexture( GL_TEXTURE_2D, (GLuint)s->tex2D );
    glActiveTexture( (GLenum)s->activeTex );
    if( s->posEnabled ) glEnableVertexAttribArray( aPos );
    else                glDisableVertexAttribArray( aPos );
    if( aUV >= 0 )
    {
        if( s->uvEnabled ) glEnableVertexAttribArray( aUV );
        else               glDisableVertexAttribArray( aUV );
    }
    if( s->depth ) glEnable( GL_DEPTH_TEST ); else glDisable( GL_DEPTH_TEST );
    if( s->cull )  glEnable( GL_CULL_FACE );  else glDisable( GL_CULL_FACE );
    if( s->blend ) glEnable( GL_BLEND );      else glDisable( GL_BLEND );
    glUseProgram( (GLuint)s->prog );
}

/* -- COLOR / IMMEDIATE-MODE VERTICES ---------------------------------------- */

static GLfloat fghColor[4] = { 1.f, 1.f, 1.f, 1.f };

#define MAX_IMM_VERTS 1024
static GLfloat fghVerts[ MAX_IMM_VERTS * 2 ];
static int     fghVertCount = 0;
static GLenum  fghImmMode = 0;
static int     fghInBegin = 0;

void fghCompatColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a )
{
    fghColor[0] = r; fghColor[1] = g; fghColor[2] = b; fghColor[3] = a;
}

void fghCompatColor4fv( const GLfloat *c )
{
    memcpy( fghColor, c, 4 * sizeof( GLfloat ) );
}

void fghCompatBegin( GLenum mode )
{
    fghImmMode = mode;
    fghVertCount = 0;
    fghInBegin = 1;
}

void fghCompatVertex2f( GLfloat x, GLfloat y )
{
    if( !fghInBegin || fghVertCount >= MAX_IMM_VERTS )
        return;
    fghVerts[ fghVertCount*2 + 0 ] = x;
    fghVerts[ fghVertCount*2 + 1 ] = y;
    fghVertCount++;
}

void fghCompatVertex2i( GLint x, GLint y )
{
    fghCompatVertex2f( (GLfloat)x, (GLfloat)y );
}

static void fghComputeMVP( fghMat4 *out )
{
    fghLazyInit();
    /* clip = projection * modelview */
    fghMatMul( out, &fghStacks[1][ fghStackTop[1] ],
                    &fghStacks[0][ fghStackTop[0] ] );
}

void fghCompatEnd( void )
{
    fghProgramSlot *p;
    fghSavedState save;
    fghMat4 mvp;
    GLenum mode = fghImmMode;
    GLfloat *verts = fghVerts;
    int count = fghVertCount;
    GLfloat quadbuf[ MAX_IMM_VERTS * 3 ]; /* worst case quads->tris: 1.5x */

    fghInBegin = 0;
    if( count < 1 )
        return;

    /* convert legacy primitives */
    if( mode == GL_QUADS )
    {
        int q, nq = count / 4, out = 0;
        for( q = 0; q < nq; q++ )
        {
            const GLfloat *v = fghVerts + q*8;
            /* v0 v1 v2  v0 v2 v3 */
            memcpy( quadbuf + out, v + 0, 6*sizeof(GLfloat) ); out += 6;
            memcpy( quadbuf + out, v + 0, 2*sizeof(GLfloat) ); out += 2;
            memcpy( quadbuf + out, v + 4, 4*sizeof(GLfloat) ); out += 4;
        }
        verts = quadbuf;
        count = nq * 6;
        mode = GL_TRIANGLES;
    }
    else if( mode == GL_QUAD_STRIP )
        mode = GL_TRIANGLE_STRIP;   /* identical vertex ordering */

    p = fghGetProgram();
    fghSaveState( &save, p->aPos, p->aUV );

    glUseProgram( p->prog );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_CULL_FACE );
    glDisable( GL_BLEND );

    fghComputeMVP( &mvp );
    glUniformMatrix4fv( p->uMVP, 1, GL_FALSE, mvp.m );
    glUniform4fv( p->uColor, 1, fghColor );
    if( p->uUseTex >= 0 ) glUniform1i( p->uUseTex, 0 );

    glEnableVertexAttribArray( p->aPos );
    glVertexAttribPointer( p->aPos, 2, GL_FLOAT, GL_FALSE, 0, verts );
    if( p->aUV >= 0 )
        glDisableVertexAttribArray( p->aUV );

    glDrawArrays( mode, 0, count );

    fghRestoreState( &save, p->aPos, p->aUV );
}

/* -- RASTER POS / BITMAP ----------------------------------------------------- */

static GLfloat fghRasterPos[2] = { 0.f, 0.f };  /* GL window coords, y up */
static int     fghRasterValid = 1;  /* GL spec: initial raster pos is valid at (0,0) */

void fghCompatRasterPos2i( GLint x, GLint y )
{
    fghMat4 mvp;
    GLfloat clip[4];
    GLint vp[4];
    int i;

    fghComputeMVP( &mvp );
    for( i = 0; i < 4; i++ )
        clip[i] = mvp.m[ 0*4 + i ] * (GLfloat)x
                + mvp.m[ 1*4 + i ] * (GLfloat)y
                + mvp.m[ 3*4 + i ];
    if( clip[3] == 0.f )
        return;
    glGetIntegerv( GL_VIEWPORT, vp );
    fghRasterPos[0] = vp[0] + ( clip[0]/clip[3] * 0.5f + 0.5f ) * vp[2];
    fghRasterPos[1] = vp[1] + ( clip[1]/clip[3] * 0.5f + 0.5f ) * vp[3];
    fghRasterValid = 1;
}

/* glyph texture cache, keyed by bitmap pointer (static font data) */
typedef struct { const GLubyte *key; GLuint tex; int w, h; } fghGlyphTex;
#define MAX_GLYPH_TEXTURES 1024
static fghGlyphTex fghGlyphs[ MAX_GLYPH_TEXTURES ];
static int fghGlyphCount = 0;

static GLuint fghGetGlyphTexture( const GLubyte *bitmap, int w, int h )
{
    int i, x, y, rowBytes;
    GLubyte *pixels;
    GLuint tex;
    GLint oldTex, oldAlign;

    for( i = 0; i < fghGlyphCount; i++ )
        if( fghGlyphs[i].key == bitmap &&
            fghGlyphs[i].w == w && fghGlyphs[i].h == h )
            return fghGlyphs[i].tex;

    /* decode the packed 1-bpp bitmap (MSB first, rows bottom-up,
       byte-aligned rows: UNPACK_ALIGNMENT==1) into 8-bit alpha */
    rowBytes = ( w + 7 ) / 8;
    pixels = malloc( (size_t)( w > 0 ? w : 1 ) * ( h > 0 ? h : 1 ) );
    if( !pixels )
        return 0;
    for( y = 0; y < h; y++ )
        for( x = 0; x < w; x++ )
        {
            GLubyte byte = bitmap[ y * rowBytes + ( x >> 3 ) ];
            pixels[ y * w + x ] =
                ( byte & ( 0x80 >> ( x & 7 ) ) ) ? 0xFF : 0x00;
        }

    glGetIntegerv( GL_TEXTURE_BINDING_2D, &oldTex );
    glGetIntegerv( GL_UNPACK_ALIGNMENT, &oldAlign );
    glGenTextures( 1, &tex );
    glBindTexture( GL_TEXTURE_2D, tex );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0,
                  GL_ALPHA, GL_UNSIGNED_BYTE, pixels );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glPixelStorei( GL_UNPACK_ALIGNMENT, oldAlign );
    glBindTexture( GL_TEXTURE_2D, (GLuint)oldTex );
    free( pixels );

    if( fghGlyphCount < MAX_GLYPH_TEXTURES )
    {
        fghGlyphs[ fghGlyphCount ].key = bitmap;
        fghGlyphs[ fghGlyphCount ].w = w;
        fghGlyphs[ fghGlyphCount ].h = h;
        fghGlyphs[ fghGlyphCount ].tex = tex;
        fghGlyphCount++;
    }
    return tex;
}

void fghCompatBitmap( GLsizei width, GLsizei height,
                      GLfloat xorig, GLfloat yorig,
                      GLfloat xmove, GLfloat ymove,
                      const GLubyte *bitmap )
{
    if( bitmap && width > 0 && height > 0 && fghRasterValid )
    {
        fghProgramSlot *p = fghGetProgram();
        fghSavedState save;
        GLint vp[4];
        GLuint tex = fghGetGlyphTexture( bitmap, width, height );
        GLfloat x0 = fghRasterPos[0] - xorig;
        GLfloat y0 = fghRasterPos[1] - yorig;
        GLfloat x1 = x0 + (GLfloat)width;
        GLfloat y1 = y0 + (GLfloat)height;
        /* pixel-space ortho over the viewport, y up (GL window coords) */
        fghMat4 mvp;
        GLfloat pos[12], uv[12];
        int i;

        static const GLfloat quadUV[12] = {
            0,0,  1,0,  1,1,   0,0,  1,1,  0,1
        };
        const GLfloat quadPos[12] = {
            x0,y0,  x1,y0,  x1,y1,   x0,y0,  x1,y1,  x0,y1
        };
        memcpy( pos, quadPos, sizeof pos );
        memcpy( uv, quadUV, sizeof uv );

        glGetIntegerv( GL_VIEWPORT, vp );
        fghMatIdentity( &mvp );
        mvp.m[0]  =  2.f / (GLfloat)vp[2];
        mvp.m[5]  =  2.f / (GLfloat)vp[3];
        mvp.m[12] = -1.f - 2.f * (GLfloat)vp[0] / (GLfloat)vp[2];
        mvp.m[13] = -1.f - 2.f * (GLfloat)vp[1] / (GLfloat)vp[3];

        fghSaveState( &save, p->aPos, p->aUV );
        glUseProgram( p->prog );
        glDisable( GL_DEPTH_TEST );
        glDisable( GL_CULL_FACE );
        glDisable( GL_BLEND );

        glUniformMatrix4fv( p->uMVP, 1, GL_FALSE, mvp.m );
        glUniform4fv( p->uColor, 1, fghColor );
        if( p->uUseTex >= 0 ) glUniform1i( p->uUseTex, 1 );
        glActiveTexture( GL_TEXTURE0 );
        glBindTexture( GL_TEXTURE_2D, tex );
        glUniform1i( p->uTex, 0 );

        glEnableVertexAttribArray( p->aPos );
        glVertexAttribPointer( p->aPos, 2, GL_FLOAT, GL_FALSE, 0, pos );
        if( p->aUV >= 0 )
        {
            glEnableVertexAttribArray( p->aUV );
            glVertexAttribPointer( p->aUV, 2, GL_FLOAT, GL_FALSE, 0, uv );
        }

        glDrawArrays( GL_TRIANGLES, 0, 6 );

        fghRestoreState( &save, p->aPos, p->aUV );
        (void)i;
    }

    if( fghRasterValid )
    {
        fghRasterPos[0] += xmove;
        fghRasterPos[1] += ymove;
    }
}

/* -- ATTRIB PUSH/POP, ENABLE/DISABLE, PIXEL STORE --------------------------- */

#define ATTRIB_STACK_DEPTH 4
typedef struct { GLboolean depth, cull, blend; } fghAttribSave;
static fghAttribSave fghAttribStack[ ATTRIB_STACK_DEPTH ];
static int fghAttribTop = 0;

void fghCompatPushAttrib( GLbitfield mask )
{
    (void)mask;
    if( fghAttribTop < ATTRIB_STACK_DEPTH )
    {
        fghAttribStack[ fghAttribTop ].depth = glIsEnabled( GL_DEPTH_TEST );
        fghAttribStack[ fghAttribTop ].cull  = glIsEnabled( GL_CULL_FACE );
        fghAttribStack[ fghAttribTop ].blend = glIsEnabled( GL_BLEND );
        fghAttribTop++;
    }
}

void fghCompatPopAttrib( void )
{
    if( fghAttribTop > 0 )
    {
        fghAttribSave *s = &fghAttribStack[ --fghAttribTop ];
        if( s->depth ) glEnable( GL_DEPTH_TEST );
        else           glDisable( GL_DEPTH_TEST );
        if( s->cull )  glEnable( GL_CULL_FACE );
        else           glDisable( GL_CULL_FACE );
        if( s->blend ) glEnable( GL_BLEND );
        else           glDisable( GL_BLEND );
    }
}

static int fghIsES2Cap( GLenum cap )
{
    switch( cap )
    {
    case GL_BLEND: case GL_CULL_FACE: case GL_DEPTH_TEST:
    case GL_DITHER: case GL_POLYGON_OFFSET_FILL:
    case GL_SAMPLE_ALPHA_TO_COVERAGE: case GL_SAMPLE_COVERAGE:
    case GL_SCISSOR_TEST: case GL_STENCIL_TEST:
        return 1;
    default:
        return 0;    /* GL_LIGHTING, GL_TEXTURE_2D-as-cap, etc. */
    }
}

void fghCompatEnable( GLenum cap )
{
    if( fghIsES2Cap( cap ) )
        glEnable( cap );
}

void fghCompatDisable( GLenum cap )
{
    if( fghIsES2Cap( cap ) )
        glDisable( cap );
}

/* fg_font.c saves/restores all classic unpack parameters; ES2 only has
   GL_UNPACK_ALIGNMENT, so emulate the rest as inert state. */
static GLint fghUnpack[5] = { 0, 0, 0, 0, 0 };
/* order: SWAP_BYTES, LSB_FIRST, ROW_LENGTH, SKIP_ROWS, SKIP_PIXELS */

static int fghUnpackIndex( GLenum pname )
{
    switch( pname )
    {
    case GL_UNPACK_SWAP_BYTES:  return 0;
    case GL_UNPACK_LSB_FIRST:   return 1;
    case GL_UNPACK_ROW_LENGTH:  return 2;
    case GL_UNPACK_SKIP_ROWS:   return 3;
    case GL_UNPACK_SKIP_PIXELS: return 4;
    default:                    return -1;
    }
}

void fghCompatPixelStorei( GLenum pname, GLint param )
{
    int i = fghUnpackIndex( pname );
    if( i >= 0 )
        fghUnpack[i] = param;
    else
        glPixelStorei( pname, param );
}

void fghCompatGetIntegerv( GLenum pname, GLint *params )
{
    int i = fghUnpackIndex( pname );
    if( i >= 0 )
        *params = fghUnpack[i];
    else
        glGetIntegerv( pname, params );
}
