/* ibm1130_plot.c: IBM 1130 1627 plotter emulation

   Based on the SIMH simulator package written by Robert M Supnik

   Brian Knittel
   Revision History

   2004.10.22 - Written.
   2006.1.2 - Rewritten as plotter routine by Carl V Claunch

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

#include "gd.h"

/***************************************************************************************
 *  1627 model 1 plotter (based on Calcomp 535 which was sold as IBM 1627)
 *
 *  - 11" wide carriage, addressible in .01" steps
 *  - continous sheet paper up to 120' in length
 *  - sheet moveable in .01" steps, either direction
 *  - switchable pen, in various colors and line widths
 *
 *  Simulator implementation will create a JPEG image corresponding to a 
 *  landscape mode sheet of paper, the width of the carriage at 11".
 *  A diagram of more than 8" of paper travel will span printed pages
 *  in landscape mode. 
 *
 *  When an 'att plot' command is issued a file is created based on the
 *  default or currently set values of paper length, starting
 *  position of the pen in both X and Y axes, pen color and pen width.
 *  Based on the number of logical pages of paper, the command will create
 *  the proper size canvas internally and create the output JPEG file.
 *  
 *  When a 'det plot' command is issued, the plotter image will be converted 
 *  into the file that was specified during the attach process. The
 *  image is not viewable until this point, unless an examine plot is
 *  issued which will dump the current state of the paper into the file.
 *  
 *  The 'set plot' command can set pen width, paper length, pen color,
 *  current carriage X and Y coordinates. Paper length can be set
 *  to alter the default of 800 (8"); changes are ignored until
 *  the next 'attach' command. The current carriage x and y positions
 *  can be set at any time and will go into effect immediately, just
 *  as the pen color and pen width can be altered on the fly.
 *
 * NOTE: requires gd library and definition of ENABLE_PLOT_SUPPORT in makefile or Visual C configuration
 * gd is not included in the main simh and ibm1130.org distributions at the present time.
 ***************************************************************************************/

#define PLOT1627_DSW_OP_COMPLETE			0x8000
#define PLOT1627_DSW_BUSY					0x0200
#define PLOT1627_DSW_NOT_READY				0x0100

#define IS_ONLINE(u) (((u)->flags & (UNIT_ATT|UNIT_DIS)) == UNIT_ATT)
#define IS_DEBUG 	 ((plot_unit->flags & UNIT_DEBUG) == UNIT_DEBUG)
#define IS_PENDOWN 	 ((plot_unit->flags & UNIT_PEN) == UNIT_PEN)

static t_stat plot_svc    (UNIT *uptr);				/* activity routine */
static t_stat plot_reset  (DEVICE *dptr);			/* reset of 1130 */
static t_stat plot_attach (UNIT *uptr, char *cptr);	/* attach, loads plotter */
static t_stat plot_detach (UNIT *uptr);				/* detach and save image */
static t_stat plot_examine (UNIT *uptr);			/* update file with current canvas */
static t_stat plot_set_length (UNIT *uptr, int32 val, char * ptr, void *desc);	/* set paper length */
static t_stat plot_set_pos (UNIT *uptr, int32 val, char * ptr, void *desc);		/* reset current X/Y position */
static t_stat plot_show_vals(FILE *fp, UNIT *uptr, int32 val, void *descrip);	/* print x, y and length */
static t_stat plot_show_nl(FILE *fp, UNIT *uptr, int32 val, void *descrip);  	/* overcome wacky simh behavior */
static void   update_pen(void);        				 /* will ensure pen action is correct when changes made */
static t_stat plot_validate_change (UNIT *uptr, int32 val, char * ptr, void *desc); /* when set command issued */
static void   process_cmd(void);					/* does actual drawing for plotter */

extern int32 sim_switches;							/* switches set on simh command */
static int16 plot_dsw  = 0;							/* device status word */
static int16 plot_cmd  = 0;							/* the command to process */
static int32 plot_wait = 1000;						/* plotter movement wait */
static int32 plot_xpos = 0;							/* current X position */
static int32 plot_xmax = 799;						/* end of paper */
static int32 plot_ypos = 0;							/* current Y position */
static int32 plot_ymax = 1099;						/* right edge of carriage */

#define PEN_DOWN 0x80000000
#define PEN_UP   0x00000000
static int32 plot_pen = PEN_UP;						/* current pen position */

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
static int need_update = 0;							/* flag to force and update_pen() */
static gdImagePtr  image;							/* pointer to our canvas */

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

#define SET_COLOR(op) {plot_unit[0].flags &= ~UNIT_COLOR; plot_unit[0].flags |= (op);}
#define GET_COLOR (plot_unit[0].flags & UNIT_COLOR)

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
#define SET_WIDTH(cd)	{plot_unit[0].flags &= ~UNIT_WIDTH; un.flags |= (cd);}

UNIT plot_unit[] = {
	{ UDATA (&plot_svc, UNIT_ATTABLE, 0) },
};

REG plot_reg[] = {
	{ HRDATA (DSW, 	    plot_dsw,  16) },			/* device status word */
	{ DRDATA (WTIME,    plot_wait, 24), PV_LEFT },		/* plotter movement wait */
	{ DRDATA (Xpos, plot_xpos,  32), PV_LEFT },		/* Current X Position*/
	{ DRDATA (Ypos, plot_ypos,  32), PV_LEFT },		/* Current Y Position*/
	{ FLDATA (PenDown, plot_pen, 0)},				/* Current pen position - 1 = down */
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

void xio_1627_plotter (iocc_addr, iocc_func, iocc_mod)
{
	char msg[80];

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
			xio_error("Control XIO not supported by 1627 plotter");
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
	char * buf;
	int32 size;

	sim_cancel(plot_unit);

	CLRBIT(plot_dsw, PLOT1627_DSW_BUSY | PLOT1627_DSW_OP_COMPLETE);

    if (IS_DEBUG) printf("reset routine for Plotter\n");

	CLRBIT(ILSW[3], ILSW_3_1627_PLOTTER);
	calc_ints();

	return SCPE_OK;
}


/* plot_attach - attach file to simulated plotter */

static t_stat plot_attach (UNIT *uptr, char *cptr)
{
	t_stat result;

    CLRBIT(uptr->flags, UNIT_DEBUG);
	if (sim_switches & SWMASK('D')) SETBIT(uptr->flags, UNIT_DEBUG);

	/* get the output file by using regular attach routine */
    result = attach_unit(uptr, cptr);

    if (result != SCPE_OK) {
       if (IS_DEBUG) printf("problem attaching file\n");
       return result;
    }

	SETBIT(plot_dsw, PLOT1627_DSW_NOT_READY);				/* assume failure */

	/* set up our canvas at the desired size */
	image = gdImageCreate(plot_ymax+1,plot_xmax+1);			/* create our canvas */
    if (image == NULL) {
       if (IS_DEBUG) printf("problem creating image canvas\n");
       return SCPE_MEM;
    }

	/* set up the basic colors after image created */
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

    update_pen();                                       	/* routine to ensure pen is okay */

	return SCPE_OK;
}

/* pen updating routine, called at attach and whenever we reset the values */

void update_pen (void)
{
	int color;
	int width;

     if (!IS_ONLINE(plot_unit)) return;     /* only do this if attached */

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
	char * buf;
	int32 size;
	FILE * fp;
	int32 result;

    SETBIT(plot_dsw, PLOT1627_DSW_NOT_READY);

	/* copy images to files, close files, set device to detached, free gd memory */

    buf = gdImageGifPtr(image,&size);
    if (! buf) {
       if (IS_DEBUG) printf("failure creating GIF in-memory\n");
       return SCPE_MEM;
    }

    fp = uptr->fileref;						/* get file attached to unit */

    if (! fseek(fp,0,SEEK_SET)) {			/* first we reset to begin of file */
       if (IS_DEBUG) printf("wrote out GIF to file\n");
       result = fwrite(buf,1,size,fp);		/* write out our image to the file */
    }

    gdFree(buf);							/* free up the memory of GIF format */
    gdImageDestroy(image);					/* free up the canvas memory */

    if (result != size) {					/* some problem writing it */
       if (IS_DEBUG) printf("error in write of image file\n");
       return SCPE_IOERR;
    }

    return detach_unit(uptr);				/* have simh close the file */
}

/* process_cmd - implement the drawing actions of the plotter */

static void process_cmd (void)
{
	int32 oldx, oldy;

    /* first see if we set any changes to pen or position, do an update */
    if (need_update) {
       update_pen();
       need_update = 0;
    }

   	/* will move pen one step or flip pen up or down */
    oldx = plot_xpos;
    oldy = plot_ypos;

    switch (plot_cmd) {
	    case 1:            /* raise pen command */
	         plot_pen = PEN_UP;
	         plot_unit->flags = plot_unit->flags & (~UNIT_PEN);
	         return;
	         break;

	    case 2:            /* +Y command */
	         plot_ypos = plot_ypos + 1;
	         break;

	    case 4:            /* -Y command */
	         plot_ypos = plot_ypos - 1;
	         break;

	    case 8:            /* -X command */
	         plot_xpos = plot_xpos - 1;
	         break;

	    case 10:            /* -X +Y command */
	         plot_xpos = plot_xpos - 1;
	         plot_ypos = plot_ypos + 1;
	         break;

	    case 12:            /* -X -Y command */
	         plot_xpos = plot_xpos - 1;
	         plot_ypos = plot_ypos - 1;
	         break;

	    case 16:            /* +X command */
	         plot_xpos = plot_xpos + 1;
	         break;

	    case 18:            /* +X +Y command */
	         plot_xpos = plot_xpos + 1;
	         plot_ypos = plot_ypos + 1;
	         break;

	    case 20:            /* +X -Y pen command */
	         plot_xpos = plot_xpos + 1;
	         plot_ypos = plot_ypos - 1;
	         break;

	    case 32:            /* lower pen command */
	         plot_pen = PEN_DOWN;
	         plot_unit->flags = plot_unit->flags | UNIT_PEN;
	         return;
	         break;

	    default:
	         if (IS_DEBUG) printf("invalid plotter command\n");
	         return;
	         break;
    }

    /* check to see if carriage has moved off any edge */
    if ((plot_xpos > (plot_xmax+1)) || (plot_ypos > (plot_ymax+1)) ||
           (plot_xpos < 0) || (plot_ypos < 0)) {
           /* if so, ignore as 1627 has no way of signalling error */
           if (IS_DEBUG) printf(
              "attempted to move carriage off paper edge %d %d for command %d\n",
              plot_xpos,plot_ypos,plot_cmd);
           return;
    }

    /* only draw a line if the pen was down during the movement command */
    if (plot_pen) {
       gdImageLine(image, plot_ymax-plot_ypos, plot_xmax-plot_xpos, plot_ymax-oldy, plot_xmax-oldx, gdAntiAliased);
       /* semantics are 0,0 point is lower right */
    }

	return;
}

/* plot_set_length - validate and store the length of the paper */

static t_stat plot_set_length (UNIT *uptr, int32 set, char *ptr, void *desc)
{
	char *cptr;
	int32 val;

#define LONGEST_ROLL 1440000                    /* longest is 120', 14400", 1,440,000 .01"s */

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

static t_stat plot_set_pos (UNIT *uptr, int32 set, char *ptr, void *desc)
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

static t_stat plot_show_vals (FILE *fp, UNIT *uptr, int32 val, void *descrip)
{
	fprintf(fp, "length=%d, Xpos=%d, Ypos=%d",plot_xmax+1, plot_xpos,plot_ypos);
    return SCPE_OK;
}

/* routine to add a terminating NL character when 'show plot length'
 * or equivalent for xpos or ypos is issued, as simh will not append for us */

static t_stat plot_show_nl(FILE *fp, UNIT *uptr, int32 val, void *descrip)
{
	int32 disp;
	char *label;

	disp  = (val == 2) ? plot_xmax + 1 : ((val == 1) ? plot_ypos : plot_xpos);
	label = (val == 2) ? "length=" : ((val == 1) ? "Ypos=" : "Xpos=");

	fprintf(fp, "%s%d\n", label, disp);
    return SCPE_OK;
}

/* plot_validate_change - force the update_pen routine to be called after user changes pen setting */

static t_stat plot_validate_change (UNIT *uptr, int32 set, char *ptr, void *desc)
{
	need_update = 1;
	return SCPE_OK;
}

#endif /* ENABLE_PLOT_SUPPORT */
