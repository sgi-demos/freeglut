/*
 * fg_init_sdl2.c
 *
 * SDL2 backend: initialization, display metrics, state queries, structure,
 * proc address lookup, cursor, game mode, and colormap stubs.
 *
 * License: MIT (same as freeglut, see fg_internal_sdl2.h)
 */

#define FREEGLUT_BUILDING_LIB
#include <GL/freeglut.h>
#include "../fg_internal.h"
#include <SDL.h>
#include <stdio.h>

/* -- INITIALIZATION ------------------------------------------------------- */

void fgPlatformInitialize( const char* displayName )
{
    SDL_DisplayMode mode;

    (void)displayName;

    if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS ) != 0 )
        fgError( "SDL_Init failed: %s", SDL_GetError() );

    fgDisplay.pDisplay.DisplayIndex = 0;

    if( SDL_GetCurrentDisplayMode( 0, &mode ) == 0 )
    {
        float hdpi = 0.f, vdpi = 0.f;
        fgDisplay.ScreenWidth  = mode.w;
        fgDisplay.ScreenHeight = mode.h;
        if( SDL_GetDisplayDPI( 0, NULL, &hdpi, &vdpi ) == 0 && hdpi > 1.f )
        {
            fgDisplay.ScreenWidthMM  = (int)( mode.w * 25.4f / hdpi );
            fgDisplay.ScreenHeightMM = (int)( mode.h * 25.4f / vdpi );
        }
        else
        {
            /* assume ~96 dpi when SDL can't tell us */
            fgDisplay.ScreenWidthMM  = (int)( mode.w * 25.4f / 96.f );
            fgDisplay.ScreenHeightMM = (int)( mode.h * 25.4f / 96.f );
        }
    }
    else
    {
        fgDisplay.ScreenWidth    = 640;
        fgDisplay.ScreenHeight   = 480;
        fgDisplay.ScreenWidthMM  = 169;
        fgDisplay.ScreenHeightMM = 127;
    }

    fgState.Initialised = GL_TRUE;
    fgState.Time = fgSystemTime();

    atexit( fgDeinitialize );
}

void fgPlatformDeinitialiseInputDevices( void )
{
    /* nothing to do; no serial dial/button devices on SDL2 */
}

void fgPlatformCloseDisplay( void )
{
    SDL_Quit();
}

void fgPlatformDestroyContext( SFG_PlatformDisplay pDisplay,
                               SFG_WindowContextType MContext )
{
    (void)pDisplay;
    if( MContext )
        SDL_GL_DeleteContext( MContext );
}

/* -- DISPLAY -------------------------------------------------------------- */

void fgPlatformGlutSwapBuffers( SFG_PlatformDisplay *pDisplayPtr,
                                SFG_Window *CurrentWindow )
{
    (void)pDisplayPtr;
    if( CurrentWindow && CurrentWindow->Window.Handle )
        SDL_GL_SwapWindow( CurrentWindow->Window.Handle );
}

void fgPlatformSwapInterval( int n )
{
    SDL_GL_SetSwapInterval( n );
}

void fgPlatformInitSwapCtl( void )
{
    /* nothing to do: SDL_GL_SetSwapInterval is always available */
}

/* -- EXTENSIONS / PROC ADDRESSES ------------------------------------------ */

SFG_Proc fgPlatformGetProcAddress( const char *procName )
{
    union { void *p; SFG_Proc fp; } cast;
    cast.p = SDL_GL_GetProcAddress( procName );
    return cast.fp;
}

GLUTproc fgPlatformGetGLUTProcAddress( const char *procName )
{
    (void)procName;
    return NULL;
}

int fgPlatformExtSupported( const char *ext )
{
    return SDL_GL_ExtensionSupported( ext ) == SDL_TRUE;
}

/* -- STATE QUERIES -------------------------------------------------------- */

int fgPlatformGlutGet( GLenum eWhat )
{
    SFG_Window *win = fgStructure.CurrentWindow;

    switch( eWhat )
    {
    case GLUT_WINDOW_X:
    case GLUT_WINDOW_Y:
    {
        int x = 0, y = 0;
        if( !win || !win->Window.Handle ) return 0;
        SDL_GetWindowPosition( win->Window.Handle, &x, &y );
        return eWhat == GLUT_WINDOW_X ? x : y;
    }
    case GLUT_WINDOW_WIDTH:
    case GLUT_WINDOW_HEIGHT:
    {
        int w = 0, h = 0;
        if( !win || !win->Window.Handle ) return 0;
        SDL_GL_GetDrawableSize( win->Window.Handle, &w, &h );
        return eWhat == GLUT_WINDOW_WIDTH ? w : h;
    }
    case GLUT_WINDOW_BORDER_WIDTH:
    case GLUT_WINDOW_HEADER_HEIGHT:
    {
        int top = 0, left = 0, bottom = 0, right = 0;
        if( !win || !win->Window.Handle ) return 0;
        SDL_GetWindowBordersSize( win->Window.Handle,
                                  &top, &left, &bottom, &right );
        return eWhat == GLUT_WINDOW_BORDER_WIDTH ? left : top;
    }
    case GLUT_WINDOW_COLORMAP_SIZE:
        return 0;
    case GLUT_WINDOW_BUFFER_SIZE:
    {
        int r=0,g=0,b=0,a=0;
        SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &r );
        SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &g );
        SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &b );
        SDL_GL_GetAttribute( SDL_GL_ALPHA_SIZE, &a );
        return r+g+b+a;
    }
    case GLUT_WINDOW_STENCIL_SIZE:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &v ); return v;
    }
    case GLUT_WINDOW_DEPTH_SIZE:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &v ); return v;
    }
    case GLUT_WINDOW_RED_SIZE:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &v ); return v;
    }
    case GLUT_WINDOW_GREEN_SIZE:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &v ); return v;
    }
    case GLUT_WINDOW_BLUE_SIZE:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &v ); return v;
    }
    case GLUT_WINDOW_ALPHA_SIZE:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_ALPHA_SIZE, &v ); return v;
    }
    case GLUT_WINDOW_DOUBLEBUFFER:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_DOUBLEBUFFER, &v ); return v;
    }
    case GLUT_WINDOW_RGBA:
        return 1;
    case GLUT_WINDOW_NUM_SAMPLES:
    {
        int v = 0; SDL_GL_GetAttribute( SDL_GL_MULTISAMPLESAMPLES, &v ); return v;
    }
    case GLUT_DISPLAY_MODE_POSSIBLE:
        return 1;
    case GLUT_WINDOW_FORMAT_ID:
        return 0;
    default:
        fgWarning( "glutGet(): missing enum handle %d", eWhat );
        return -1;
    }
}

int fgPlatformGlutDeviceGet( GLenum eWhat )
{
    switch( eWhat )
    {
    case GLUT_HAS_KEYBOARD:
        return 1;
    case GLUT_HAS_MOUSE:
        return 1;
    case GLUT_NUM_MOUSE_BUTTONS:
        return 3;
    default:
        fgWarning( "glutDeviceGet(): missing enum handle %d", eWhat );
        return -1;
    }
}

int *fgPlatformGlutGetModeValues( GLenum eWhat, int *size )
{
    (void)eWhat;
    *size = 0;
    return NULL;
}

/* -- STRUCTURE ------------------------------------------------------------ */

void fgPlatformCreateWindow( SFG_Window *window )
{
    memset( &window->State.pWState, 0, sizeof( SFG_PlatformWindowState ) );
}

/* -- CURSOR --------------------------------------------------------------- */

static SDL_Cursor *fghCursorCache[ SDL_NUM_SYSTEM_CURSORS ];

static void fghSetSystemCursor( SDL_SystemCursor id )
{
    if( !fghCursorCache[ id ] )
        fghCursorCache[ id ] = SDL_CreateSystemCursor( id );
    SDL_SetCursor( fghCursorCache[ id ] );
    SDL_ShowCursor( SDL_ENABLE );
}

void fgPlatformSetCursor( SFG_Window *window, int cursorID )
{
    (void)window;
    switch( cursorID )
    {
    case GLUT_CURSOR_NONE:
        SDL_ShowCursor( SDL_DISABLE );
        break;
    case GLUT_CURSOR_CROSSHAIR:
    case GLUT_CURSOR_FULL_CROSSHAIR:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_CROSSHAIR );
        break;
    case GLUT_CURSOR_TEXT:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_IBEAM );
        break;
    case GLUT_CURSOR_WAIT:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_WAIT );
        break;
    case GLUT_CURSOR_TOP_LEFT_CORNER:
    case GLUT_CURSOR_BOTTOM_RIGHT_CORNER:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_SIZENWSE );
        break;
    case GLUT_CURSOR_TOP_RIGHT_CORNER:
    case GLUT_CURSOR_BOTTOM_LEFT_CORNER:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_SIZENESW );
        break;
    case GLUT_CURSOR_LEFT_RIGHT:
    case GLUT_CURSOR_LEFT_SIDE:
    case GLUT_CURSOR_RIGHT_SIDE:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_SIZEWE );
        break;
    case GLUT_CURSOR_UP_DOWN:
    case GLUT_CURSOR_TOP_SIDE:
    case GLUT_CURSOR_BOTTOM_SIDE:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_SIZENS );
        break;
    case GLUT_CURSOR_DESTROY:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_NO );
        break;
    case GLUT_CURSOR_INHERIT:
    case GLUT_CURSOR_LEFT_ARROW:
    case GLUT_CURSOR_RIGHT_ARROW:
    default:
        fghSetSystemCursor( SDL_SYSTEM_CURSOR_ARROW );
        break;
    }
}

void fgPlatformWarpPointer( int x, int y )
{
    if( fgStructure.CurrentWindow && fgStructure.CurrentWindow->Window.Handle )
        SDL_WarpMouseInWindow( fgStructure.CurrentWindow->Window.Handle, x, y );
}

/* -- GAME MODE ------------------------------------------------------------ */

void fgPlatformRememberState( void ) { }
void fgPlatformRestoreState( void ) { }

GLboolean fgPlatformChangeDisplayMode( GLboolean haveToTest )
{
    /* We only ever use "fullscreen desktop", which always works */
    (void)haveToTest;
    return GL_TRUE;
}

void fgPlatformEnterGameMode( void )
{
    if( fgStructure.GameModeWindow &&
        fgStructure.GameModeWindow->Window.Handle )
        SDL_SetWindowFullscreen( fgStructure.GameModeWindow->Window.Handle,
                                 SDL_WINDOW_FULLSCREEN_DESKTOP );
}

void fgPlatformLeaveGameMode( void )
{
    if( fgStructure.GameModeWindow &&
        fgStructure.GameModeWindow->Window.Handle )
        SDL_SetWindowFullscreen( fgStructure.GameModeWindow->Window.Handle, 0 );
}

GLvoid fgPlatformGetGameModeVMaxExtent( SFG_Window *window, int *x, int *y )
{
    (void)window;
    *x = fgDisplay.ScreenWidth;
    *y = fgDisplay.ScreenHeight;
}

/* -- COLORMAP (index mode unsupported on ES2) ------------------------------ */

void fgPlatformSetColor( int idx, float r, float g, float b )
{
    (void)idx; (void)r; (void)g; (void)b;
    fgWarning( "glutSetColor(): color index mode not supported on SDL2/GLES2" );
}

float fgPlatformGetColor( int idx, int comp )
{
    (void)idx; (void)comp;
    return -1.0f;
}

void fgPlatformCopyColormap( int win )
{
    (void)win;
}

/* -- INPUT DEVICES / SPACEBALL (not supported) ------------------------------ */

void fgPlatformRegisterDialDevice( const char *dial_device )
{
    (void)dial_device;
}

void fgPlatformInitializeSpaceball( void ) { }
void fgPlatformSpaceballClose( void ) { }
int  fgPlatformHasSpaceball( void ) { return 0; }
int  fgPlatformSpaceballNumButtons( void ) { return 0; }
void fgPlatformSpaceballSetWindow( SFG_Window *window ) { (void)window; }

/* -- CURSOR POSITION QUERY -------------------------------------------------- */

void fghPlatformGetCursorPos( const SFG_Window *window, GLboolean client,
                              SFG_XYUse *mouse_pos )
{
    int x, y;
    if( client && window )
    {
        mouse_pos->X = window->State.MouseX;
        mouse_pos->Y = window->State.MouseY;
    }
    else
    {
        SDL_GetGlobalMouseState( &x, &y );
        mouse_pos->X = x;
        mouse_pos->Y = y;
    }
    mouse_pos->Use = GL_TRUE;
}

/* -- SERIAL PORT (dial & button box) stubs ---------------------------------- */

typedef struct _serialport SERIALPORT;

SERIALPORT *fg_serial_open( const char *device ) { (void)device; return NULL; }
void fg_serial_close( SERIALPORT *port ) { (void)port; }
int  fg_serial_getchar( SERIALPORT *port ) { (void)port; return EOF; }
int  fg_serial_putchar( SERIALPORT *port, unsigned char ch )
{ (void)port; (void)ch; return 0; }
void fg_serial_flush( SERIALPORT *port ) { (void)port; }
