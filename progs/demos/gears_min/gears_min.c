/*
 * gears_min.c - GLU-free immediate-mode freeglut demo for validating the
 * freeglut SDL2 "Legacy configuration" (-DFREEGLUT_SDL2_GL4ES=ON), where gl4es
 * translates fixed-function/immediate-mode OpenGL to OpenGL ES 2.0.
 *
 * Deliberately uses NO GLU: projection is set with glFrustum (core GL), not
 * gluPerspective. This isolates the freeglut + gl4es path so we can confirm it
 * works before adding the glues (GLU-ES) dependency.
 *
 * It exercises all three sources of GL calls that the Legacy config must
 * handle through gl4es:
 *   1. the app's own immediate mode: glBegin/glNormal3f/glVertex3f (the floor),
 *   2. freeglut's shapes API: glutSolidTorus / glutWireCube,
 *   3. fixed-function state: depth test, GL_LIGHTING/GL_LIGHT0, glMaterialfv,
 *      the matrix stack (glMatrixMode/glTranslatef/glRotatef/glFrustum).
 * Plus a right-click menu, to exercise freeglut's GUI + GLUT_MENU_IN_WINDOW.
 *
 * Controls: left-drag (or arrow keys) rotate; space toggles spin; right-click
 * menu; q / Esc quit.
 *
 * License: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <GL/freeglut.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int   win_w = 640, win_h = 480;
static float rot_x = 20.0f, rot_y = 30.0f;
static int   spinning = 1;
static int   last_x, last_y, dragging;

static void set_projection( int w, int h )
{
    float aspect = (float)w / (float)( h ? h : 1 );
    float znear = 1.0f, zfar = 50.0f;
    /* vertical half-extent at the near plane for ~50 deg vertical FOV */
    float top   = znear * (float)tan( 25.0 * M_PI / 180.0 );
    float right = top * aspect;

    glMatrixMode( GL_PROJECTION );
    glLoadIdentity( );
    glFrustum( -right, right, -top, top, znear, zfar );   /* core GL, no GLU */
    glMatrixMode( GL_MODELVIEW );
}

static void reshape( int w, int h )
{
    win_w = w; win_h = h;
    glViewport( 0, 0, w, h );
    set_projection( w, h );
}

static void draw_floor( void )
{
    int i;
    glDisable( GL_LIGHTING );
    glColor3f( 0.25f, 0.25f, 0.30f );
    glBegin( GL_LINES );
    for( i = -10; i <= 10; ++i )
    {
        glVertex3f( (float)i, -1.5f, -10.0f ); glVertex3f( (float)i, -1.5f, 10.0f );
        glVertex3f( -10.0f, -1.5f, (float)i ); glVertex3f( 10.0f, -1.5f, (float)i );
    }
    glEnd( );
    glEnable( GL_LIGHTING );
}

static void display( void )
{
    GLfloat torus_mat[]  = { 0.20f, 0.55f, 0.85f, 1.0f };
    GLfloat cube_mat[]   = { 0.85f, 0.45f, 0.20f, 1.0f };
    GLfloat light_pos[]  = { 4.0f, 6.0f, 8.0f, 0.0f };

    glClearColor( 0.08f, 0.08f, 0.12f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glLoadIdentity( );
    glTranslatef( 0.0f, 0.0f, -8.0f );
    glLightfv( GL_LIGHT0, GL_POSITION, light_pos );
    glRotatef( rot_x, 1.0f, 0.0f, 0.0f );
    glRotatef( rot_y, 0.0f, 1.0f, 0.0f );

    draw_floor( );

    glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, torus_mat );
    glColor3fv( torus_mat );
    glutSolidTorus( 0.4, 1.2, 24, 36 );

    glPushMatrix( );
    glTranslatef( 0.0f, 0.0f, 0.0f );
    glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, cube_mat );
    glColor3fv( cube_mat );
    glDisable( GL_LIGHTING );
    glutWireCube( 2.6 );
    glEnable( GL_LIGHTING );
    glPopMatrix( );

    glutSwapBuffers( );
}

static void idle( void )
{
    if( spinning )
    {
        rot_y += 0.4f;
        if( rot_y >= 360.0f ) rot_y -= 360.0f;
        glutPostRedisplay( );
    }
}

static void keyboard( unsigned char key, int x, int y )
{
    (void)x; (void)y;
    switch( key )
    {
        case 'q': case 'Q': case 27: exit( 0 ); break;
        case ' ': spinning = !spinning; glutPostRedisplay( ); break;
        default: break;
    }
}

static void special( int key, int x, int y )
{
    (void)x; (void)y;
    switch( key )
    {
        case GLUT_KEY_LEFT:  rot_y -= 5.0f; break;
        case GLUT_KEY_RIGHT: rot_y += 5.0f; break;
        case GLUT_KEY_UP:    rot_x -= 5.0f; break;
        case GLUT_KEY_DOWN:  rot_x += 5.0f; break;
        default: return;
    }
    glutPostRedisplay( );
}

static void mouse( int button, int state, int x, int y )
{
    if( button == GLUT_LEFT_BUTTON )
    {
        dragging = ( state == GLUT_DOWN );
        last_x = x; last_y = y;
    }
}

static void motion( int x, int y )
{
    if( dragging )
    {
        rot_y += ( x - last_x ) * 0.5f;
        rot_x += ( y - last_y ) * 0.5f;
        last_x = x; last_y = y;
        glutPostRedisplay( );
    }
}

static void menu( int item )
{
    switch( item )
    {
        case 1: spinning = !spinning; break;
        case 2: rot_x = 20.0f; rot_y = 30.0f; break;
        case 3: exit( 0 ); break;
        default: break;
    }
    glutPostRedisplay( );
}

int main( int argc, char **argv )
{
    glutInit( &argc, argv );
    glutInitWindowSize( win_w, win_h );
    glutInitDisplayMode( GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH );
    glutCreateWindow( "freeglut + gl4es: GLU-free immediate-mode demo" );

    /* In-window menu overlay: required under Emscripten (single canvas) and
       harmless on the desktop. */
    glutSetOption( GLUT_MENU_IN_WINDOW, 1 );

    glutDisplayFunc( display );
    glutReshapeFunc( reshape );
    glutKeyboardFunc( keyboard );
    glutSpecialFunc( special );
    glutMouseFunc( mouse );
    glutMotionFunc( motion );
    glutIdleFunc( idle );

    glutCreateMenu( menu );
    glutAddMenuEntry( "Toggle spin", 1 );
    glutAddMenuEntry( "Reset view", 2 );
    glutAddMenuEntry( "Quit", 3 );
    glutAttachMenu( GLUT_RIGHT_BUTTON );

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_LIGHTING );
    glEnable( GL_LIGHT0 );
    glEnable( GL_NORMALIZE );

    glutMainLoop( );
    return 0;
}
