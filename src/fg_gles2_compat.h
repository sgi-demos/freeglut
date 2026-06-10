/*
 * fg_gles2_compat.h
 *
 * Minimal fixed-function/immediate-mode emulation for OpenGL ES 2.0,
 * covering exactly the GL 1.x subset used by fg_menu.c and fg_font.c
 * (menu boxes, bitmap fonts, stroke fonts).  Include this header from
 * those files only, when building for OpenGL ES 2.0.
 *
 * License: MIT (same as freeglut)
 */

#ifndef FREEGLUT_GLES2_COMPAT_H
#define FREEGLUT_GLES2_COMPAT_H

/* -- Legacy tokens missing from ES2 headers -------------------------------- */
#ifndef GL_QUADS
#define GL_QUADS                 0x0007
#endif
#ifndef GL_QUAD_STRIP
#define GL_QUAD_STRIP            0x0008
#endif
#ifndef GL_MODELVIEW
#define GL_MODELVIEW             0x1700
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION            0x1701
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING              0x0B50
#endif
#ifndef GL_UNPACK_SWAP_BYTES
#define GL_UNPACK_SWAP_BYTES     0x0CF0
#endif
#ifndef GL_UNPACK_LSB_FIRST
#define GL_UNPACK_LSB_FIRST      0x0CF1
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH     0x0CF2
#endif
#ifndef GL_UNPACK_SKIP_ROWS
#define GL_UNPACK_SKIP_ROWS      0x0CF3
#endif
#ifndef GL_UNPACK_SKIP_PIXELS
#define GL_UNPACK_SKIP_PIXELS    0x0CF4
#endif
#ifndef GL_TEXTURE_BIT
#define GL_TEXTURE_BIT           0x00040000
#endif
#ifndef GL_LIGHTING_BIT
#define GL_LIGHTING_BIT          0x00000040
#endif
#ifndef GL_POLYGON_BIT
#define GL_POLYGON_BIT           0x00000008
#endif

/* -- Compat entry points ---------------------------------------------------- */
void fghCompatMatrixMode( GLenum mode );
void fghCompatPushMatrix( void );
void fghCompatPopMatrix( void );
void fghCompatLoadIdentity( void );
void fghCompatOrtho( double l, double r, double b, double t,
                     double n, double f );
void fghCompatTranslatef( GLfloat x, GLfloat y, GLfloat z );

void fghCompatColor4f( GLfloat r, GLfloat g, GLfloat b, GLfloat a );
void fghCompatColor4fv( const GLfloat *c );

void fghCompatBegin( GLenum mode );
void fghCompatEnd( void );
void fghCompatVertex2f( GLfloat x, GLfloat y );
void fghCompatVertex2i( GLint x, GLint y );

void fghCompatRasterPos2i( GLint x, GLint y );
void fghCompatBitmap( GLsizei width, GLsizei height,
                      GLfloat xorig, GLfloat yorig,
                      GLfloat xmove, GLfloat ymove,
                      const GLubyte *bitmap );

void fghCompatPushAttrib( GLbitfield mask );
void fghCompatPopAttrib( void );
void fghCompatEnable( GLenum cap );
void fghCompatDisable( GLenum cap );
void fghCompatPixelStorei( GLenum pname, GLint param );
void fghCompatGetIntegerv( GLenum pname, GLint *params );

/* -- Remap the GL 1.x names used in fg_menu.c / fg_font.c ------------------ */
#define glMatrixMode    fghCompatMatrixMode
#define glPushMatrix    fghCompatPushMatrix
#define glPopMatrix     fghCompatPopMatrix
#define glLoadIdentity  fghCompatLoadIdentity
#define glOrtho         fghCompatOrtho
#define glTranslatef    fghCompatTranslatef
#define glColor4f       fghCompatColor4f
#define glColor4fv      fghCompatColor4fv
#define glBegin         fghCompatBegin
#define glEnd           fghCompatEnd
#define glVertex2f      fghCompatVertex2f
#define glVertex2i      fghCompatVertex2i
#define glRasterPos2i   fghCompatRasterPos2i
#define glBitmap        fghCompatBitmap
#define glPushAttrib    fghCompatPushAttrib
#define glPopAttrib     fghCompatPopAttrib
#define glEnable        fghCompatEnable
#define glDisable       fghCompatDisable
#define glPixelStorei   fghCompatPixelStorei
#define glGetIntegerv   fghCompatGetIntegerv

#endif /* FREEGLUT_GLES2_COMPAT_H */
