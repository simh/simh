/*
 * Panel of BESM-6, displayed as a graphics window.
 * Using libSDL for graphics and libSDL_ttf for fonts.
 *
 * Copyright (c) 2009, Serge Vakulenko
 * Copyright (c) 2014, Leonid Broukhis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.

 * Except as contained in this notice, the name of Leonid Broukhis or
 * Serge Vakulenko shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from Leonid Broukhis and Serge Vakulenko.
 */

#include "besm6_defs.h"
#if defined (HAVE_LIBSDL)
#if !defined (FONTFILE)
#include "besm6_panel_font.h"
#endif /* !defined (FONTFILE) */
#if !defined (FONTFILE)
#undef HAVE_LIBSDL
#endif /* !defined (FONTFILE) */
#endif /* defined (HAVE_LIBSDL) */

#ifdef HAVE_LIBSDL
#include <stdlib.h>

/*
 * Use a 640x480 window with 32 bit pixels.
 */
#define WIDTH   800
#define HEIGHT  450
#define DEPTH   32

#define STEPX   14
#define STEPY   16
#define TEXTW   76
#define HEADER  28

#include <SDL.h>
#include <SDL_ttf.h>

#define _QUOTE(x) #x
#define QUOTE(x) _QUOTE(x)

/* Data and functions that don't depend on SDL version */
static TTF_Font *font_big;
static TTF_Font *font_small;
static SDL_Color foreground;
static SDL_Color background;
static const SDL_Color white = { 255, 255, 255 };
static const SDL_Color black = { 0,   0,   0   };
static const SDL_Color cyan  = { 0,   128, 128 };
static const SDL_Color grey  = { 64,  64,  64  };
static t_value old_BRZ [8], old_GRP [2];
static uint32 old_M [NREGS], old_PRP [2], old_PC;
static char M_lamps[NREGS][15], BRZ_lamps[8][48],
    GRP_lamps[2][48], PRP_lamps[2][24], PC_lamps[16];

static const int regnum[] = {
    013, 012, 011, 010, 7, 6, 5, 4,
    027, 016, 015, 014, 3, 2, 1, 020,
};

/* The lights can be of 3 brightness levels, averaging
 * 2 samples. Drawing can be done in decay mode
 * (a one-sample glitch == a half-bright light for 2 frames)
 * or in PWM mode (a one-sample glitch = a half-bright light
 * for 1 frame).
 * PWM mode is used, 'act' toggles between 0 and 1.
 */
static int act;

static SDL_Surface *screen;

/*
 * Drawing antialiased text in UTF-8 encoding.
 * The halign parameter controls horizontal alignment.
 * Foreground/background colors are taken from global variables.
 */
static void render_utf8 (TTF_Font *font, int x, int y, int halign, char *message)
{
    SDL_Surface *text;
    SDL_Rect area;

    /* Build image from text */
    text = TTF_RenderUTF8_Shaded (font, message, foreground, background);

    area.x = x;
    if (halign < 0)
        area.x -= text->w;              /* align left */
    else if (halign == 0)
        area.x -= text->w / 2;          /* center */
    area.y = y;
    area.w = text->w;
    area.h = text->h;

    /* Put text image to screen */
    SDL_BlitSurface (text, 0, screen, &area);
    SDL_FreeSurface (text);
}

static SDL_Surface *sprite_from_data (int width, int height,
                                      const unsigned char *data)
{
    SDL_Surface *sprite;
    unsigned *s, r, g, b;
    int y, x;

    sprite = SDL_CreateRGBSurface (SDL_SWSURFACE,
                                   width, height, DEPTH, 0, 0, 0, 0);
    /*
      SDL_Surface *optimized = SDL_DisplayFormat (sprite);
      SDL_FreeSurface (sprite);
      sprite = optimized;
    */
    SDL_LockSurface (sprite);
    for (y=0; y<height; ++y) {
        s = (unsigned*) ((char*)sprite->pixels + y * sprite->pitch);
        for (x=0; x<width; ++x) {
            r = *data++;
            g = *data++;
            b = *data++;
            *s++ = SDL_MapRGB (sprite->format, r, g, b);
        }
    }
    SDL_UnlockSurface (sprite);
    return sprite;
}

/*
 * Drawing a neon light.
 */
static void draw_lamp (int left, int top, int on)
{
    /* Images created by GIMP: save as C file without alpha channel. */
    static const int lamp_width = 12;
    static const int lamp_height = 12;
    static const unsigned char lamp_on [12 * 12 * 3 + 1] =
        "\0\0\0\0\0\0\0\0\0\13\2\2-\14\14e\31\31e\31\31-\14\14\13\2\2\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0D\20\20\313,,\377??\377CC\377CC\377DD\31333D\21\21\0\0"
        "\0\0\0\0\0\0\0D\20\20\357LL\377\243\243\376~~\37699\376@@\376@@\377AA\357"
        "<<D\21\21\0\0\0\13\2\2\313,,\377\243\243\377\373\373\377\356\356\377NN\377"
        ">>\377@@\377@@\377AA\31333\13\2\2-\14\14\377??\376~~\377\356\356\377\321"
        "\321\377<<\377??\377@@\377@@\376@@\377DD-\14\14e\31\31\377CC\37699\377NN"
        "\377<<\377??\377@@\377@@\377@@\376??\377CCe\31\31e\31\31\377CC\376@@\377"
        ">>\377??\377@@\377@@\377@@\377@@\376??\377CCe\31\31-\14\14\377DD\376@@\377"
        "@@\377@@\377@@\377@@\377@@\377@@\376@@\377DD-\14\14\13\2\2\31333\377AA\377"
        "@@\377@@\377@@\377@@\377@@\377@@\377AA\31333\13\2\2\0\0\0D\21\21\357<<\377"
        "AA\376@@\376??\376??\376@@\377AA\357<<D\21\21\0\0\0\0\0\0\0\0\0D\21\21\313"
        "33\377DD\377CC\377CC\377DD\31333D\21\21\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\13"
        "\2\2-\14\14e\31\31e\31\31-\14\14\13\2\2\0\0\0\0\0\0\0\0\0";
    static const unsigned char lamp_off [12 * 12 * 3 + 1] =
        "\0\0\0\0\0\0\0\0\0\0\0\0\14\2\2\14\2\2\14\2\2\14\2\2\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\25\5\5A\21\21h\32\32c\30\30c\30\30h\32\32A\21\21\25\5\5"
        "\0\0\0\0\0\0\0\0\0\25\5\5\\\30\30""8\16\16\0\0\0\0\0\0\0\0\0\0\0\0""8\16"
        "\16\\\30\30\25\5\5\0\0\0\0\0\0A\21\21""8\16\16\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0""8\16\16A\21\21\0\0\0\14\2\2h\32\32\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0h\32\32\14\2\2\14\2\2c\30\30\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0c\30\30\14\2\2\14\2\2c\30\30\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0c\30\30\14\2\2\14\2\2h\32\32\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0h\32\32\14\2\2\0\0\0A\21\21""8\16\16\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0""8\16\16A\21\21\0\0\0\0\0\0\25\5\5\\\30"
        "\30""8\16\16\0\0\0\0\0\0\0\0\0\0\0\0""8\16\16\\\30\30\25\5\5\0\0\0\0\0\0"
        "\0\0\0\25\5\5A\21\21h\32\32c\30\30c\30\30h\32\32A\21\21\25\5\5\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\14\2\2\14\2\2\14\2\2\14\2\2\0\0\0\0\0\0\0\0\0"
        "\0\0\0";

    static unsigned char lamp_mid [sizeof(lamp_on)];
    static SDL_Surface * sprites[3];
    SDL_Rect area;
    int i;

    if (! sprites[0]) {
        sprites[0] = sprite_from_data (lamp_width, lamp_height,
                                       lamp_off);
    }
    if (!sprites[1]) {
        for (i = 0; i < sizeof(lamp_mid); ++i)
            lamp_mid[i] = (lamp_on[i] + lamp_off[i] + 1) / 2;
        sprites[1] = sprite_from_data (lamp_width, lamp_height,
                                       lamp_mid);
    }
    if (! sprites[2]) {
        sprites[2] = sprite_from_data (lamp_width, lamp_height,
                                       lamp_on);
    }

    area.x = left;
    area.y = top;
    area.w = lamp_width;
    area.h = lamp_height;
    SDL_BlitSurface (sprites[on], 0, screen, &area);
}

static void draw_a_modifier (int reg, int hpos, int vpos) {
    int x, val, anded, ored;
    val = M [reg];
    anded = old_M [reg] & val;
    ored = old_M [reg] | val;
    old_M [reg] = val;
    for (x=0; act && x<15; ++x) {
        int new_lamp = anded >> (14-x) & 1 ? 2 : ored >> (14-x) & 1 ? 1 : 0;
        if (new_lamp != M_lamps[reg][x]) {
            draw_lamp (hpos + x*STEPX, vpos, new_lamp);
            M_lamps[reg][x] = new_lamp;
        }
    }
}

/*
 * Drawing index (modifier) registers. They form two groups.
 */
static void draw_modifiers_periodic (int group, int left, int top)
{
    int y, reg;

    for (y=0; y<8; ++y) {
        reg = regnum [y + group*8];
        draw_a_modifier (reg, left + TEXTW, top + HEADER + y*STEPY);
    }
}

static void draw_full_word (t_value val, t_value * old, char * lamps,
                            int hpos, int vpos)
{
    int x;
    t_value anded, ored;
    anded = *old & val;
    ored = *old | val;
    *old = val;
    for (x=0; act && x<48; ++x) {
        int new_lamp = anded >> (47-x) & 1 ? 2 : ored >> (47-x) & 1 ? 1 : 0;
        if (new_lamp != lamps[x]) {
            draw_lamp (hpos + x*STEPX, vpos, new_lamp);
            lamps[x] = new_lamp;
        }
    }
}

/*
 * Drawing the main interrupt register and its mask.
 */
static void draw_grp_periodic (int left, int top)
{
    draw_full_word (GRP, &old_GRP[0], GRP_lamps[0], left + TEXTW, top+HEADER);
    draw_full_word (MGRP, &old_GRP[1], GRP_lamps[1], left + TEXTW, top+HEADER+STEPY);
}

static void draw_partial_word (uint32 val, int bits, uint32 * old,
                               char * lamps, int hpos, int vpos)
{
    int x;
    uint32 anded, ored;
    anded = *old & val;
    ored = *old | val;
    *old = val;
    --bits;
    for (x=0; act && x<=bits; ++x) {
        int new_lamp = anded >> (bits-x) & 1 ? 2 :
            ored >> (bits-x) & 1 ? 1 : 0;
        if (new_lamp != lamps[x]) {
            draw_lamp (hpos + x*STEPX, vpos, new_lamp);
            lamps[x] = new_lamp;
        }
    }
}

static void draw_counters_periodic (int left, int top)
{
    draw_a_modifier(017, left+TEXTW+STEPX, top+HEADER);
    /* The MSB of the displayed PC is the supervisor mode tag */
    draw_partial_word(IS_SUPERVISOR(RUU) ? PC | BBIT(16) : PC, 16,
                      &old_PC, PC_lamps, left+TEXTW, top+HEADER+STEPY);
}

/*
 * Drawing the peripheral interrupt register and its mask.
 */
static void draw_prp_periodic (int left, int top)
{
    draw_partial_word(PRP, 24, &old_PRP[0], PRP_lamps[0], left+TEXTW, top+HEADER);
    draw_partial_word(MPRP, 24, &old_PRP[1], PRP_lamps[1], left+TEXTW, top+HEADER+STEPY);
}

/*
 * Drawing the data cache registers.
 */
static void draw_brz_periodic (int left, int top)
{
    int y;

    for (y=0; y<8; ++y) {
        draw_full_word (BRZ[7-y], &old_BRZ[7-y], BRZ_lamps[y],
                        left+TEXTW, top+HEADER+y*STEPY);
    }
}

/*
 * Visually separating groups of bits.
 */
static void draw_separators (int left, int top, int startbit, int step, int totbits, int rows)
{
    int x, color;
    SDL_Rect area;

    color = grey.r << 16 | grey.g << 8 | grey.b;
    for (x=startbit; x<totbits; x+=step) {
        area.x = left + TEXTW-2 + x*STEPX;
        area.y = top + HEADER-2;
        area.w = 2;
        area.h = rows*STEPY + 2;
        SDL_FillRect (screen, &area, color);
    }
}

/*
 * Drawing weaving bit numbers.
 */
static void draw_bit_numbers (int left, int top, int totbits)
{
    char message [16];
    int x;
    for (x=0; x<totbits; ++x) {
        sprintf (message, "%d", totbits-x);
        render_utf8 (font_small, left+TEXTW+(STEPX/2-1) + x*STEPX,
                     (x & 1) ? top+4 : top+10, 0, message);
    }
}

/*
 * Drawing the static part of the modifier register area.
 */
static void draw_modifiers_static (int group, int left, int top)
{
    int y, reg;
    char message [16];

    draw_separators (left, top, 3, 3, 15, 8);

    /* Register names */
    for (y=0; y<8; ++y) {
        reg = regnum [y + group*8];
        sprintf (message, "М%2o", reg);
        render_utf8 (font_big, left, top + HEADER-4 + y*STEPY, 1, message);
    }

    draw_bit_numbers (left, top, 15);
}

/*
 * Drawing the static part of the interrupt register area.
 */
static void draw_grp_static (int left, int top)
{
    draw_separators (left, top, 3, 3, 48, 2);

    /* Register names */
    render_utf8 (font_big, left, top + HEADER-4, 1, "ГРП");
    render_utf8 (font_big, left, top + HEADER-4 + STEPY, 1, "МГРП");

    draw_bit_numbers (left, top, 48);
}

/*
 * Drawing the static part of the interrupt register area.
 */
static void draw_prp_static (int left, int top)
{
    draw_separators (left, top, 3, 3, 24, 2);

    /* Register names */
    render_utf8 (font_big, left, top + HEADER-4, 1, "ПРП");
    render_utf8 (font_big, left, top + HEADER-4 + STEPY, 1, "МПРП");

    draw_bit_numbers (left, top, 24);
}

/*
 * Drawing the static part of PC and SP (M17) area.
 */
static void draw_counters_static (int left, int top)
{
    draw_separators (left, top, 1, 3, 16, 2);

    /* Register names */
    render_utf8 (font_big, left, top + HEADER-4, 1, "СчМ");
    render_utf8 (font_big, left, top + HEADER-4 + STEPY, 1, "СчАС");

    draw_bit_numbers (left, top, 16);
}

/*
 * Drawing the static part of the cache register area
 */
static void draw_brz_static (int left, int top)
{
    int y;
    char message [40];

    draw_separators (left, top, 3, 3, 48, 8);

    /* Register names */
    for (y=7; y>=0; --y) {
        sprintf (message, "БРЗ %d", 7-y);
        render_utf8 (font_big, left, top + HEADER-4 + y*STEPY, 1, message);
    }

    /* Using bit numbers above GRP */
}

/*
 * Closing the graphical window.
 */
t_stat besm6_close_panel (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    if (! screen)
        return SCPE_UNATT;
    if (font_big) TTF_CloseFont(font_big);
    if (font_small) TTF_CloseFont(font_small);
    TTF_Quit();
    SDL_Quit();
    screen = 0;
    return SCPE_OK;
}

t_stat besm6_show_panel (FILE *st, UNIT *up, int32 v, CONST void *dp)
{
    if (screen)
        fprintf(st, "Panel displayed");
    else
        fprintf(st, "Panel closed");
    return SCPE_OK;
}

#if SDL_MAJOR_VERSION == 2

static SDL_Window *sdlWindow;
static SDL_Renderer *sdlRenderer;
static SDL_Texture *sdlTexture;

/*
 * Initializing of the graphical window and the fonts.
 */
t_stat besm6_init_panel (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    if (screen)
        return SCPE_ALATT;
    /* Initialize SDL subsystems - in this case, only video. */
    if (SDL_Init (SDL_INIT_VIDEO) < 0) {
        return sim_messagef (SCPE_OPENERR, "SDL: unable to init: %s\n",
                             SDL_GetError ());
    }
    sdlWindow = SDL_CreateWindow ("BESM-6 panel",
                                  SDL_WINDOWPOS_UNDEFINED,
                                  SDL_WINDOWPOS_UNDEFINED,
                                  WIDTH, HEIGHT, 0 /* regular window */);
    if (! sdlWindow) {
        return sim_messagef (SCPE_OPENERR, "SDL: unable to set %dx%dx%d mode: %s\n",
                             WIDTH, HEIGHT, DEPTH, SDL_GetError ());
    }

    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, 0);
    /* Make black background */
    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);

    /* Initialize the TTF library */
    if (TTF_Init() < 0) {
        t_stat ret = sim_messagef(SCPE_OPENERR, "SDL: couldn't initialize TTF: %s\n",
                                  SDL_GetError());
        SDL_Quit();
        return ret;
    }

    /* Font colors */
    background = black;
    foreground = cyan;

    /* Open the font file with the requested point size */
    font_big = TTF_OpenFont (QUOTE(FONTFILE), 16);
    font_small = TTF_OpenFont (QUOTE(FONTFILE), 9);
    if (! font_big || ! font_small) {
        t_stat ret = sim_messagef(SCPE_OPENERR, "SDL: couldn't load font %s: %s\n",
                                  QUOTE(FONTFILE), SDL_GetError());
        besm6_close_panel(u, val, cptr, desc);
        return ret;
    }

    screen = SDL_CreateRGBSurface(0, WIDTH, HEIGHT, 32,
                                  0x00FF0000,
                                  0x0000FF00,
                                  0x000000FF,
                                  0xFF000000);

    sdlTexture = SDL_CreateTexture(sdlRenderer,
                                   SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STATIC,
                                   WIDTH, HEIGHT);

    /* Drawing the static part of the BESM-6 panel */
    draw_modifiers_static (0, 24, 10);
    draw_modifiers_static (1, 400, 10);
    draw_prp_static (24, 170);
    draw_counters_static (24+32*STEPX, 170);
    draw_grp_static (24, 230);
    draw_brz_static (24, 280);

    /* Make sure all lights are updated */
    memset(M_lamps, ~0, sizeof(M_lamps));
    memset(BRZ_lamps, ~0, sizeof(BRZ_lamps));
    memset(GRP_lamps, ~0, sizeof(GRP_lamps));
    memset(PRP_lamps, ~0, sizeof(PRP_lamps));
    memset(PC_lamps, ~0, sizeof(PC_lamps));
    besm6_draw_panel(1);

    /* Tell SDL to update the whole screen */
    SDL_UpdateTexture(sdlTexture, NULL, screen->pixels, screen->pitch);
    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent (sdlRenderer);
    return SCPE_OK;
}

/*
 * Refreshing the window.
 */
void besm6_draw_panel (int force)
{
    SDL_Event event;

    if (! screen)
        return;

    if (force) {
        /* When the CPU is stopped */
        act = 1;
        besm6_draw_panel(0);
        act = 1;
        besm6_draw_panel(0);
        return;
    }

    /* Do the blinkenlights */
    draw_modifiers_periodic (0, 24, 10);
    draw_modifiers_periodic (1, 400, 10);
    draw_counters_periodic (24+32*STEPX, 170);
    draw_prp_periodic (24, 170);
    draw_grp_periodic (24, 230);
    draw_brz_periodic (24, 280);

    act = !act;

    /* Tell SDL to update the whole screen */
    SDL_UpdateTexture(sdlTexture, NULL, screen->pixels, screen->pitch);
    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent (sdlRenderer);

    /* Close the panel window */
    if (SDL_PollEvent (&event) && event.type == SDL_QUIT)
        besm6_close_panel(&cpu_unit, 0, NULL, NULL);
}

#else

/*
 * Initializing of the graphical window and the fonts.
 */
t_stat besm6_init_panel (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    if (screen)
        return SCPE_ALATT;
    /* Initialize SDL subsystems - in this case, only video. */
    if (SDL_Init (SDL_INIT_VIDEO) < 0) {
        return sim_messagef (SCPE_OPENERR, "SDL: unable to init: %s\n",
                             SDL_GetError ());
    }
    screen = SDL_SetVideoMode (WIDTH, HEIGHT, DEPTH, SDL_SWSURFACE);
    if (! screen) {
        return sim_messagef (SCPE_OPENERR, "SDL: unable to set %dx%dx%d mode: %s\n",
                             WIDTH, HEIGHT, DEPTH, SDL_GetError ());
    }

    /* Initialize the TTF library */
    if (TTF_Init() < 0) {
        t_stat ret = sim_messagef(SCPE_OPENERR, "SDL: couldn't initialize TTF: %s\n",
                                  SDL_GetError());
        SDL_Quit();
        return ret;
    }

    /* Open the font file with the requested point size */
    font_big = TTF_OpenFont (QUOTE(FONTFILE), 16);
    font_small = TTF_OpenFont (QUOTE(FONTFILE), 9);
    if (! font_big || ! font_small) {
        t_stat ret = sim_messagef(SCPE_OPENERR, "SDL: couldn't load font %s: %s\n",
                                  QUOTE(FONTFILE), SDL_GetError());
        besm6_close_panel(u, val, cptr, desc);
        return ret;
    }

    /* Font colors */
    background = black;
    foreground = cyan;

    /* Drawing the static part of the BESM-6 panel */
    draw_modifiers_static (0, 24, 10);
    draw_modifiers_static (1, 400, 10);
    draw_prp_static (24, 170);
    draw_counters_static (472, 170);
    draw_grp_static (24, 230);
    draw_brz_static (24, 280);

    /* Make sure all lights are updated */
    memset(M_lamps, ~0, sizeof(M_lamps));
    memset(BRZ_lamps, ~0, sizeof(BRZ_lamps));
    memset(GRP_lamps, ~0, sizeof(GRP_lamps));
    memset(PRP_lamps, ~0, sizeof(PRP_lamps));
    besm6_draw_panel(1);

    /* Tell SDL to update the whole screen */
    SDL_UpdateRect (screen, 0, 0, WIDTH, HEIGHT);
    return SCPE_OK;
}

/*
 * Refreshing the window
 */
void besm6_draw_panel (int force)
{
    SDL_Event event;
    if (! screen)
        return;

    if (force) {
        /* When the CPU is stopped */
        act = 1;
        besm6_draw_panel(0);
        act = 1;
        besm6_draw_panel(0);
        return;
    }

    /* Do the blinkenlights */
    draw_modifiers_periodic (0, 24, 10);
    draw_modifiers_periodic (1, 400, 10);
    draw_counters_periodic (472, 170);
    draw_prp_periodic (24, 170);
    draw_grp_periodic (24, 230);
    draw_brz_periodic (24, 280);

    /* Tell SDL to update the whole screen */
    SDL_UpdateRect (screen, 0, 0, WIDTH, HEIGHT);

    /* Close the panel window */
    if (SDL_PollEvent (&event) && event.type == SDL_QUIT)
        besm6_close_panel(&cpu_unit, 0, NULL, NULL);
}

#endif /* SDL_MAJOR_VERSION */

#else /* HAVE_LIBSDL */
t_stat besm6_init_panel (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    return sim_messagef(SCPE_OPENERR, "Need SDL and SDLttf libraries");
}

t_stat besm6_close_panel (UNIT *u, int32 val, CONST char *cptr, void *desc)
{
    return SCPE_UNATT;
}

t_stat besm6_show_panel (FILE *st, UNIT *up, int32 v, CONST void *dp)
{
    return SCPE_UNATT;
}

void besm6_draw_panel (int force)
{
}
#endif /* HAVE_LIBSDL */
