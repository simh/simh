/* ibm1130_plot.c: IBM 1130 1627 plotter emulation

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel
   Revision History

   2004.10.22 - Written.
   2006.1.2 - Rewritten as plotter routine by Carl V Claunch
   2012.11.23 - added -d option in detach, which we'll use in the CGI simulator. BK.

 * (C) Copyright 2004, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 */

#include "ibm1130_defs.h"

#ifndef ENABLE_PLOT_SUPPORT

	DEVICE plot_dev = {
		"PLOT", NULL, NULL, NULL,
		0, 16, 16, 1, 16, 16,
		NULL, NULL, NULL,
		NULL, NULL, NULL};

	void xio_1627_plotter	(int32 addr, int32 func, int32 modify)
	{
		/* silently eat any plotter commands */
	}

#else

#define NONDLL		// I am linking statically to avoid some issues.
#include "gd.h"

/***************************************************************************************
 *  1627 model 1 plotter (based on Calcomp 535 which was sold as IBM 1627)
 *
 *  - 11" wide carriage, addressible in .01" steps
 *  - continous sheet paper up to 120' in length
 *  - sheet moveable in .01" steps, either direction
 *  - switchable pen, in various colors and line widths
 *
 *  Notice that the WIDTH is 11" and the LENGTH can be anything up to 120'. And, the WIDTH
 *  was the plotter's Y direction, and the LENGTH was the plotter's X direction.
 * 
 *  The simulator creates a GIF image corresponding to a landscape mode sheet of paper. That is,
 *  the plotter's Y direction is the image's horizontal dimension, and the plotter's X direction
 *  is the image's vertical dimension. The WIDTH of the image is always 1100 pixels (11 inches at
 *  100 dpi), and the LENGTH (height) of the image can be set. The default is 800 pixels (8 
 *  inches at 100 dpi). A diagram of more than 8" in length (X direction) will span more than 
 *  one printed page in landscape mode. 
 *
 *  When an 'att plot' command is issued a file is created based on the
 *  default or currently set values of paper length, pen position, pen color and pen width.
 *  
 *  When a 'det plot' command is issued, the plotter image will be
 *  written to the GIF that was created during the attach process. The
 *  image is not viewable until this point. (You could implement an EXAMINE PLOT command
 *  of some sort to write out an intermediate version of the image, but this is not currently
 *  implemented).
 *  
 *  The 'set plot' command can set pen width, paper length, pen color,
 *  current carriage X and Y coordinates, as discussed below. Paper length can be set
 *  to alter the default of 800 (8"); changes are ignored until
 *  the next 'attach' command. The current carriage x and y positions
 *  can be set at any time and will go into effect immediately, just
 *  as the pen color and pen width can be altered on the fly.
 *
 * NOTE: requires the libgd library and definition of ENABLE_PLOT_SUPPORT in makefile or Visual C configuration
 * gd source is not included in the main simh and ibm1130.org source distributions at the present time due to
 * licensing issues.
 *
 * NOTE: On Windows, you need to either:
 *	+	compile both LIBGD and SIMH to use the static C runtime libraries, compile
 *		   LIBGD to a static library, and link LIBGD into ibm1130.exe (which is
 *		   what we do at IBM1130.org, so that gd is built into the version of ibm1130.exe
 *		   we distribute), or, 
 *	+	Compile both LIBGD and IBM1130 to use the DLL version of the C runtime, and compile
 *		   GD to either a static library or a DLL, but, static is easier since you don't
 *		   need to copy LIBGD.DLL along with ibm1130.exe
 *
 * SIMH commands:
 *
 * attach [-w] plot filename.gif
 *		Creates file filename.gif and attaches the plotter device to it.
 *      The file is empty at this point. The pen is raised. If the -w option is specified, and the 
 *		simulator does not draw on the plotter between attach and detach, the gif file will be deleted
 *		on detach. (This is useful for the the cgi version of the simulator).
 *
 * detach plot filename.gif
 *		Detach the plot device. The gif data is written at this point, not before.
 *		If the -w flag was used on attach, and there was no plot activity, the gif file will be deleted.
 *
 * set plot black | red | blue | green | yellow | purple | lgrey | grey
 *		Sets the pen to the named color. Default is black.
 *
 * set plot 1.0 | 2.0 | 3.0 | 4.0
 *		Sets the pen thickness to the specified number of hundredths of an inch. Default is 1.0
 *
 * set plot penup | pendown
 *		Moves the pen up or down (onto the paper).
 *
 * set plot length NNN
 *		Sets the plot length (plotter X direction, GIF vertical dimension) to the NNN hundredths of 
 *		an inch. Default is 800. The plot width (plotter Y direction, GIF horizontal dimension) is always 
 *		1100 (11 inches). NOTE: Changing this setting has no affect on the current plot. It takes affect at 
 *		the next "attach plot" command.
 *
 * set plot xpos NNN
 * set plot ypos NNN
 *		Sets the pen x or y position to NNN hundredths of an inch.
 * 
 * (You cannot manually create a plot by issuing set plot pendown, xpos and ypos commands. The xpos and ypos
 *  settings only change the starting point for the simulated program).
 ***************************************************************************************/

#define PLOT1627_DSW_OP_COMPLETE			0x8000
#define PLOT1627_DSW_BUSY					0x0200
#define PLOT1627_DSW_NOT_READY				0x0100

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)
#define IS_DEBUG 	 ((plot_unit->flags & UNIT_DEBUG) == UNIT_DEBUG)
#define IS_PENDOWN 	 ((plot_unit->flags & UNIT_PEN) != 0)

static t_stat plot_svc    (UNIT *uptr);				/* activity routine */
static t_stat plot_reset  (DEVICE *dptr);			/* reset of 1130 */
static t_stat plot_attach (UNIT *uptr, CONST char *cptr);	/* attach, loads plotter */
static t_stat plot_detach (UNIT *uptr);				/* detach and save image */
static t_stat plot_examine (UNIT *uptr);			/* update file with current canvas */
static t_stat plot_set_length (UNIT *uptr, int32 val, char * ptr, void *desc);	/* set paper length */
static t_stat plot_set_pos (UNIT *uptr, int32 val, CONST char * ptr, void *desc);		/* reset current X/Y position */
static t_stat plot_show_vals(FILE *fp, UNIT *uptr, int32 val, CONST void *descrip);	/* print x, y and length */
static t_stat plot_show_nl(FILE *fp, UNIT *uptr, int32 val, CONST void *descrip);  	/* overcome wacky simh behavior */
static void   update_pen(void);        				 /* will ensure pen action is correct when changes made */
static t_stat plot_validate_change (UNIT *uptr, int32 val, CONST char * ptr, void *desc); /* when set command issued */
static void   process_cmd(void);					/* does actual drawing for plotter */

static int16 plot_dsw  = 0;							/* device status word */
static int16 plot_cmd  = 0;							/* the command to process */
static int32 plot_wait = 1000;						/* plotter movement wait */
static int32 plot_xpos = 0;							/* current X position */
static int32 plot_xmax = 799;						/* end of paper */
static int32 plot_ypos = 0;							/* current Y position */
static int32 plot_ymax = 1099;						/* right edge of carriage */

#define PEN_DOWN 0x80000000
#define PEN_UP   0x00000000
static int32 plot_pen = PEN_UP;						/* current pen position. This duplicates the device flag PLOT_PEN. Makes the show dev plot command nicer. */

static int black_pen;								/* holds color black */
static int blue_pen;								/* holds color blue */
static int red_pen;								    /* holds color red */
static int green_pen;								/* holds color green */
static int yellow_pen;                              /* holds yellow color */
static int purple_pen;								/* holds color purple */
static int ltgrey_pen;                              /* holds light grey */
static int grey_pen;                                /* holds grey */
static int white_background;						/* holds white of paper roll */
static int plot_pwidth;							    /* set and display variable */
static int plot_pcolor;							    /* set and display variable */
static int need_update = FALSE;						/* flag to force and update_pen() */
static int plot_used = FALSE;						/* flag set to true if anything was actually plotted between attach and detach */
static int delete_if_unused = FALSE;				/* if TRUE and no plotter activity was seen, delete file on detach. This flag is set by -w option on attach command. */
static gdImagePtr image = NULL;						/* pointer to our canvas */

#define UNIT_V_COLOR    (UNIT_V_UF + 0)				/* color of selected pen - 3 bits */
#define UNIT_V_WIDTH	(UNIT_V_UF + 3)				/* width of pen - two bits */
#define UNIT_V_NOOP     (UNIT_V_UF + 5)             /* dummy for set/show commands */
#define UNIT_V_DEBUG    (UNIT_V_UF + 6)             /* for -d switch on attach command */
#define UNIT_V_PEN      (UNIT_V_UF + 7)             /* track pen state */

#define UNIT_WIDTH		 (3u << UNIT_V_WIDTH)		/* two bits */
#define UNIT_COLOR		 (7u << UNIT_V_COLOR)		/* three bits */
#define UNIT_NOOP        (1u << UNIT_V_NOOP)        /* dummy for set/show */
#define UNIT_DEBUG       (1u << UNIT_V_DEBUG)       /* shows debug mode on */
#define UNIT_PEN         (1u << UNIT_V_PEN)         /* the pen state bit */

#define PEN_BLACK	 	 (0u << UNIT_V_COLOR)
#define PEN_RED	 	     (1u << UNIT_V_COLOR)
#define PEN_BLUE	 	 (2u << UNIT_V_COLOR)
#define PEN_GREEN 	     (3u << UNIT_V_COLOR)
#define PEN_YELLOW	     (4u << UNIT_V_COLOR)
#define PEN_PURPLE	     (5u << UNIT_V_COLOR)
#define PEN_LTGREY	     (6u << UNIT_V_COLOR)
#define PEN_GREY 	     (7u << UNIT_V_COLOR)

#define SET_COLOR(op)   (plot_unit[0].flags = (plot_unit[0].flags & ~UNIT_COLOR) | (op))
#define GET_COLOR       (plot_unit[0].flags & UNIT_COLOR)

#define BLACK           0,0,0
#define BLUE            0,0,255
#define RED             255,0,0
#define GREEN           0,255,0
#define YELLOW          200,200,0
#define PURPLE          150,0,150
#define LTGREY          200,200,200
#define GREY            120,120,120
#define WHITE           255,255,255

#define PEN_SINGLE		(0u << UNIT_V_WIDTH)
#define PEN_DOUBLE 		(1u << UNIT_V_WIDTH)
#define PEN_TRIPLE      (2u << UNIT_V_WIDTH)
#define PEN_QUAD 		(3u << UNIT_V_WIDTH)

#define GET_WIDTH()		(plot_unit[0].flags & UNIT_WIDTH)
#define SET_WIDTH(cd)	(plot_unit[0].flags = (plot_unit[0].flags & ~UNIT_WIDTH) | (cd))

UNIT plot_unit[] = {
	{ UDATA (&plot_svc, UNIT_ATTABLE, 0) },
};

REG plot_reg[] = {
	{ HRDATA (DSW, 	    plot_dsw,  16) },			/* device status word */
	{ DRDATA (WTIME,    plot_wait, 24), PV_LEFT },	/* plotter movement wait */
	{ DRDATA (Xpos, plot_xpos,  32), PV_LEFT },		/* Current X Position*/
	{ DRDATA (Ypos, plot_ypos,  32), PV_LEFT },		/* Current Y Position*/
	{ FLDATA (PenDown, plot_pen, 0)},				/* Current pen position: 1 = down */
    { DRDATA (PaperSize, plot_xmax, 32), PV_LEFT }, /* Length of paper in inches */
	{ NULL }  };

MTAB plot_mod[] = {
	{ UNIT_COLOR,    PEN_BLACK,		"black",	"BLACK",	&plot_validate_change},
	{ UNIT_COLOR,    PEN_RED,		"red",		"RED",		&plot_validate_change},
	{ UNIT_COLOR,    PEN_BLUE,		"blue",		"BLUE",		&plot_validate_change},
	{ UNIT_COLOR,    PEN_GREEN,		"green",	"GREEN",	&plot_validate_change},
	{ UNIT_COLOR,    PEN_YELLOW,	"yellow",	"YELLOW",	&plot_validate_change},
	{ UNIT_COLOR,    PEN_PURPLE,	"purple",	"PURPLE",	&plot_validate_change},
	{ UNIT_COLOR,    PEN_LTGREY,	"ltgrey",	"LTGREY",	&plot_validate_change},
	{ UNIT_COLOR,    PEN_GREY,		"grey",		"GREY",		&plot_validate_change},
	{ UNIT_WIDTH,    PEN_SINGLE,	"1.0",		"1.0",		&plot_validate_change},
	{ UNIT_WIDTH,    PEN_DOUBLE,	"2.0",		"2.0",		&plot_validate_change},
	{ UNIT_WIDTH,    PEN_TRIPLE,	"3.0",		"3.0",		&plot_validate_change},
	{ UNIT_WIDTH,    PEN_QUAD,		"4.0",		"4.0",		&plot_validate_change},
    { UNIT_PEN,      UNIT_PEN,      "pendown",  "PENDOWN",  &plot_validate_change},
    { UNIT_PEN,      0,             "penup",    "PENUP",    &plot_validate_change},
    /* below is dummy entry to trigger the show routine and print extended values */
    { UNIT_NOOP,     0,              "",        NULL,       NULL, &plot_show_vals},
    /* extended entries must allow parm for both unit and dev, but
     * then they will print the value twice for a 'show plot' command
     * therefore they are set to not display unless explicity requested
     * and the special dummy NOOP entry will cause the print of these values */
 	{ MTAB_XTD | MTAB_VAL | MTAB_VUN | MTAB_VDV | MTAB_NMO,    2,
             "length",	"LENGTH", &plot_set_length, &plot_show_nl, &plot_reg[5]},
 	{ MTAB_XTD | MTAB_VAL | MTAB_VDV | MTAB_VUN | MTAB_NMO,     0,
             "Xpos",	"XPOS", &plot_set_pos, &plot_show_nl, &plot_reg[2]},
 	{ MTAB_XTD | MTAB_VAL | MTAB_VDV | MTAB_VUN | MTAB_NMO,     1,
             "Ypos",	"YPOS", &plot_set_pos, &plot_show_nl, &plot_reg[3]},
	{ 0 }  };

DEVICE plot_dev = {
	"PLOT", plot_unit, plot_reg, plot_mod,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, plot_reset,
	NULL, plot_attach, plot_detach};

/* xio_1627_plotter - XIO command interpreter for the 1627 plotter model 1 */

void xio_1627_plotter (int32 iocc_addr, int32 iocc_func, int32 iocc_mod)
{
	char msg[80];
	int16 v;

	if (! IS_ONLINE(plot_unit) ) {
		SETBIT(plot_dsw, PLOT1627_DSW_NOT_READY);					/* set not ready */
        if (IS_DEBUG) printf("Plotter has no paper, ignored\n");
		return;														/* and ignore */
	}

	switch (iocc_func) {
		case XIO_READ:												/* read XIO */
			xio_error("Read XIO not supported by 1627 plotter");
			break;

		case XIO_WRITE:												/* write: do one plotter operation */
			if ((plot_dsw & PLOT1627_DSW_NOT_READY)) {
                 if (IS_DEBUG) printf("Wrote to non-ready Plotter\n");
                 break;
            }
			plot_cmd = (uint16) ( M[iocc_addr & mem_mask] >> 10 );	/* pick up command */
			process_cmd();						       				/* interpret command */
			sim_activate(plot_unit, plot_wait);						/* schedule interrupt */
			SETBIT(plot_dsw, PLOT1627_DSW_BUSY);					/* mark it busy */
			break;

		case XIO_SENSE_DEV:											/* sense device status */
			ACC = plot_dsw;											/* get current status */
			if (iocc_mod & 0x01) {									/* reset interrupts */
				CLRBIT(plot_dsw, PLOT1627_DSW_OP_COMPLETE); 
				CLRBIT(ILSW[3], ILSW_3_1627_PLOTTER);
			}
			break;

		case XIO_CONTROL:											/* control XIO */
			// xio_error("Control XIO not supported by 1627 plotter");
			// Well, not on a real 1130. But on our simulator, let's use XIO_CONTROL to
			// allow programmatic control of the pen. Nifty, eh?
			//
			// Functions: XIO_CONTROL 0 clr		- sets pen color (0=black, 1=red, 2=blue, 3=green, 4=yellow, 5=purple, 6=ltgrey, 7=grey)
			//			  XIO_CONTRLL 1 wid		- sets pen width (1..4)
			//			  XIO_CONTROL 2 xpos	- sets pen xpos
			//			  XIO_CONTROL 3 ypos	- sets pen ypos

			v = (int16) iocc_addr;									/* get signed 16 bit value passed in addr arg */
			switch (iocc_mod) {
				case 0:												/* set pen color */
					if (BETWEEN(v,0,7)) {
						SET_COLOR(v << UNIT_V_COLOR);
						update_pen();
					}
					break;

				case 1:												/* set pen width 1..4*/
					if (BETWEEN(v,1,4)) {
						SET_WIDTH((v-1) << UNIT_V_WIDTH);
						update_pen();
					}
					break;

				case 2:												/* set xpos. (Programmatic xpos and ypos are probably not that valuable) */
					plot_xpos = v;									/* Note that it's possible to move the pen way off the paper */
					break;

				case 3:												/* set ypos. Clip to valid range! */
					if (v <= 0)
						plot_ypos = 0;
					else if (v > plot_ymax)
						plot_ypos = plot_ymax;
					else
						plot_ypos = v;
					break;
			}
			break;

		default:
			sprintf(msg, "Invalid 1627 Plotter XIO function %x", iocc_func);
			xio_error(msg);
	}
    return;
}

// plot_svc - 1627 plotter operation complete

static t_stat plot_svc (UNIT *uptr)
{
	CLRBIT(plot_dsw, PLOT1627_DSW_BUSY);			/* clear reader busy flag */

	SETBIT(plot_dsw, PLOT1627_DSW_OP_COMPLETE);		/* indicate read complete */

	SETBIT(ILSW[3], ILSW_3_1627_PLOTTER);			/* initiate interrupt */
	calc_ints();

	return SCPE_OK;
}

/* plot_reset - reset emulated plotter */

static t_stat plot_reset (DEVICE *dptr)
{
#ifdef NONDLL
	static int show_notice = FALSE;

	if (show_notice && ! cgi) {
		printf("Plotter support included. Please see www.libgd.org for libgd copyright information.\n");
		show_notice = FALSE;
	}
#endif

	sim_cancel(plot_unit);

	CLRBIT(plot_dsw, PLOT1627_DSW_BUSY | PLOT1627_DSW_OP_COMPLETE);

    if (IS_DEBUG) printf("reset routine for Plotter\n");

	CLRBIT(ILSW[3], ILSW_3_1627_PLOTTER);
	calc_ints();

	return SCPE_OK;
}


/* plot_attach - attach file to simulated plotter */

static t_stat plot_attach (UNIT *uptr, CONST char *cptr)
{
	t_stat result;

	SETBIT(plot_dsw, PLOT1627_DSW_NOT_READY);				/* assume failure */

	CLRBIT(uptr->flags, UNIT_DEBUG);
	if (sim_switches & SWMASK('D')) SETBIT(uptr->flags, UNIT_DEBUG);

	if (cptr == NULL || ! *cptr)							/* filename must be passed */
		return SCPE_ARG;

	/* set up our canvas at the desired size */
	image = gdImageCreate(plot_ymax+1,plot_xmax+1);			/* create our canvas */
    if (image == NULL) {
       if (IS_DEBUG) printf("problem creating image canvas\n");
       return SCPE_MEM;
    }

	delete_if_unused = (sim_switches & SWMASK('W')) != 0;

	remove(cptr);											/* delete file if it already exists. Otherwise, attach_unit() would open r+w */
	/* get the output file by using regular attach routine */
    result = attach_unit(uptr, cptr);

    if (result != SCPE_OK) {
       if (IS_DEBUG) printf("problem attaching file\n");
	   gdImageDestroy(image);			/* free up the canvas memory */
	   image = NULL;
       return result;
    }

	/* set up the basic colors after image created */
	/* (by the way, these calls don't allocate any memory in or out of the image buffer. They just populate its "colors-used" table */
	white_background = gdImageColorAllocate(image,WHITE);	/* white is background */
	black_pen  = gdImageColorAllocate(image,BLACK);			/* load up black color */
	blue_pen   = gdImageColorAllocate(image,BLUE);			/* load up blue color */
	red_pen    = gdImageColorAllocate(image,RED);			/* load up red color */
	green_pen  = gdImageColorAllocate(image,GREEN);			/* load up green color */
    yellow_pen = gdImageColorAllocate(image,YELLOW); 		/* load up yellow color */
	purple_pen = gdImageColorAllocate(image,PURPLE);		/* load up purple color */
    ltgrey_pen = gdImageColorAllocate(image,LTGREY); 		/* load up light grey color */
    grey_pen   = gdImageColorAllocate(image,GREY);    		/* load up grey color */

    if ( (white_background == -1) || (black_pen == -1) ||
    	 (red_pen    == -1) || (blue_pen == -1) || (green_pen == -1) ||
       	 (purple_pen == -1) || (ltgrey_pen == -1) || (grey_pen == -1) ) {
           if (IS_DEBUG) printf("problem allocating pen colors\n");
           return SCPE_MEM;
    }

	CLRBIT(plot_dsw, PLOT1627_DSW_NOT_READY);				/* we're in business */

	plot_pen = PEN_UP;
	CLRBIT(plot_unit->flags, UNIT_PEN);

    update_pen();                                       	/* routine to ensure pen is okay */
	plot_used = FALSE;										/* plotter page is blank */
	return SCPE_OK;
}

/* pen updating routine, called at attach and whenever we reset the values */

static void update_pen (void)
{
	int color;
	int width;

     if (! IS_ONLINE(plot_unit)) return;     /* only do this if attached */

     /* pick up latest color as active pen */
     color = GET_COLOR;
     switch (color) {
	     case PEN_BLACK:
	          plot_pcolor = black_pen;
	          break;

	     case PEN_RED:
	          plot_pcolor = red_pen;
	          break;

	     case PEN_BLUE:
	          plot_pcolor = blue_pen;
	          break;

	     case PEN_GREEN:
	          plot_pcolor = green_pen;
	          break;

	     case PEN_YELLOW:
	          plot_pcolor = yellow_pen;
	          break;

	     case PEN_PURPLE:
	          plot_pcolor = purple_pen;
	          break;

	     case PEN_LTGREY:
	          plot_pcolor = ltgrey_pen;
	          break;

	     case PEN_GREY:
	          plot_pcolor = grey_pen;
	          break;

	     default:
	          if (IS_DEBUG) printf("invalid pen color state\n");
	          plot_pcolor = black_pen;
	          break;
     }

     /* set up anti-aliasing for the line */
     gdImageSetAntiAliased(image, plot_pcolor);

     /* pick up latest width for pen */
     width = GET_WIDTH();
     switch (width) {
	     case PEN_SINGLE:
             plot_pwidth = 1;
             gdImageSetThickness(image, 1);
             break;

	     case PEN_TRIPLE:
             plot_pwidth = 3;
             gdImageSetThickness(image, 3);
             break;

	     case PEN_DOUBLE:
             plot_pwidth = 2;
             gdImageSetThickness(image, 2);
             break;

	     case PEN_QUAD:
             plot_pwidth = 4;
             gdImageSetThickness(image, 4);
             break;

	     default:
             if (IS_DEBUG) printf("invalid pen width\n");
             plot_pwidth = 1;
             gdImageSetThickness(image, 1);
             break;
     }

     /* now ensure the pen state is accurate */
     plot_pen = IS_PENDOWN ? PEN_DOWN : PEN_UP;
     return;
}

/* plot_detach - detach file from simulated plotter */

static t_stat plot_detach (UNIT *uptr)
{
	char * buf, * fname;
	int32 size, result, saveit;
	FILE * fp;
	t_stat rval = SCPE_OK;			/* return value */

    SETBIT(plot_dsw, PLOT1627_DSW_NOT_READY);

	if (! (uptr->flags & UNIT_ATT))	/* not currently attached; don't proceed */
		return SCPE_OK;

									/* if -w flag was passed on attach: save file if there was plotter activity, otherwise delete it */
									/* if -w flag was not passed on attached, always save the file */
	saveit = (plot_used || ! delete_if_unused) && (image != NULL);

	if (saveit) {					/* copy images to files, close files, set device to detached, free gd memory */
		if ((buf = gdImageGifPtr(image,&size)) == NULL) {
		   if (IS_DEBUG) printf("failure creating GIF in-memory\n");
		   return SCPE_MEM;
		}

		fp = uptr->fileref;			/* get file attached to unit */

		if (fseek(fp,0,SEEK_SET) == 0) {		/* first we reset to begin of file */
			if (IS_DEBUG) printf("wrote out GIF to file\n");
			result = fwrite(buf,1,size,fp);		/* write out our image to the file */
		}
		else
			result = 0;				/* make it look like the write failed so we return error status */
	
		gdFree(buf);				/* free up the memory of GIF format */
	}
	else {							/* make a copy of the filename so we can delete it after detach */
		if ((fname = malloc(strlen(uptr->filename)+1)) != NULL)
			strcpy(fname, uptr->filename);
	}

	if (image != NULL) {
		gdImageDestroy(image);		/* free up the canvas memory */
		image = NULL;
	}

    rval = detach_unit(uptr);		/* have simh close the file */

	if (saveit) {					/* if we wrote the file, check that write was OK */
		if (result != size) {		/* report error writing file */
			if (IS_DEBUG) printf("error in write of image file\n");
			rval = SCPE_IOERR;
		}
    }
	else {							/* if we did not write the file, delete the file */
		if (fname == NULL) {
			rval = SCPE_MEM;		/* we previously failed to allocate a copy of the filename (this will never happen) */
		}
		else {
			remove(fname);			/* remove the file and free the copy of the filename */
			free(fname);
		}
	}

	return rval;
}

/* process_cmd - implement the drawing actions of the plotter */

static void process_cmd (void)
{
	int32 oldx, oldy;

    /* first see if we set any changes to pen or position, do an update */
    if (need_update) {
       update_pen();
       need_update = FALSE;
    }

   	/* will move pen one step or flip pen up or down */
    oldx = plot_xpos;
    oldy = plot_ypos;

    switch (plot_cmd) {
	    case 1:            /* raise pen command */
	         plot_pen = PEN_UP;
			 CLRBIT(plot_unit->flags, UNIT_PEN);
	         return;
	         break;

	    case 2:            /* +Y command */
	         ++plot_ypos;
	         break;

	    case 4:            /* -Y command */
	         --plot_ypos;
	         break;

	    case 8:            /* -X command */
	         --plot_xpos;
	         break;

	    case 10:            /* -X +Y command */
	         --plot_xpos;
	         ++plot_ypos;
	         break;

	    case 12:            /* -X -Y command */
	         --plot_xpos;
	         --plot_ypos;
	         break;

	    case 16:            /* +X command */
	         ++plot_xpos;
	         break;

	    case 18:            /* +X +Y command */
	         ++plot_xpos;
	         ++plot_ypos;
	         break;

	    case 20:            /* +X -Y pen command */
	         ++plot_xpos;
	         --plot_ypos;
	         break;

	    case 32:            /* lower pen command */
	         plot_pen = PEN_DOWN;
			 SETBIT(plot_unit->flags, UNIT_PEN);
	         return;
	         break;

	    default:
	         if (IS_DEBUG) printf("invalid plotter command\n");
	         return;
	         break;
    }

	/* On the real plotter, y motions were physically restricted at the ends of travel.
	 * We simulate this by clipping the plot_ypos value. Three +y movements at the right
	 * end of travel followed by three -y movements will back up 3 positions, just as it would have on
	 * the physical plotter. Without clipping, the pen would end up where it started, which 
	 * is incorrect. (Hopefully, good 1130 plotting software would never make this happen anyhow!)
	 */

	 if (plot_ypos < 0)
		plot_ypos = 0;
	 else if (plot_ypos > plot_ymax)
		plot_ypos = plot_ymax;

	/* We do allow X overtravel though, as the drum simply turned past the end of the paper. Three +x
	 * movements past the end of the paper followed by three -x movements would put the pen back at the
	 * edge of the paper.
	 */

	 if ((plot_xpos < 0) || (plot_xpos > plot_xmax)) {
           /* if so, ignore as 1627 has no way of signalling error */
           if (IS_DEBUG) printf(
              "attempted to move carriage off paper edge %d %d for command %d\n",
              plot_xpos,plot_ypos,plot_cmd);

		   return;		// no drawing takes place if the pen is off of the paper!
    }

    /* only draw a line if the pen was down during the movement command */
    if (plot_pen) {
		gdImageLine(image, plot_ymax-plot_ypos, plot_xmax-plot_xpos, plot_ymax-oldy, plot_xmax-oldx, gdAntiAliased);
		plot_used = TRUE;										/* remember that we drew something */
       /* semantics are 0,0 point is lower right */
    }

	return;
}

/* plot_set_length - validate and store the length of the paper */

static t_stat plot_set_length (UNIT *uptr, int32 set, CONST char *ptr, void *desc)
{
	char *cptr;
	int32 val;

#define LONGEST_ROLL 1440000                    /* longest is 120', 14400", 1,440,000 .01"s */

	if (ptr == NULL) {							/* check for missing argument */
		printf("Command format is: set plot length=nnn\n");
		return SCPE_ARG;
	}

	val = strtotv (ptr, &cptr, (uint32) 10);   /* sim routine to get value */
	if ((val < 1) | (val >= LONGEST_ROLL)) {   /* check valid range */
		if (IS_DEBUG) printf("setting paper more than 120' or less than 1 inch\n");
		return SCPE_ARG;
	}

	/* origin zero drawing, reduce by 1 but show command will fudge by adding it back */
	*((int32 *)((REG *) desc)->loc) = val - 1;

	return SCPE_OK;
}

/* plot_set_pos - validate and store the new position of the carriage */

static t_stat plot_set_pos (UNIT *uptr, int32 set, CONST char *ptr, void *desc)
{
	char *cptr;
	int32 val;
	int32 max;

	max = (set == 1) ? plot_ymax : plot_xmax;
	val = strtotv (ptr, &cptr, (uint32) 10);
	if ((val < 0) | (val > max)) {
		if (IS_DEBUG) printf("error moving carriage off paper edge\n");
			return SCPE_ARG;
	}

	*((int32 *)((REG *) desc)->loc) = val;

	return SCPE_OK;
}

/* routine to display the paper length and carriage position
 * cannot use regular simh routine because it prints values twice,
 * once for device and once for unit
 */

static t_stat plot_show_vals (FILE *fp, UNIT *uptr, int32 val, CONST void *descrip)
{
	fprintf(fp, "length=%d, Xpos=%d, Ypos=%d",plot_xmax+1, plot_xpos,plot_ypos);
    return SCPE_OK;
}

/* routine to add a terminating NL character when 'show plot length'
 * or equivalent for xpos or ypos is issued, as simh will not append for us */

static t_stat plot_show_nl(FILE *fp, UNIT *uptr, int32 val, CONST void *descrip)
{
	int32 disp;
	char *label;

	disp  = (val == 2) ? plot_xmax + 1 : ((val == 1) ? plot_ypos : plot_xpos);
	label = (val == 2) ? "length=" : ((val == 1) ? "Ypos=" : "Xpos=");

	fprintf(fp, "%s%d\n", label, disp);
    return SCPE_OK;
}

/* plot_validate_change - force the update_pen routine to be called after user changes pen setting */

static t_stat plot_validate_change (UNIT *uptr, int32 set, CONST char *ptr, void *desc)
{
	need_update = TRUE;
	return SCPE_OK;
}

#endif /* ENABLE_PLOT_SUPPORT */

