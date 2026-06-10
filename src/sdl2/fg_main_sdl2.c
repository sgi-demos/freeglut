/*
 * fg_main_sdl2.c
 *
 * SDL2 backend: event loop, event translation, timing.
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

/* -- TIME ----------------------------------------------------------------- */

fg_time_t fgPlatformSystemTime( void )
{
    return (fg_time_t)SDL_GetTicks64();
}

void fgPlatformSleepForEvents( fg_time_t msec )
{
    if( msec <= 0 )
        return;
    if( msec > 1000000 )
        msec = 1000000;
    /* NULL event pointer: wait without removing anything from the queue,
       preserving event order for ProcessSingleEvent */
    SDL_WaitEventTimeout( NULL, (int)msec );
}

void fgPlatformMainLoopPreliminaryWork( void )
{
}

/* -- HELPERS -------------------------------------------------------------- */

static SFG_Window *fghWindowFromID( Uint32 id )
{
    SDL_Window *sw = SDL_GetWindowFromID( id );
    if( !sw )
        return NULL;
    return fgWindowByHandle( sw );
}

static int fghGetModifiers( SDL_Keymod mod )
{
    int ret = 0;
    if( mod & ( KMOD_LSHIFT | KMOD_RSHIFT ) ) ret |= GLUT_ACTIVE_SHIFT;
    if( mod & ( KMOD_LCTRL  | KMOD_RCTRL  ) ) ret |= GLUT_ACTIVE_CTRL;
    if( mod & ( KMOD_LALT   | KMOD_RALT   ) ) ret |= GLUT_ACTIVE_ALT;
    return ret;
}

/* Map an SDL keycode to a GLUT special key, or -1 */
static int fghSpecialKey( SDL_Keycode key )
{
    switch( key )
    {
    case SDLK_F1:        return GLUT_KEY_F1;
    case SDLK_F2:        return GLUT_KEY_F2;
    case SDLK_F3:        return GLUT_KEY_F3;
    case SDLK_F4:        return GLUT_KEY_F4;
    case SDLK_F5:        return GLUT_KEY_F5;
    case SDLK_F6:        return GLUT_KEY_F6;
    case SDLK_F7:        return GLUT_KEY_F7;
    case SDLK_F8:        return GLUT_KEY_F8;
    case SDLK_F9:        return GLUT_KEY_F9;
    case SDLK_F10:       return GLUT_KEY_F10;
    case SDLK_F11:       return GLUT_KEY_F11;
    case SDLK_F12:       return GLUT_KEY_F12;
    case SDLK_LEFT:      return GLUT_KEY_LEFT;
    case SDLK_RIGHT:     return GLUT_KEY_RIGHT;
    case SDLK_UP:        return GLUT_KEY_UP;
    case SDLK_DOWN:      return GLUT_KEY_DOWN;
    case SDLK_PAGEUP:    return GLUT_KEY_PAGE_UP;
    case SDLK_PAGEDOWN:  return GLUT_KEY_PAGE_DOWN;
    case SDLK_HOME:      return GLUT_KEY_HOME;
    case SDLK_END:       return GLUT_KEY_END;
    case SDLK_INSERT:    return GLUT_KEY_INSERT;
    case SDLK_NUMLOCKCLEAR: return GLUT_KEY_NUM_LOCK;
    case SDLK_DELETE:    return GLUT_KEY_DELETE;
    case SDLK_LSHIFT:    return GLUT_KEY_SHIFT_L;
    case SDLK_RSHIFT:    return GLUT_KEY_SHIFT_R;
    case SDLK_LCTRL:     return GLUT_KEY_CTRL_L;
    case SDLK_RCTRL:     return GLUT_KEY_CTRL_R;
    case SDLK_LALT:      return GLUT_KEY_ALT_L;
    case SDLK_RALT:      return GLUT_KEY_ALT_R;
    case SDLK_LGUI:      return GLUT_KEY_SUPER_L;
    case SDLK_RGUI:      return GLUT_KEY_SUPER_R;
    default:             return -1;
    }
}

/* Map an SDL keycode to a GLUT ASCII char delivered via the Keyboard
   callback, or -1.  Printable characters are normally delivered via
   SDL_TEXTINPUT; this handles control characters and provides the value
   used for KeyboardUp (where no text event exists). */
static int fghAsciiKey( SDL_Keycode key, SDL_Keymod mod )
{
    if( key == SDLK_ESCAPE )    return 27;
    if( key == SDLK_BACKSPACE ) return 8;
    if( key == SDLK_TAB )       return 9;
    if( key == SDLK_RETURN || key == SDLK_KP_ENTER ) return 13;

    if( key >= 32 && key < 127 )
    {
        int c = (int)key;
        if( mod & ( KMOD_LCTRL | KMOD_RCTRL ) )
        {
            /* Ctrl+letter produces a control character, like X11/Win32 */
            if( c >= 'a' && c <= 'z' )
                return c - 'a' + 1;
        }
        if( mod & ( KMOD_LSHIFT | KMOD_RSHIFT ) )
        {
            if( c >= 'a' && c <= 'z' )
                c = c - 'a' + 'A';
        }
        return c;
    }
    return -1;
}

static void fghUpdateMenuMousePos( SFG_Window *window )
{
    /* When a menu is active, fgUpdateMenuHighlight needs the mouse position
       expressed in the menu window's coordinates. */
    SFG_Menu *menu = window->ActiveMenu;
    if( !menu )
        return;
    if( fgState.MenuInWindow )
    {
        /* overlay mode: menu->X/Y are parent-window coordinates */
        menu->Window->State.MouseX = window->State.MouseX - menu->X;
        menu->Window->State.MouseY = window->State.MouseY - menu->Y;
    }
    else
    {
        int gx, gy;
        SDL_GetGlobalMouseState( &gx, &gy );
        menu->Window->State.MouseX = gx - menu->X;
        menu->Window->State.MouseY = gy - menu->Y;
    }
}

/* -- EVENT PROCESSING ------------------------------------------------------ */

static void fghProcessEvent( SDL_Event *ev )
{
    SFG_Window *window;

    switch( ev->type )
    {
    case SDL_QUIT:
        if( fgState.ActionOnWindowClose != GLUT_ACTION_CONTINUE_EXECUTION )
            fgState.ExecState = GLUT_EXEC_STATE_STOP;
        break;

    case SDL_WINDOWEVENT:
        window = fghWindowFromID( ev->window.windowID );
        if( !window )
            break;
        switch( ev->window.event )
        {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        {
            int w, h;
            SDL_GL_GetDrawableSize( window->Window.Handle, &w, &h );
            fghOnReshapeNotify( window, w, h, GL_FALSE );
            break;
        }
        case SDL_WINDOWEVENT_MOVED:
            fghOnPositionNotify( window, ev->window.data1, ev->window.data2,
                                 GL_FALSE );
            break;
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
            window->State.Visible = GL_TRUE;
            window->State.WorkMask |= GLUT_DISPLAY_WORK;
            INVOKE_WCB( *window, WindowStatus, ( GLUT_FULLY_RETAINED ) );
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
        case SDL_WINDOWEVENT_HIDDEN:
            window->State.Visible = GL_FALSE;
            INVOKE_WCB( *window, WindowStatus, ( GLUT_HIDDEN ) );
            break;
        case SDL_WINDOWEVENT_ENTER:
            INVOKE_WCB( *window, Entry, ( GLUT_ENTERED ) );
            break;
        case SDL_WINDOWEVENT_LEAVE:
            if( window->IsMenu &&
                window->ActiveMenu && window->ActiveMenu->IsActive )
            {
                fghUpdateMenuMousePos( window );
                fgUpdateMenuHighlight( window->ActiveMenu );
            }
            INVOKE_WCB( *window, Entry, ( GLUT_LEFT ) );
            break;
        case SDL_WINDOWEVENT_CLOSE:
            fgDestroyWindow( window );
            if( fgState.ActionOnWindowClose != GLUT_ACTION_CONTINUE_EXECUTION )
                fgState.ExecState = GLUT_EXEC_STATE_STOP;
            break;
        }
        break;

    case SDL_MOUSEMOTION:
        window = fghWindowFromID( ev->motion.windowID );
        if( !window )
            break;

        window->State.MouseX = ev->motion.x;
        window->State.MouseY = ev->motion.y;

        if( window->ActiveMenu )
        {
            fghUpdateMenuMousePos( window );
            fgUpdateMenuHighlight( window->ActiveMenu );
            break;
        }

        fgState.Modifiers = fghGetModifiers( SDL_GetModState() );
        if( ev->motion.state &
            ( SDL_BUTTON_LMASK | SDL_BUTTON_MMASK | SDL_BUTTON_RMASK ) )
            INVOKE_WCB( *window, Motion, ( ev->motion.x, ev->motion.y ) );
        else
            INVOKE_WCB( *window, Passive, ( ev->motion.x, ev->motion.y ) );
        fgState.Modifiers = INVALID_MODIFIERS;
        break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    {
        GLboolean pressed = ( ev->type == SDL_MOUSEBUTTONDOWN );
        int button, x = ev->button.x, y = ev->button.y;

        window = fghWindowFromID( ev->button.windowID );
        if( !window )
            break;

        switch( ev->button.button )
        {
        case SDL_BUTTON_LEFT:   button = GLUT_LEFT_BUTTON;   break;
        case SDL_BUTTON_MIDDLE: button = GLUT_MIDDLE_BUTTON; break;
        case SDL_BUTTON_RIGHT:  button = GLUT_RIGHT_BUTTON;  break;
        default:                button = ev->button.button - 1; break;
        }

        window->State.MouseX = x;
        window->State.MouseY = y;

        /* Let the menu system consume the click if appropriate */
        if( fgCheckActiveMenu( window, button, pressed, x, y ) )
            break;

        if( !FETCH_WCB( *window, Mouse ) )
            break;

        fgState.Modifiers = fghGetModifiers( SDL_GetModState() );
        INVOKE_WCB( *window, Mouse,
                    ( button, pressed ? GLUT_DOWN : GLUT_UP, x, y ) );
        fgState.Modifiers = INVALID_MODIFIERS;
        break;
    }

    case SDL_MOUSEWHEEL:
    {
        int dir;
        window = fghWindowFromID( ev->wheel.windowID );
        if( !window || ev->wheel.y == 0 )
            break;
        dir = ev->wheel.y > 0 ? 1 : -1;
#if SDL_VERSION_ATLEAST(2,0,4)
        if( ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
            dir = -dir;
#endif
        if( FETCH_WCB( *window, MouseWheel ) )
        {
            fgState.Modifiers = fghGetModifiers( SDL_GetModState() );
            INVOKE_WCB( *window, MouseWheel,
                        ( 0, dir, window->State.MouseX,
                          window->State.MouseY ) );
            fgState.Modifiers = INVALID_MODIFIERS;
        }
        else if( FETCH_WCB( *window, Mouse ) )
        {
            /* classic GLUT compatibility: wheel as buttons 3/4 */
            int btn = dir > 0 ? 3 : 4;
            fgState.Modifiers = fghGetModifiers( SDL_GetModState() );
            INVOKE_WCB( *window, Mouse,
                        ( btn, GLUT_DOWN, window->State.MouseX,
                          window->State.MouseY ) );
            INVOKE_WCB( *window, Mouse,
                        ( btn, GLUT_UP, window->State.MouseX,
                          window->State.MouseY ) );
            fgState.Modifiers = INVALID_MODIFIERS;
        }
        break;
    }

    case SDL_TEXTINPUT:
    {
        const char *p;
        window = fghWindowFromID( ev->text.windowID );
        if( !window )
            break;
        for( p = ev->text.text; *p; p++ )
        {
            /* only ASCII makes sense for the GLUT keyboard callback */
            if( (unsigned char)*p < 128 )
            {
                fgState.Modifiers = fghGetModifiers( SDL_GetModState() );
                INVOKE_WCB( *window, Keyboard,
                            ( (unsigned char)*p, window->State.MouseX,
                              window->State.MouseY ) );
                fgState.Modifiers = INVALID_MODIFIERS;
            }
        }
        break;
    }

    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
        GLboolean pressed = ( ev->type == SDL_KEYDOWN );
        int special, ascii;

        window = fghWindowFromID( ev->key.windowID );
        if( !window )
            break;

        if( ev->key.repeat &&
            ( fgState.KeyRepeat == GLUT_KEY_REPEAT_OFF ||
              window->State.IgnoreKeyRepeat ) )
            break;

        special = fghSpecialKey( ev->key.keysym.sym );
        ascii   = fghAsciiKey( ev->key.keysym.sym,
                               (SDL_Keymod)ev->key.keysym.mod );

        fgState.Modifiers = fghGetModifiers(
                                (SDL_Keymod)ev->key.keysym.mod );
        if( special != -1 )
        {
            if( pressed )
                INVOKE_WCB( *window, Special,
                            ( special, window->State.MouseX,
                              window->State.MouseY ) );
            else
                INVOKE_WCB( *window, SpecialUp,
                            ( special, window->State.MouseX,
                              window->State.MouseY ) );
        }
        else if( ascii != -1 )
        {
            /* Printable keydowns are delivered via SDL_TEXTINPUT (which
               handles layout and shift properly); here we deliver control
               characters on keydown, and everything on keyup. */
            if( pressed )
            {
                if( ascii < 32 || ascii == 127 )
                    INVOKE_WCB( *window, Keyboard,
                                ( (unsigned char)ascii, window->State.MouseX,
                                  window->State.MouseY ) );
            }
            else
                INVOKE_WCB( *window, KeyboardUp,
                            ( (unsigned char)ascii, window->State.MouseX,
                              window->State.MouseY ) );
        }
        fgState.Modifiers = INVALID_MODIFIERS;
        break;
    }

    default:
        break;
    }
}

void fgPlatformProcessSingleEvent( void )
{
    SDL_Event ev;
    while( SDL_PollEvent( &ev ) )
        fghProcessEvent( &ev );
}
