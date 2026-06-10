/*
 * fg_window_sdl2.c
 *
 * SDL2 backend: window creation and management.
 *
 * License: MIT (same as freeglut, see fg_internal_sdl2.h)
 */

#define FREEGLUT_BUILDING_LIB
#include <GL/freeglut.h>
#include "../fg_internal.h"
#include <SDL.h>

extern void fghOnReshapeNotify( SFG_Window *window, int width, int height,
                                GLboolean forceNotify );
extern void fghOnPositionNotify( SFG_Window *window, int x, int y,
                                 GLboolean forceNotify );

static void fghApplyContextAttributes( void )
{
    SDL_GL_ResetAttributes();

    /* OpenGL ES 2.0 context */
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK,
                         SDL_GL_CONTEXT_PROFILE_ES );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );

    SDL_GL_SetAttribute( SDL_GL_RED_SIZE,   8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE,  8 );

    SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE,
        ( fgState.DisplayMode & GLUT_ALPHA ) ? 8 : 0 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE,
        ( fgState.DisplayMode & GLUT_DEPTH ) ? 24 : 0 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE,
        ( fgState.DisplayMode & GLUT_STENCIL ) ? 8 : 0 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER,
        ( fgState.DisplayMode & GLUT_DOUBLE ) ? 1 : 0 );

    if( fgState.DisplayMode & GLUT_MULTISAMPLE )
    {
        SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 1 );
        SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES,
                             fgState.SampleNumber );
    }

    /* Share contexts so menu windows can reuse the GUI shader/state and
       resources can be shared between glut windows, matching X11 behavior */
    SDL_GL_SetAttribute( SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1 );
}

void fgPlatformOpenWindow( SFG_Window *window, const char *title,
                           GLboolean positionUse, int x, int y,
                           GLboolean sizeUse, int w, int h,
                           GLboolean gameMode, GLboolean isSubWindow )
{
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

    if( window->IsMenu && fgState.MenuInWindow )
    {
        /* GLUT_MENU_IN_WINDOW: menus are drawn as an overlay inside their
           parent window; no platform window or GL context is needed.
           All fgPlatform* entry points are guarded against NULL handles. */
        window->Window.Handle = NULL;
        window->Window.Context = NULL;
        window->State.Visible = GL_FALSE;
        return;
    }

    if( isSubWindow )
    {
        fgWarning( "glutCreateSubWindow is not supported by the SDL2 "
                   "backend; creating a top-level window instead" );
    }

    if( !sizeUse )
    {
        w = 300;
        h = 300;
    }

    if( window->IsMenu )
        flags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP
               | SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_POPUP_MENU
               | SDL_WINDOW_HIDDEN;
    else if( gameMode )
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    else
        flags |= SDL_WINDOW_RESIZABLE;

    fghApplyContextAttributes();

    window->Window.Handle = SDL_CreateWindow(
        title ? title : "freeglut",
        positionUse ? x : (int)SDL_WINDOWPOS_UNDEFINED,
        positionUse ? y : (int)SDL_WINDOWPOS_UNDEFINED,
        w, h, flags );

    if( !window->Window.Handle )
        fgError( "SDL_CreateWindow failed: %s", SDL_GetError() );

    /* Menu windows share the GL context of an existing window when possible;
       each regular window gets its own context (classic GLUT semantics). */
    window->Window.Context = SDL_GL_CreateContext( window->Window.Handle );
    if( !window->Window.Context )
        fgError( "SDL_GL_CreateContext (ES 2.0) failed: %s", SDL_GetError() );

    SDL_GL_MakeCurrent( window->Window.Handle, window->Window.Context );
    SDL_GL_SetSwapInterval( 1 );

    /* Stash actual drawable size */
    SDL_GL_GetDrawableSize( window->Window.Handle,
                            &window->State.Width, &window->State.Height );
    SDL_GetWindowPosition( window->Window.Handle,
                           &window->State.Xpos, &window->State.Ypos );

    window->State.Visible = window->IsMenu ? GL_FALSE : GL_TRUE;
    window->State.WorkMask |= GLUT_INIT_WORK;
}

void fgPlatformCloseWindow( SFG_Window *window )
{
    if( window->Window.Context )
    {
        SDL_GL_DeleteContext( window->Window.Context );
        window->Window.Context = NULL;
    }
    if( window->Window.Handle )
    {
        SDL_DestroyWindow( window->Window.Handle );
        window->Window.Handle = NULL;
    }
}

void fgPlatformSetWindow( SFG_Window *window )
{
    if( window && window->Window.Handle )
        SDL_GL_MakeCurrent( window->Window.Handle, window->Window.Context );
}

void fgPlatformGlutSetWindowTitle( const char *title )
{
    if( fgStructure.CurrentWindow && fgStructure.CurrentWindow->Window.Handle )
        SDL_SetWindowTitle( fgStructure.CurrentWindow->Window.Handle, title );
}

void fgPlatformGlutSetIconTitle( const char *title )
{
    (void)title;    /* SDL2 has no separate icon title */
}

void fgPlatformHideWindow( SFG_Window *window )
{
    if( window && window->Window.Handle )
        SDL_HideWindow( window->Window.Handle );
}

/* Deferred position/size/z-order/fullscreen work */
void fgPlatformPosResZordWork( SFG_Window *window, unsigned int workMask )
{
    SDL_Window *sw;
    if( !window || !window->Window.Handle )
        return;
    sw = window->Window.Handle;

    if( workMask & GLUT_FULL_SCREEN_WORK )
    {
        Uint32 fs = window->State.IsFullscreen ? 0
                                               : SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetWindowFullscreen( sw, fs );
        window->State.IsFullscreen = !window->State.IsFullscreen;
    }
    if( workMask & GLUT_POSITION_WORK )
        SDL_SetWindowPosition( sw, window->State.DesiredXpos,
                                   window->State.DesiredYpos );
    if( workMask & GLUT_SIZE_WORK )
        SDL_SetWindowSize( sw, window->State.DesiredWidth,
                               window->State.DesiredHeight );
    if( workMask & GLUT_ZORDER_WORK )
    {
        if( window->State.DesiredZOrder > 0 )
            SDL_RaiseWindow( sw );
        /* lowering a window is not supported by SDL2 */
    }
}

/* Deferred visibility work */
void fgPlatformVisibilityWork( SFG_Window *window )
{
    SFG_Window *win = window;
    if( !window || !window->Window.Handle )
        return;

    switch( window->State.DesiredVisibility )
    {
    case DesireHiddenState:
        SDL_HideWindow( window->Window.Handle );
        break;
    case DesireIconicState:
        /* iconify the top-level window freeglut-style */
        while( win->Parent )
            win = win->Parent;
        SDL_MinimizeWindow( win->Window.Handle );
        break;
    case DesireNormalState:
        SDL_ShowWindow( window->Window.Handle );
        if( window->IsMenu )
            SDL_RaiseWindow( window->Window.Handle );
        break;
    }
}

/* First-iteration work after window creation */
void fgPlatformInitWork( SFG_Window *window )
{
    int w, h;
    if( !window || !window->Window.Handle )
        return;

    SDL_GL_GetDrawableSize( window->Window.Handle, &w, &h );
    fghOnReshapeNotify( window, w, h, GL_TRUE );

    if( !window->IsMenu )
    {
        INVOKE_WCB( *window, WindowStatus, ( GLUT_FULLY_RETAINED ) );
        window->State.Visible = GL_TRUE;
    }
}
