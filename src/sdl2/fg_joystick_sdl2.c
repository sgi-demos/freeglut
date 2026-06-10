/*
 * fg_joystick_sdl2.c
 *
 * SDL2 backend: joystick support via SDL_Joystick.
 *
 * License: MIT (same as freeglut, see fg_internal_sdl2.h)
 */

#define FREEGLUT_BUILDING_LIB
#include <GL/freeglut.h>
#include "../fg_internal.h"
#include <SDL.h>

void fgPlatformJoystickRawRead( SFG_Joystick *joy, int *buttons, float *axes )
{
    int i;
    SDL_Joystick *h = joy->pJoystick.handle;

    if( joy->error || !h )
        return;

    SDL_JoystickUpdate();

    if( buttons )
    {
        int nbtn = SDL_JoystickNumButtons( h );
        *buttons = 0;
        for( i = 0; i < nbtn && i < 32; i++ )
            if( SDL_JoystickGetButton( h, i ) )
                *buttons |= ( 1 << i );
    }
    if( axes )
        for( i = 0; i < joy->num_axes; i++ )
            axes[ i ] = (float)SDL_JoystickGetAxis( h, i );
}

void fgPlatformJoystickOpen( SFG_Joystick *joy )
{
    int i;

    joy->pJoystick.handle = SDL_JoystickOpen( joy->id );
    if( !joy->pJoystick.handle )
    {
        joy->error = GL_TRUE;
        return;
    }

    joy->num_axes = SDL_JoystickNumAxes( joy->pJoystick.handle );
    if( joy->num_axes > _JS_MAX_AXES )
        joy->num_axes = _JS_MAX_AXES;
    joy->num_buttons = SDL_JoystickNumButtons( joy->pJoystick.handle );

    for( i = 0; i < joy->num_axes; i++ )
    {
        joy->min[ i ] = -32768.0f;
        joy->max[ i ] =  32767.0f;
        joy->center[ i ] = 0.0f;
        joy->dead_band[ i ] = 0.0f;
        joy->saturate[ i ] = 1.0f;
    }
    joy->error = GL_FALSE;
}

void fgPlatformJoystickInit( SFG_Joystick *fgJoystick[], int ident )
{
    fgJoystick[ ident ]->id = ident;
    fgJoystick[ ident ]->error = GL_FALSE;
    fgJoystick[ ident ]->pJoystick.handle = NULL;

    if( ident >= SDL_NumJoysticks() )
        fgJoystick[ ident ]->error = GL_TRUE;
}

void fgPlatformJoystickClose( int ident )
{
    extern SFG_Joystick *fgJoystick[ MAX_NUM_JOYSTICKS ];
    if( fgJoystick[ ident ]->pJoystick.handle )
    {
        SDL_JoystickClose( fgJoystick[ ident ]->pJoystick.handle );
        fgJoystick[ ident ]->pJoystick.handle = NULL;
    }
}
