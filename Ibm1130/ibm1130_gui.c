/* ibm1130_gui.c: IBM 1130 CPU simulator Console Display
 *
 *  Based on the SIMH package written by Robert M Supnik
 *
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to simh@ibm1130.org
 *
 * 30-Dec-05 BLK	Fixed mask for IAR and SAR register display and added display
 *					of Arithmetic Factor, per Carl Claunch.
 *
 * 09-Apr-04 BLK	Changed code to use stock windows cursor IDC_HAND if available
 *
 * 02-Dec-02 BLK	Changed display, added printer and card reader icons
 *					Added drag and drop support for scripts and card decks
 *					Added support for physical card reader and printer (hides icons)
 *
 * 17-May-02 BLK	Pulled out of ibm1130_cpu.c
 */

/* ------------------------------------------------------------------------
 * Definitions
 * ------------------------------------------------------------------------ */

#include <stdarg.h>
#include <ctype.h>
#include <math.h>

#include "ibm1130_defs.h"
#include "ibm1130res.h"

#define UPDATE_BY_TIMER

#ifdef UPDATE_BY_TIMER
#  define UPDATE_INTERVAL	20				/* set to desired number of updates/second */
#else
#  define UPDATE_INTERVAL	5000 			/* GUI: set to 100000/f where f = desired updates/second of 1130 time */
#endif

#define UNIT_V_CR_EMPTY	   (UNIT_V_UF + 5)			/* NOTE: THESE MUST MATCH THE DEFINITION IN ibm1130_cr.c */
#define UNIT_CR_EMPTY	   (1u << UNIT_V_CR_EMPTY)
#define UNIT_V_PHYSICAL	   (UNIT_V_UF + 9)
#define UNIT_PHYSICAL	   (1u << UNIT_V_PHYSICAL)

#define UNIT_V_PHYSICAL_PTR	(UNIT_V_UF + 10)		/* NOTE: THESE MUST MATCH THE DEFINITION IN ibm1130_prt.c */
#define UNIT_PHYSICAL_PTR (1u << UNIT_V_PHYSICAL_PTR)

/* I think I had it wrong; Program Load actually does start the processor after
 * reading in the card?
 */

#define PROGRAM_LOAD_STARTS_CPU

/* ------------------------------------------------------------------------
 * Function declarations
 * ------------------------------------------------------------------------ */

t_stat console_reset (DEVICE *dptr);

/* ------------------------------------------------------------------------ 
 * Console display - on Windows builds (only) this code displays the 1130 console
 * and toggle switches. It really enhances the experience.
 *
 * Currently, when the IPS throttle is nonzero, I update the display after every
 * UPDATE_INTERVAL instructions, plus or minus a random amount to avoid aliased
 * sampling in loops.  When UPDATE_INTERVAL is defined as zero, we update every
 * instruction no matter what the throttle. This makes the simulator too slow
 * but it's cool and helpful during development.
 * ------------------------------------------------------------------------ */

#define UNIT_V_DISPLAY   (UNIT_V_UF + 0)
#define UNIT_DISPLAY	 (1u << UNIT_V_DISPLAY)

MTAB console_mod[] = {
	{ UNIT_DISPLAY, 0,            "off", "OFF", NULL },
	{ UNIT_DISPLAY, UNIT_DISPLAY, "on",  "ON",  NULL },
	{ 0 }
};

UNIT console_unit = {UDATA (NULL, UNIT_DISABLE|UNIT_DISPLAY, 0) };

DEVICE console_dev = {
	"GUI", &console_unit, NULL, console_mod,
	1, 16, 16, 1, 16, 16,
	NULL, NULL, console_reset,
	NULL, NULL, NULL
};

/* reset for the "console" display device  */

extern UNIT cr_unit;									/* pointers to 1442 and 1132 (1403) printers */
extern UNIT prt_unit;

#ifndef GUI_SUPPORT
	void update_gui (int force)				  {}		/* stubs for non-GUI builds */
	void forms_check (int set)				  {}
	void print_check (int set)				  {}
	void keyboard_select (int select)		  {}
	void keyboard_selected (int select) 	  {}
	void disk_ready (int ready)         	  {}
	void disk_unlocked (int unlocked)   	  {}
	void gui_run (int running)          	  {} 
	static void init_console_window (void) 	  {}
	static void destroy_console_window (void) {}

	t_stat console_reset (DEVICE *dptr)		  					{return SCPE_OK;}
	void   stuff_cmd (char *cmd) 				  				{}
	t_bool stuff_and_wait (char *cmd, int timeout, int delay)	{return FALSE;}
	char  *read_cmdline (char *ptr, int size, FILE *stream)		{return read_line(ptr, size, stream);}
    void   remark_cmd (char *remark)			  				{sim_printf("%s\n", remark);}
#else

t_stat console_reset (DEVICE *dptr)
{
	if (! sim_gui) {
		SETBIT(console_unit.flags, UNIT_DIS);			/* disable the GUI */
		CLRBIT(console_unit.flags, UNIT_DISPLAY);		/* turn the GUI off */
	}

	update_gui(FALSE);
	return SCPE_OK;
}

/* scp_panic - report fatal internal programming error */

void scp_panic (const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

#ifdef _WIN32
	/* only _WIN32 is defined right now */

#include <windows.h>

#define BUTTON_WIDTH  90
#define BUTTON_HEIGHT 50

#define IDC_KEYBOARD_SELECT		0
#define IDC_DISK_UNLOCK			1
#define IDC_RUN					2
#define IDC_PARITY_CHECK		3
#define IDC_UNUSED				4
#define IDC_FILE_READY			5
#define IDC_FORMS_CHECK			6
#define IDC_POWER_ON			7
#define IDC_POWER				8
#define IDC_PROGRAM_START		9
#define IDC_PROGRAM_STOP		10
#define IDC_LOAD_IAR			11
#define IDC_KEYBOARD			12
#define IDC_IMM_STOP			13
#define IDC_RESET				14
#define IDC_PROGRAM_LOAD		15

#define IDC_TEAR				16	/* standard button */
#define IDC_1442				17	/* device images */
#define IDC_1132				18

#define LAMPTIME        500			/* 500 msec delay on updating */
#define FLASH_TIMER_ID    1
#define UPDATE_TIMER_ID   2

#define RUNSWITCH_X 689				/* center of the run mode switch dial */
#define RUNSWITCH_Y 107
#define TOGGLES_X	122				/* left edge of series of toggle switches */

#define TXTBOX_X 	200				/* text labels showing attached devices */
#define TXTBOX_Y	300
#define TXTBOX_WIDTH   	195
#define TXTBOX_HEIGHT	 12

static BOOL   class_defined = FALSE;
static HWND   hConsoleWnd = NULL;
static HBITMAP hBitmap = NULL;
static HFONT  hFont = NULL;
static HFONT  hBtnFont = NULL;
static HFONT  hTinyFont = NULL;
static HBRUSH hbLampOut = NULL;
static HBRUSH hbWhite = NULL;
static HBRUSH hbBlack = NULL;
static HBRUSH hbGray  = NULL;
static HPEN   hSwitchPen = NULL;
static HPEN   hWhitePen  = NULL;
static HPEN   hBlackPen  = NULL;
static HPEN   hLtGreyPen = NULL;
static HPEN   hGreyPen   = NULL;
static HPEN   hDkGreyPen = NULL;
static int    hUpdateTimer = 0;
static int    hFlashTimer  = 0;

static HCURSOR hcArrow = NULL;
static HCURSOR hcHand  = NULL;
static HINSTANCE hInstance;
static HDC hCDC = NULL;
static char szConsoleClassName[] = "1130CONSOLE";
static DWORD PumpID = 0;
static HANDLE hPump = INVALID_HANDLE_VALUE;
static int bmwid, bmht;
static HANDLE hbm1442_full, hbm1442_empty, hbm1442_eof, hbm1442_middle;
static HANDLE hbm1132_full, hbm1132_empty;

static struct tag_btn {
	int x, y, wx, wy;
	char *txt;
	BOOL pushable, state;
	COLORREF clr;
	HBRUSH hbrLit, hbrDark;
	HWND   hBtn;
	BOOL   subclassed;

} btn[] = {
	0, 0, BUTTON_WIDTH, BUTTON_HEIGHT,	"KEYBOARD\nSELECT",		FALSE,	FALSE,	RGB(255,255,180),	NULL, NULL, NULL,	TRUE,
	0, 1, BUTTON_WIDTH, BUTTON_HEIGHT,	"DISK\nUNLOCK",			FALSE, 	TRUE,	RGB(255,255,180),	NULL, NULL, NULL,	TRUE,
	0, 2, BUTTON_WIDTH, BUTTON_HEIGHT,	"RUN",					FALSE,	FALSE,	RGB(0,255,0),		NULL, NULL, NULL,	TRUE,
	0, 3, BUTTON_WIDTH, BUTTON_HEIGHT,	"PARITY\nCHECK",		FALSE,	FALSE,	RGB(255,0,0),		NULL, NULL, NULL,	TRUE,

	1, 0, BUTTON_WIDTH, BUTTON_HEIGHT,	"",						FALSE, 	FALSE,	RGB(255,255,180),	NULL, NULL, NULL,	TRUE,
	1, 1, BUTTON_WIDTH, BUTTON_HEIGHT,	"FILE\nREADY",			FALSE, 	FALSE,	RGB(0,255,0),		NULL, NULL, NULL,	TRUE,
	1, 2, BUTTON_WIDTH, BUTTON_HEIGHT,	"FORMS\nCHECK",			FALSE, 	FALSE,	RGB(255,255,0),		NULL, NULL, NULL,	TRUE,
	1, 3, BUTTON_WIDTH, BUTTON_HEIGHT,	"POWER\nON",			FALSE, 	TRUE,	RGB(255,255,180),	NULL, NULL, NULL,	TRUE,

	2, 0, BUTTON_WIDTH, BUTTON_HEIGHT,	"POWER",				TRUE,	FALSE,	RGB(255,255,180), 	NULL, NULL, NULL,	TRUE,
	2, 1, BUTTON_WIDTH, BUTTON_HEIGHT,	"PROGRAM\nSTART",		TRUE,	FALSE,	RGB(0,255,0),		NULL, NULL, NULL,	TRUE,
	2, 2, BUTTON_WIDTH, BUTTON_HEIGHT,	"PROGRAM\nSTOP",		TRUE,	FALSE,	RGB(255,0,0),		NULL, NULL, NULL,	TRUE,
	2, 3, BUTTON_WIDTH, BUTTON_HEIGHT,	"LOAD\nIAR",			TRUE,	FALSE,	RGB(0,0,255),		NULL, NULL, NULL,	TRUE,

	3, 0, BUTTON_WIDTH, BUTTON_HEIGHT,	"KEYBOARD",				TRUE, 	FALSE,	RGB(255,255,180),	NULL, NULL, NULL,	TRUE,
	3, 1, BUTTON_WIDTH, BUTTON_HEIGHT,	"IMM\nSTOP",			TRUE, 	FALSE,	RGB(255,0,0),		NULL, NULL, NULL,	TRUE,
	3, 2, BUTTON_WIDTH, BUTTON_HEIGHT,	"CHECK\nRESET",			TRUE, 	FALSE,	RGB(0,0,255),		NULL, NULL, NULL,	TRUE,
	3, 3, BUTTON_WIDTH, BUTTON_HEIGHT,	"PROGRAM\nLOAD",		TRUE, 	FALSE,	RGB(0,0,255),		NULL, NULL, NULL,	TRUE,
							
	TXTBOX_X+40, TXTBOX_Y+25, 35, 12, 	"Tear",					TRUE,	FALSE,	0,					NULL, NULL, NULL,	FALSE,
	635, 238, 110, 110,                 "EMPTY_1442",			TRUE,   FALSE,  0,					NULL, NULL, NULL,	FALSE,
	635, 366, 110, 110,                 "EMPTY_1132",		    TRUE,   FALSE,  0,					NULL, NULL, NULL,	FALSE,
};
#define NBUTTONS (sizeof(btn) / sizeof(btn[0]))

#define STATE_1442_EMPTY	0		/* no cards (no file attached) */
#define STATE_1442_FULL		1		/* cards in hopper (file attached at BOF) */
#define STATE_1442_MIDDLE	2		/* cards in hopper and stacker (file attached, neither EOF nor BOF) */
#define STATE_1442_EOF		3		/* cards in stacker (file attached, at EOF) */
#define STATE_1442_HIDDEN	4		/* simulator is attached to physical card reader */

#define STATE_1132_EMPTY	0		/* no paper hanging out of printer */
#define STATE_1132_FULL		1		/* paper hanging out of printer */
#define STATE_1132_HIDDEN	2		/* printer is attached to physical printer */

static struct tag_txtbox {
	int x, y;
	char *txt;
	char *unitname;
	int idctrl;
} txtbox[] = {
	TXTBOX_X, TXTBOX_Y, 	"Card Reader",	"CR",		-1,
	TXTBOX_X, TXTBOX_Y+ 25, "Printer",		"PRT",		IDC_1132,
	TXTBOX_X, TXTBOX_Y+ 50, "Disk 1",		"DSK0",		-1,
	TXTBOX_X, TXTBOX_Y+ 75, "Disk 2",		"DSK1",		-1,
	TXTBOX_X, TXTBOX_Y+100, "Disk 3",		"DSK2",		-1,
	TXTBOX_X, TXTBOX_Y+125, "Disk 4",		"DSK3",		-1,
	TXTBOX_X, TXTBOX_Y+150, "Disk 5",		"DSK4",		-1,
};
#define NTXTBOXES (sizeof(txtbox) / sizeof(txtbox[0]))

#define TXTBOX_BOTTOM	(TXTBOX_Y+150)

static void init_console_window (void);
static void destroy_console_window (void);
LRESULT CALLBACK ConsoleWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI Pump (LPVOID arg);
static void accept_dropped_file (HANDLE hDrop);
static void tear_printer (void);

#define NIXOBJECT(hObj) if (hObj != NULL) {DeleteObject(hObj); hObj = NULL;}

/* ------------------------------------------------------------------------ 
 * init_console_window - display the 1130 console. Actually just creates a thread 
 * to run the Pump routine which does the actual work.
 * ------------------------------------------------------------------------ */

static void init_console_window (void)
{
	static BOOL did_atexit = FALSE;

	if (hConsoleWnd != NULL)
		return;

	if (PumpID == 0)
		hPump = CreateThread(NULL, 0, Pump, 0, 0, &PumpID);

	if (! did_atexit) {
		atexit(destroy_console_window);
		did_atexit = TRUE;
	}
}

/* ------------------------------------------------------------------------ 
 * destroy_console_window - delete GDI objects.
 * ------------------------------------------------------------------------ */

static void destroy_console_window (void)
{
	int i;

	if (hConsoleWnd != NULL)
		SendMessage(hConsoleWnd, WM_CLOSE, 0, 0);	/* cross thread call is OK */

	if (hPump != INVALID_HANDLE_VALUE) {			/* this is not the most graceful way to do it */
		TerminateThread(hPump, 0);
		hPump  = INVALID_HANDLE_VALUE;
		PumpID = 0;
		hConsoleWnd = NULL;
	}
	if (hCDC != NULL) {
		DeleteDC(hCDC);
		hCDC = NULL;
	}

	NIXOBJECT(hBitmap)
	NIXOBJECT(hbLampOut)
	NIXOBJECT(hFont)
	NIXOBJECT(hBtnFont);
	NIXOBJECT(hTinyFont);
	NIXOBJECT(hcHand)
	NIXOBJECT(hSwitchPen)
	NIXOBJECT(hLtGreyPen)
	NIXOBJECT(hGreyPen)
	NIXOBJECT(hDkGreyPen)

	for (i = 0; i < NBUTTONS; i++) {
		NIXOBJECT(btn[i].hbrLit);
		NIXOBJECT(btn[i].hbrDark);
	}

/*	if (class_defined) {
		UnregisterClass(hInstance, szConsoleClassName);
		class_defined = FALSE;
	}
*/
}

/* ------------------------------------------------------------------------ 
 * these variables hold the displayed versions of the system registers 
 * ------------------------------------------------------------------------ */

static int shown_iar = 0, shown_sar = 0, shown_sbr = 0, shown_afr = 0, shown_acc = 0, shown_ext  = 0;
static int shown_op  = 0, shown_tag = 0, shown_irq = 0, shown_ccc = 0, shown_cnd = 0, shown_wait = 0;
static int shown_ces = 0, shown_arf = 0, shown_runmode = MODE_RUN;
static int CND;

/* ------------------------------------------------------------------------ 
 * RedrawRegion - mark a region for redrawing without background erase
 * ------------------------------------------------------------------------ */

static void RedrawRegion (HWND hWnd, int left, int top, int right, int bottom)
{
	RECT r;

	r.left   = left;
	r.top    = top;
	r.right  = right;
	r.bottom = bottom;

	InvalidateRect(hWnd, &r, FALSE);
}

/* ------------------------------------------------------------------------ 
 * RepaintRegion - mark a region for redrawing with background erase
 * ------------------------------------------------------------------------ */

static void RepaintRegion (HWND hWnd, int left, int top, int right, int bottom)
{
	RECT r;

	r.left   = left;
	r.top    = top;
	r.right  = right;
	r.bottom = bottom;

	InvalidateRect(hWnd, &r, TRUE);
}

/* ------------------------------------------------------------------------ 
 * update_gui - sees if anything on the console display has changed, and invalidates 
 * the changed regions. Then it calls UpdateWindow to force an immediate repaint. This
 * function (update_gui) should probably not be called every time through the main
 * instruction loop but it should be called at least whenever wait_state or int_req change, and then
 * every so many instructions.  It's also called after every simh command so manual changes are
 * reflected instantly.
 * ------------------------------------------------------------------------ */

void update_gui (BOOL force)
{	
	int i;
	BOOL state;
	static int in_here = FALSE;
	static int32 displayed = 0;
	RECT xin;

	if ((int32)(console_unit.flags & UNIT_DISPLAY) != displayed) {		/* setting has changed */
		displayed = console_unit.flags & UNIT_DISPLAY;
		if (displayed)
			init_console_window();
		else
			destroy_console_window();
	}

	if (hConsoleWnd == NULL)
		return;

	GUI_BEGIN_CRITICAL_SECTION				/* only one thread at a time, please */
	if (in_here) {
		GUI_END_CRITICAL_SECTION
		return;
	}
	in_here = TRUE;
	GUI_END_CRITICAL_SECTION

	CND = 0;	/* combine carry and V as two bits */
	if (C)
		CND |= 2;
	if (V)
		CND |= 1;

	int_lamps |= int_req;
	if (ipl >= 0)
		int_lamps |= (0x20 >> ipl);

	if (RUNMODE == MODE_LOAD)
		SBR = CES;			/* in load mode, SBR follows the console switches */

	if (IAR != shown_iar)
			{shown_iar = IAR; 		 RedrawRegion(hConsoleWnd, 75,    8, 364,  32);}	/* lamps: don't bother erasing bkgnd */
	if (SAR != shown_sar)
			{shown_sar = SAR; 		 RedrawRegion(hConsoleWnd, 75,   42, 364,  65);}
	if (ARF != shown_arf)
			{shown_arf = ARF; 		 RedrawRegion(hConsoleWnd, 75,  114, 364, 136);}
	if (ACC != shown_acc)
			{shown_acc = ACC; 		 RedrawRegion(hConsoleWnd, 75,  141, 364, 164);}
	if (EXT != shown_ext)
			{shown_ext = EXT; 		 RedrawRegion(hConsoleWnd, 75,  174, 364, 197);}
	if (SBR != shown_sbr)
			{shown_sbr = SBR; 		 RedrawRegion(hConsoleWnd, 75,   77, 364,  97);}
	if (OP  != shown_op)		  			 
			{shown_op  = OP;  		 RedrawRegion(hConsoleWnd, 501,   8, 595,  32);}
	if (TAG != shown_tag)
			{shown_tag = TAG; 		 RedrawRegion(hConsoleWnd, 501,  77, 595,  97);}

	if (int_lamps != shown_irq)
			{shown_irq = int_lamps;  RedrawRegion(hConsoleWnd, 501, 108, 595, 130);}

	if (CCC != shown_ccc)
			{shown_ccc = CCC;		 RedrawRegion(hConsoleWnd, 501, 141, 595, 164);}
	if (CND != shown_cnd)
			{shown_cnd = CND;        RedrawRegion(hConsoleWnd, 501, 174, 595, 197);}
	if ((wait_state|wait_lamp) != shown_wait)
			{shown_wait= (wait_state|wait_lamp); RedrawRegion(hConsoleWnd, 380,  77, 414,  97);}
	if (CES != shown_ces)
			{shown_ces = CES; 		 RepaintRegion(hConsoleWnd, TOGGLES_X-7, 230, TOGGLES_X+360, 275);}	/* console entry sw: do erase bkgnd */
	if (RUNMODE != shown_runmode)
			{shown_runmode = RUNMODE;RepaintRegion(hConsoleWnd, RUNSWITCH_X-50, RUNSWITCH_Y-50, RUNSWITCH_X+50, RUNSWITCH_Y+50);}

	int_lamps = 0;

	/* this loop works with lamp buttons that are calculated on-the-fly only */
	for (i = 0; i < NBUTTONS; i++) {
		if (btn[i].pushable)
			continue;

		switch (i) {
			case IDC_RUN:
				state = hFlashTimer || (running && ! wait_state);
				break;

/* this button is always off
			case IDC_PARITY_CHECK
*/

/* these buttons are enabled/disabled directly
			case IDC_POWER_ON:
			case IDC_FILE_READY:
			case IDC_FORMS_CHECK:
			case IDC_KEYBOARD_SELECT:
			case IDC_DISK_UNLOCK:
*/
			default:
				continue;
		}

		if (state != btn[i].state) {				/* state has changed */
			EnableWindow(btn[i].hBtn, state);
			btn[i].state = state;
		}
	}

	if (force) {									/* if force flag is set, update text region */
		SetRect(&xin, TXTBOX_X, TXTBOX_Y, TXTBOX_X+TXTBOX_WIDTH, TXTBOX_BOTTOM+2*TXTBOX_HEIGHT);
		InvalidateRect(hConsoleWnd, &xin, TRUE);
	}

	state = ((cr_unit.flags & UNIT_ATT) == 0) ? STATE_1442_EMPTY  :
			 (cr_unit.flags & UNIT_PHYSICAL)  ? STATE_1442_HIDDEN :
			 (cr_unit.flags & UNIT_CR_EMPTY)  ? STATE_1442_EOF    : 
			  cr_unit.pos 					  ? STATE_1442_MIDDLE :
			  								    STATE_1442_FULL;

	if (state != btn[IDC_1442].state) {
		if (state == STATE_1442_HIDDEN)
			ShowWindow(btn[IDC_1442].hBtn, SW_HIDE);
		else {
			if (btn[IDC_1442].state == STATE_1442_HIDDEN)
				ShowWindow(btn[IDC_1442].hBtn, SW_SHOWNA);

			SendMessage(btn[IDC_1442].hBtn, STM_SETIMAGE, IMAGE_BITMAP,
				(LPARAM) (
					(state == STATE_1442_FULL)   ? hbm1442_full   :
					(state == STATE_1442_MIDDLE) ? hbm1442_middle : 
					(state == STATE_1442_EOF)    ? hbm1442_eof    :
					hbm1442_empty));
		}

		btn[IDC_1442].state = state;
	}

	state = ((prt_unit.flags & UNIT_ATT) == 0)    ? STATE_1132_EMPTY  :
			 (prt_unit.flags & UNIT_PHYSICAL_PTR) ? STATE_1132_HIDDEN :
			  prt_unit.pos						  ? STATE_1132_FULL :
			  								        STATE_1132_EMPTY;

	if (state != btn[IDC_1132].state) {
		if (state == STATE_1132_HIDDEN)
			ShowWindow(btn[IDC_1132].hBtn, SW_HIDE);
		else {
			if (btn[IDC_1132].state == STATE_1132_HIDDEN)
				ShowWindow(btn[IDC_1132].hBtn, SW_SHOWNA);

			SendMessage(btn[IDC_1132].hBtn, STM_SETIMAGE, IMAGE_BITMAP,
				(LPARAM) (
					(state == STATE_1132_FULL) ? hbm1132_full : hbm1132_empty));
		}

		btn[IDC_1132].state = state;
	}

	in_here = FALSE;
}

WNDPROC oldButtonProc = NULL;

/* ------------------------------------------------------------------------ 
 * ------------------------------------------------------------------------ */

LRESULT CALLBACK ButtonProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int i;

	i = GetWindowLong(hWnd, GWL_ID);

	if (! btn[i].pushable) {
		if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_LBUTTONDBLCLK)
			return 0;

		if (uMsg == WM_CHAR)
			if ((TCHAR) wParam == ' ')
				return 0;
	}

   	return CallWindowProc(oldButtonProc, hWnd, uMsg, wParam, lParam);
}

/* ------------------------------------------------------------------------ 
 * ------------------------------------------------------------------------ */

static int occurs (char *txt, char ch)
{
	int count = 0;

	while (*txt)
		if (*txt++ == ch)
			count++;

	return count;
}

/* ------------------------------------------------------------------------ 
 * turns out to get properly colored buttons you have to paint them yourself. Sheesh.
 * On the plus side, this lets do a better job of aligning the button text than
 * the button would by itself.
 * ------------------------------------------------------------------------ */

void PaintButton (LPDRAWITEMSTRUCT dis)
{
	int i = dis->CtlID, nc, nlines, x, y, dy;
 	BOOL down = dis->itemState & ODS_SELECTED;
	HPEN hOldPen;
	HFONT hOldFont;
	UINT oldAlign;
	COLORREF oldBk;
	char *txt, *tstart;

	if (! BETWEEN(i, 0, NBUTTONS-1))
		return;

	if (! btn[i].subclassed)
		return;

	FillRect(dis->hDC, &dis->rcItem, ((btn[i].pushable || power) && IsWindowEnabled(btn[i].hBtn)) ? btn[i].hbrLit : btn[i].hbrDark);

	if (! btn[i].pushable) {
		hOldPen = SelectObject(dis->hDC, hBlackPen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.top, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.top);
	}
	else if (down) {
		/* do the three-D thing */
		hOldPen = SelectObject(dis->hDC, hDkGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-2, NULL);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.top);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hWhitePen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-1, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left+1,  dis->rcItem.bottom-3, NULL);
		LineTo(dis->hDC,   dis->rcItem.left+1,  dis->rcItem.top+1);
		LineTo(dis->hDC,   dis->rcItem.right-3, dis->rcItem.top+1);
	}
	else {
		hOldPen = SelectObject(dis->hDC, hWhitePen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-2, NULL);
		LineTo(dis->hDC,   dis->rcItem.left,    dis->rcItem.top);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hDkGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left,    dis->rcItem.bottom-1, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.bottom-1);
		LineTo(dis->hDC,   dis->rcItem.right-1, dis->rcItem.top);

		SelectObject(dis->hDC, hGreyPen);
		MoveToEx(dis->hDC, dis->rcItem.left+1,  dis->rcItem.bottom-2, NULL);
		LineTo(dis->hDC,   dis->rcItem.right-2, dis->rcItem.bottom-2);
		LineTo(dis->hDC,   dis->rcItem.right-2, dis->rcItem.top+1);
	}

	SelectObject(dis->hDC, hOldPen);

	hOldFont = SelectObject(dis->hDC, hBtnFont);
	oldAlign = SetTextAlign(dis->hDC, TA_CENTER|TA_TOP);
	oldBk    = SetBkMode(dis->hDC, TRANSPARENT);

	txt = btn[i].txt;
	nlines = occurs(txt, '\n')+1;
	x  = (dis->rcItem.left + dis->rcItem.right)  / 2;
	y  = (dis->rcItem.top  + dis->rcItem.bottom) / 2;

	dy = 14;
	y  = y - (nlines*dy)/2;

	if (down) {
		x += 1;
		y += 1;
	}

	for (;;) {
		for (nc = 0, tstart = txt; *txt && *txt != '\n'; txt++, nc++)
			;

		TextOut(dis->hDC, x, y, tstart, nc);

		if (*txt == '\0')
			break;

		txt++;
		y += dy;
	}

	SetTextAlign(dis->hDC, oldAlign);
	SetBkMode(dis->hDC,    oldBk);
	SelectObject(dis->hDC, hOldFont);
}
	
/* ------------------------------------------------------------------------ 
 * ------------------------------------------------------------------------ */

HWND CreateSubclassedButton (HWND hwParent, int i)
{
	HWND hBtn;
	int x, y;
	int r, g, b;

	y = bmht - (4*BUTTON_HEIGHT) + BUTTON_HEIGHT * btn[i].y;
	x = (btn[i].x < 2) ? (btn[i].x*BUTTON_WIDTH) : (598 - (4-btn[i].x)*BUTTON_WIDTH);

	if ((hBtn = CreateWindow("BUTTON", btn[i].txt, WS_CHILD|WS_VISIBLE|BS_CENTER|BS_MULTILINE|BS_OWNERDRAW,
			x, y, BUTTON_WIDTH, BUTTON_HEIGHT, hwParent, (HMENU) i, hInstance, NULL)) == NULL)
		return NULL;

	btn[i].hBtn = hBtn;

	if (oldButtonProc == NULL)
		oldButtonProc = (WNDPROC) GetWindowLong(hBtn, GWL_WNDPROC);

	btn[i].hbrLit = CreateSolidBrush(btn[i].clr);

	if (! btn[i].pushable) {
		r = GetRValue(btn[i].clr) / 4;
		g = GetGValue(btn[i].clr) / 4;
		b = GetBValue(btn[i].clr) / 4;

		btn[i].hbrDark = CreateSolidBrush(RGB(r,g,b));
		EnableWindow(hBtn, FALSE);
	}

	SetWindowLong(hBtn, GWL_WNDPROC, (LONG) ButtonProc);
	return hBtn;
}

/* ------------------------------------------------------------------------ 
 * Pump - thread that takes care of the console window. It has to be a separate thread so that it gets
 * execution time even when the simulator is compute-bound or IO-blocked. This routine creates the window
 * and runs a standard Windows message pump. The window function does the actual display work.
 * ------------------------------------------------------------------------ */

static DWORD WINAPI Pump (LPVOID arg)
{
	MSG msg;
	int wx, wy, i;
	RECT r, ra;
	BITMAP bm;
	WNDCLASS cd;
	HDC hDC;
	HWND hActWnd;

	hActWnd = GetForegroundWindow();

	if (! class_defined) {							/* register Window class */
		hInstance = GetModuleHandle(NULL);

		memset(&cd, 0, sizeof(cd));
		cd.style         = CS_NOCLOSE;
		cd.lpfnWndProc   = ConsoleWndProc;
		cd.cbClsExtra    = 0;
		cd.cbWndExtra    = 0;
		cd.hInstance     = hInstance;
		cd.hIcon         = NULL;
		cd.hCursor       = hcArrow;
		cd.hbrBackground = NULL;
		cd.lpszMenuName  = NULL;
		cd.lpszClassName = szConsoleClassName;

		if (! RegisterClass(&cd)) {
			PumpID = 0;
			return 0;
		}

		class_defined = TRUE;
	}

	hbWhite    = GetStockObject(WHITE_BRUSH);			/* create or fetch useful GDI objects */
	hbBlack    = GetStockObject(BLACK_BRUSH);			/* create or fetch useful GDI objects */
	hbGray     = GetStockObject(GRAY_BRUSH);
	hSwitchPen = CreatePen(PS_SOLID, 5, RGB(255,255,255));

	hWhitePen  = GetStockObject(WHITE_PEN);
	hBlackPen  = GetStockObject(BLACK_PEN);
	hLtGreyPen = CreatePen(PS_SOLID, 1, RGB(190,190,190));
	hGreyPen   = CreatePen(PS_SOLID, 1, RGB(128,128,128));
	hDkGreyPen = CreatePen(PS_SOLID, 1, RGB(64,64,64));

	hcArrow    = LoadCursor(NULL,      IDC_ARROW);
#ifdef IDC_HAND
	hcHand     = LoadCursor(NULL, IDC_HAND);							/* use stock object provided by Windows */
	if (hcHand == NULL)
		hcHand = LoadCursor(hInstance, MAKEINTRESOURCE(IDC_MYHAND));
#else
	hcHand     = LoadCursor(hInstance, MAKEINTRESOURCE(IDC_MYHAND));
#endif

	if (hBitmap   == NULL)
		hBitmap   = LoadBitmap(hInstance, MAKEINTRESOURCE(IDB_CONSOLE));
	if (hbLampOut == NULL)
		hbLampOut = CreateSolidBrush(RGB(50,50,50));
	if (hFont     == NULL)
		hFont     = CreateFont(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, FIXED_PITCH, FF_SWISS, "Arial");
	if (hBtnFont  == NULL)
		hBtnFont  = CreateFont(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, FIXED_PITCH, FF_SWISS, "Arial");
	if (hTinyFont == NULL)
		hTinyFont = CreateFont(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, FIXED_PITCH, FF_SWISS, "Arial");

	if (hConsoleWnd == NULL) {						/* create window */
		if ((hConsoleWnd = CreateWindow(szConsoleClassName, "IBM 1130", WS_OVERLAPPED|WS_CLIPCHILDREN, 0, 0, 200, 200, NULL, NULL, hInstance, NULL)) == NULL) {
			PumpID = 0;
			return 0;
		}

		DragAcceptFiles(hConsoleWnd, TRUE);			/* let it accept dragged files (scripts) */
	}

	GetObject(hBitmap, sizeof(bm), &bm);			/* get bitmap size */
	bmwid = bm.bmWidth;
	bmht  = bm.bmHeight;

	for (i = 0; i < NBUTTONS; i++) {
		if (! btn[i].subclassed)
			continue;

		CreateSubclassedButton(hConsoleWnd, i);
		if (! btn[i].pushable)
			EnableWindow(btn[i].hBtn, btn[i].state);
	}

/* This isn't needed anymore, now that we have the big printer icon -- it acts like a button now
 *	i = IDC_TEAR;
 *	btn[i].hBtn = CreateWindow("BUTTON", btn[i].txt, WS_CHILD|WS_VISIBLE|BS_CENTER,
 *			btn[i].x, btn[i].y, btn[i].wx, btn[i].wy, hConsoleWnd, (HMENU) i, hInstance, NULL);
 *
 *	SendMessage(btn[i].hBtn, WM_SETFONT, (WPARAM) hTinyFont, TRUE);
 */

	hbm1442_full   = LoadBitmap(hInstance, "FULL_1442");
	hbm1442_empty  = LoadBitmap(hInstance, "EMPTY_1442");
	hbm1442_eof    = LoadBitmap(hInstance, "EOF_1442");
	hbm1442_middle = LoadBitmap(hInstance, "MIDDLE_1442");
	hbm1132_full   = LoadBitmap(hInstance, "FULL_1132");
	hbm1132_empty  = LoadBitmap(hInstance, "EMPTY_1132");

	i = IDC_1442;

	btn[i].hBtn = CreateWindow("STATIC", btn[i].txt, WS_CHILD|WS_VISIBLE|SS_BITMAP|SS_SUNKEN|WS_BORDER|SS_REALSIZEIMAGE|SS_NOTIFY,
			btn[i].x, btn[i].y, btn[i].wx, btn[i].wy, hConsoleWnd, (HMENU) i, hInstance, NULL);
	btn[i].state = STATE_1442_EMPTY;

	wx = SendMessage(btn[i].hBtn, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM) hbm1442_empty);

	i = IDC_1132;

	btn[i].hBtn = CreateWindow("STATIC", btn[i].txt, WS_CHILD|WS_VISIBLE|SS_BITMAP|SS_SUNKEN|WS_BORDER|SS_REALSIZEIMAGE|SS_NOTIFY,
			btn[i].x, btn[i].y, btn[i].wx, btn[i].wy, hConsoleWnd, (HMENU) i, hInstance, NULL);
	btn[i].state = FALSE;

	wx = SendMessage(btn[i].hBtn, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM) hbm1132_empty);

	GetWindowRect(hConsoleWnd, &r);					/* get window size as created */
	wx = r.right  - r.left + 1;
	wy = r.bottom - r.top  + 1;

	if (hCDC == NULL) {								/* get a memory DC and select the bitmap into ti */
		hDC = GetDC(hConsoleWnd);
		hCDC = CreateCompatibleDC(hDC);
		SelectObject(hCDC, hBitmap);
		ReleaseDC(hConsoleWnd, hDC);
	}

	GetClientRect(hConsoleWnd, &r);
	wx = (wx - r.right  - 1) + bmwid;				/* compute new desired size based on how client area came out */
	wy = (wy - r.bottom - 1) + bmht;
	MoveWindow(hConsoleWnd, 0, 0, wx, wy, FALSE);	/* resize window */

	ShowWindow(hConsoleWnd, SW_SHOWNOACTIVATE);		/* display it */
	UpdateWindow(hConsoleWnd);

	if (hActWnd != NULL) {							/* bring console (sim) window back to top */
		GetWindowRect(hConsoleWnd, &r);
		ShowWindow(hActWnd, SW_NORMAL);				/* and move it just below the display window */
		SetWindowPos(hActWnd, HWND_TOP, 0, r.bottom, 0, 0, SWP_NOSIZE);
		GetWindowRect(hActWnd, &ra);
		if (ra.bottom >= GetSystemMetrics(SM_CYSCREEN)) {	/* resize if it goes of bottom of screen */
			ra.bottom = GetSystemMetrics(SM_CYSCREEN) - 1;
			SetWindowPos(hActWnd, 0, 0, 0, ra.right-ra.left+1, ra.bottom-ra.top+1, SWP_NOZORDER|SWP_NOMOVE);
		}
	}

	if (running)									/* if simulator is already running, start update timer */
		gui_run(TRUE);

	while (GetMessage(&msg, hConsoleWnd, 0, 0)) {	/* message pump - this basically loops forevermore */
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (hConsoleWnd != NULL) { 
		DragAcceptFiles(hConsoleWnd, FALSE);		/* unregister as drag/drop target */
		DestroyWindow(hConsoleWnd);					/* but if a quit message got posted, clean up */
		hConsoleWnd = NULL;
	}

	PumpID = 0;
	return 0;
}

/* ------------------------------------------------------------------------ 
 * DrawBits - starting at position (x,y), draw lamps for nbits bits of word 'bits',	looking only at masked bits
 * ------------------------------------------------------------------------ */

static void DrawBits (HDC hDC, int x, int y, int bits, int nbits, int mask, char *syms)
{
	int i, b = 0x0001 << (nbits-1);

	for (i = 0; i < nbits; i++, b >>= 1) {
		if (mask & b) {								/* select white or black lettering then write 2 chars */
			SetTextColor(hDC, (b & bits && power) ? RGB(255,255,255) : RGB(0,0,0));
			TextOut(hDC, x, y, syms, 2);
		}
		syms += 2;									/* go to next symbol pair */

		if (i < 10)
			x += 15;								/* step between lamps */
		else
			x += 19;

		if (x < 500) {
			if (b & 0x1110)
				x += 10;							/* step over nibble divisions on left side */
			else if (b & 0x0001)
				x += 9;
		}
	}
}

/* ------------------------------------------------------------------------ 
 * DrawToggles - display the console sense switches
 * ------------------------------------------------------------------------ */


static void DrawToggles (HDC hDC, int bits)
{
	int b, x;

	for (b = 0x8000, x = TOGGLES_X; b != 0; b >>= 1) {
		if (shown_ces & b) {			/* up */
			SelectObject(hDC, hbWhite);
			Rectangle(hDC, x, 232, x+9, 240);
			SelectObject(hDC, hbGray);
			Rectangle(hDC, x, 239, x+9, 255);
 		}
		else {							/* down */
			SelectObject(hDC, hbWhite);
			Rectangle(hDC, x, 263, x+9, 271);
			SelectObject(hDC, hbGray);
			Rectangle(hDC, x, 248, x+9, 264);
		}

		x += (b & 0x1111) ? 31 : 21;
	}
}

/* ------------------------------------------------------------------------ 
 * DrawRunmode - draw the run mode rotary switch's little tip
 * ------------------------------------------------------------------------ */

void DrawRunmode (HDC hDC, int mode)
{
	double angle = (mode*45. + 90.) * 3.1415926 / 180.;		/* convert mode position to angle in radians */
	double ca, sa;											/* sine and cosine */
	int x0, y0, x1, y1;
	HPEN hOldPen;

	ca = cos(angle);
	sa = sin(angle);

	x0 = RUNSWITCH_X + (int) (20.*ca + 0.5);		/* inner radius */
	y0 = RUNSWITCH_Y - (int) (20.*sa + 0.5);
	x1 = RUNSWITCH_X + (int) (25.*ca + 0.5);		/* outer radius */
	y1 = RUNSWITCH_Y - (int) (25.*sa + 0.5);

	hOldPen = SelectObject(hDC, hSwitchPen);

	MoveToEx(hDC, x0, y0, NULL);
	LineTo(hDC, x1, y1);

	SelectObject(hDC, hOldPen);
}

/* ------------------------------------------------------------------------ 
 * HandleClick - handle mouse clicks on the console window. Now we just 
 * look at the console sense switches.  Actual says this is a real click, rather
 * than a mouse-region test.  Return value TRUE means the cursor is over a hotspot.
 * ------------------------------------------------------------------------ */

static BOOL HandleClick (HWND hWnd, int xh, int yh, BOOL actual, BOOL rightclick)
{
	int b, x, r, ang, i;

	for (b = 0x8000, x = TOGGLES_X; b != 0; b >>= 1) {
		if (BETWEEN(xh, x-3, x+8+3) && BETWEEN(yh, 230, 275)) {
			if (actual) {
				CES ^= b;						/* a hit. Invert the bit and redisplay */
				update_gui(TRUE);
			}
			return TRUE;
		}
		x += (b & 0x1111) ? 31 : 21;
	}

	if (BETWEEN(xh, RUNSWITCH_X-50, RUNSWITCH_X+50) && BETWEEN(yh, RUNSWITCH_Y-50, RUNSWITCH_Y+50)) {		/* hit near rotary switch */
		ang = (int) (atan2(RUNSWITCH_X-xh, RUNSWITCH_Y-yh)*180./3.1415926);	/* this does implicit 90 deg rotation by the way */
		r = (int) sqrt((xh-RUNSWITCH_X)*(xh-RUNSWITCH_X)+(yh-RUNSWITCH_Y)*(yh-RUNSWITCH_Y));
		if (r > 12) {
			for (i = MODE_LOAD; i <= MODE_INT_RUN; i++) {
				if (BETWEEN(ang, i*45-12, i*45+12)) {
					if (actual) {
						RUNMODE = i;
						update_gui(TRUE);
					}
					return TRUE;
				}
			}
			
		}
	}

	return FALSE;
}

/* ------------------------------------------------------------------------ 
 * DrawConsole - refresh the console display. (This routine could be sped up by intersecting
 * the various components' bounding rectangles with the repaint rectangle.  The bounding rects
 * could be put into an array and used both here and in the refresh routine).
 *
 * RedrawRegion -> force repaint w/o background redraw. used for lamps which are drawn in the same place in either state
 * RepaintRegion-> repaint with background redraw. Used for toggles which change position.
 * ------------------------------------------------------------------------ */

static void DrawConsole (HDC hDC, PAINTSTRUCT *ps)
{
	static char digits[] = " 0 1 2 3 4 5 6 7 8 9101112131415";
	static char cccs[]   = "3216 8 4 2 1";
	static char cnds[]   = " C V";
	static char waits[]  = " W";
	HFONT hOldFont, hOldBrush;
	RECT xout, xin;
	int i, n;
	DEVICE *dptr;
	UNIT *uptr;
	t_bool enab;
	char nametemp[50], *dispname;

	hOldFont  = SelectObject(hDC, hFont);			/* use that tiny font */
	hOldBrush = SelectObject(hDC, hbWhite);

	SetBkMode(hDC, TRANSPARENT);					/* overlay letters w/o changing background */

	DrawBits(hDC,  76,  15, shown_iar,    16, mem_mask, digits);	/* register holds only 15 bits */
	DrawBits(hDC,  76, 	48, shown_sar,    16, mem_mask, digits);	/* but let's display only used bits */
	DrawBits(hDC,  76,  81, shown_sbr,    16, 0xFFFF, digits);
	DrawBits(hDC,  76, 114, shown_arf,    16, 0xFFFF, digits);
	DrawBits(hDC,  76, 147, shown_acc,    16, 0xFFFF, digits);
	DrawBits(hDC,  76, 180, shown_ext,    16, 0xFFFF, digits);

	DrawBits(hDC, 506,  15, shown_op,      5, 0x001F, digits);
	DrawBits(hDC, 506,  81, shown_tag,     4, 0x0007, digits);
	DrawBits(hDC, 506, 114, shown_irq,     6, 0x003F, digits);
	DrawBits(hDC, 506, 147, shown_ccc,     6, 0x003F, cccs);
	DrawBits(hDC, 506, 180, shown_cnd,     2, 0x0003, cnds);

	DrawBits(hDC, 390,  81, shown_wait?1:0,1, 0x0001, waits);

	DrawToggles(hDC, shown_ces);

	DrawRunmode(hDC, shown_runmode);

	SelectObject(hDC, hOldFont);
	SelectObject(hDC, hOldBrush);

	SetBkColor(hDC, RGB(0,0,0));

	SetRect(&xin, TXTBOX_X, TXTBOX_Y, TXTBOX_X+TXTBOX_WIDTH, TXTBOX_BOTTOM+TXTBOX_HEIGHT);
	if (IntersectRect(&xout, &xin, &ps->rcPaint)) {
		hOldFont = SelectObject(hDC, hTinyFont);

		for (i = 0; i < NTXTBOXES; i++) {
			enab = FALSE;

			dptr = find_unit(txtbox[i].unitname, &uptr);
			if (dptr != NULL && uptr != NULL) {
				if (uptr->flags & UNIT_DIS) {
					SetTextColor(hDC, RGB(128,0,0));
				}
				else if (uptr->flags & UNIT_ATT) {
					SetTextColor(hDC, RGB(0,0,255));
					if ((n = strlen(uptr->filename)) > 30) {
						strcpy(nametemp, "...");
						strcpy(nametemp+3, uptr->filename+n-30);
						dispname = nametemp;						
					}
					else
						dispname = uptr->filename;

					TextOut(hDC, txtbox[i].x+25, txtbox[i].y+TXTBOX_HEIGHT, dispname, strlen(dispname));
					SetTextColor(hDC, RGB(255,255,255));
					enab = TRUE;
				}
				else {
					SetTextColor(hDC, RGB(128,128,128));
				}
				TextOut(hDC, txtbox[i].x, txtbox[i].y, txtbox[i].txt, strlen(txtbox[i].txt));
			}

			if (txtbox[i].idctrl >= 0)
				EnableWindow(btn[txtbox[i].idctrl].hBtn, enab);
		}

		SelectObject(hDC, hOldFont);
	}
}

/* ------------------------------------------------------------------------ 
 * Handles button presses. Remember that this occurs in the context of 
 * the Pump thread, not the simulator thread.
 * ------------------------------------------------------------------------ */

void flash_run (void)              
{
	EnableWindow(btn[IDC_RUN].hBtn, TRUE);		/* enable the run lamp */

	if (hFlashTimer != 0)
		KillTimer(hConsoleWnd, FLASH_TIMER_ID);	/* (re)schedule lamp update */

	hFlashTimer = SetTimer(hConsoleWnd, FLASH_TIMER_ID, LAMPTIME, NULL);
}

void gui_run (int running)
{
	if (running && hUpdateTimer == 0 && hConsoleWnd != NULL) {
		hUpdateTimer = SetTimer(hConsoleWnd, UPDATE_TIMER_ID, 1000/UPDATE_INTERVAL, NULL);
	}
	else if (hUpdateTimer != 0 && ! running) {
		KillTimer(hConsoleWnd, UPDATE_TIMER_ID);
		hUpdateTimer = 0;
	}
	flash_run();								/* keep run lamp active for a while after we stop running */
} 

void HandleCommand (HWND hWnd, WORD wNotify, WORD idCtl, HWND hwCtl)
{
	int i;

	switch (idCtl) {
		case IDC_POWER:						/* toggle system power */
			power = ! power;
			reset_all(0);
			if (running && ! power) {		/* turning off */
				reason = STOP_POWER_OFF;
				/* wait for execution thread to exit */
/* this prevents message pump from running, which unfortunately locks up
 * the emulator thread when it calls gui_run(FALSE) which calls EnableWindow on the Run lamp
 *				while (running)
 *					Sleep(10);
 */				
			}

			btn[IDC_POWER_ON].state = power;
			EnableWindow(btn[IDC_POWER_ON].hBtn, power);

			for (i = 0; i < NBUTTONS; i++)	/* repaint all of the lamps */
				if (! btn[i].pushable)
					InvalidateRect(btn[i].hBtn, NULL, TRUE);

			break;

		case IDC_PROGRAM_START:				/* begin execution */
			if (! running) {
				switch (RUNMODE) {
					case MODE_INT_RUN:
					case MODE_RUN:
					case MODE_SI:
						stuff_cmd("cont");
						break;

					case MODE_DISP:			/* display core and advance IAR */
						ReadW(IAR);
						IAR = IAR+1;
						flash_run();		/* illuminate run lamp for .5 sec */
						break;

					case MODE_LOAD:			/* store to core and advance IAR */
						WriteW(IAR, CES);
						IAR = IAR+1;
						flash_run();
						break;
				}
			}
			break;

		case IDC_PROGRAM_STOP:
			if (running) {					/* potential race condition here */
				GUI_BEGIN_CRITICAL_SECTION
				SETBIT(con_dsw, CPU_DSW_PROGRAM_STOP);
				SETBIT(ILSW[5], ILSW_5_INT_RUN_PROGRAM_STOP);
				int_req   |= INT_REQ_5;		/* note: calc_ints() is not needed in this case */
				int_lamps |= INT_REQ_5;
				GUI_END_CRITICAL_SECTION
			}
			break;

		case IDC_LOAD_IAR:
			if (! running) {
				IAR = CES & mem_mask;		/* set IAR from console entry switches */
			}
			break;

		case IDC_KEYBOARD:					/* toggle between console/keyboard mode */
			break;

		case IDC_IMM_STOP:
			if (running) {
				reason = STOP_IMMEDIATE;	/* terminate execution without setting wait_mode */
				/* wait for execution thread to exit */
/* this prevents message pump from running, which unfortunately locks up
 * the emulator thread when it calls gui_run(FALSE) which calls EnableWindow on the Run lamp
 *				while (running)
 *					Sleep(10);				
 */
			}
			break;

		case IDC_RESET:
			if (! running) {				/* check-reset is disabled while running */
				reset_all(0);
				forms_check(0);				/* clear forms-check status */
				print_check(0);
			}
			break;

		case IDC_PROGRAM_LOAD:
			if (! running) {				/* if card reader is attached to a file, do cold start read of one card */
				IAR = 0;					/* reset IAR */
#ifdef PROGRAM_LOAD_STARTS_CPU
				stuff_cmd("boot cr");
#else
				if (cr_boot(0, NULL) != SCPE_OK)	/* load boot card */
					remark_cmd("IPL failed");
#endif
			}
			break;

		case IDC_TEAR:						/* "tear off printer output" */
		case IDC_1132:						/* do same if they click on the printer icon */
			if (btn[IDC_1132].state && (wNotify == STN_CLICKED || wNotify == STN_DBLCLK))
				tear_printer();
			break;

		case IDC_1442:
			if (btn[IDC_1442].state == STATE_1442_FULL || wNotify == STN_DBLCLK)
				stuff_cmd("detach cr");
			else if (btn[IDC_1442].state != STATE_1442_EMPTY && wNotify == STN_CLICKED) {
				cr_rewind();
				update_gui(TRUE);
			}
			break;
	}
	
	update_gui(FALSE);
}

/* ------------------------------------------------------------------------ 
 * ConsoleWndProc - window process for the console display
 * ------------------------------------------------------------------------ */

LRESULT CALLBACK ConsoleWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	PAINTSTRUCT ps;
	POINT p;
	RECT clip, xsect, rbmp;
	int i;

	switch (uMsg) {
		case WM_CLOSE:
			DestroyWindow(hWnd);
			break;

		case WM_DESTROY:
			gui_run(FALSE);
			hConsoleWnd = NULL;
			break;

		case WM_ERASEBKGND:
			hDC = (HDC) wParam;
			GetClipBox(hDC, &clip);
			SetRect(&rbmp, 0, 0, bmwid, bmht);
			if (IntersectRect(&xsect, &clip, &rbmp))
				BitBlt(hDC, xsect.left, xsect.top, xsect.right-xsect.left+1, xsect.bottom-xsect.top+1, hCDC, xsect.left, xsect.top, SRCCOPY);
			return TRUE;			/* let Paint do this so we know what the update region is (ps.rcPaint) */

		case WM_PAINT:
			hDC = BeginPaint(hWnd, &ps);
			DrawConsole(hDC, &ps);
			EndPaint(hWnd, &ps);
			break;

		case WM_COMMAND:							/* button click */
			HandleCommand(hWnd, HIWORD(wParam), LOWORD(wParam), (HWND) lParam);
			break;

		case WM_CTLCOLOREDIT:						/* text color for edit controls */
			SetBkColor((HDC) wParam, RGB(0,0,0));
			SetTextColor((HDC) wParam, RGB(255,255,255));
			break;

		case WM_DRAWITEM:
			PaintButton((LPDRAWITEMSTRUCT) lParam);
			break;

		case WM_SETCURSOR:
			GetCursorPos(&p);
			ScreenToClient(hWnd, &p);
			SetCursor(HandleClick(hWnd, p.x, p.y, FALSE, FALSE) ? hcHand : hcArrow);
			return TRUE;

		case WM_LBUTTONDOWN:
			HandleClick(hWnd, LOWORD(lParam), HIWORD(lParam), TRUE, FALSE);
			break;

		case WM_RBUTTONDOWN:
			HandleClick(hWnd, LOWORD(lParam), HIWORD(lParam), TRUE, TRUE);
			break;

		case WM_CTLCOLORBTN:
			i = GetWindowLong((HWND) lParam, GWL_ID);
			if (BETWEEN(i, 0, NBUTTONS-1))
				return (LRESULT) (power && IsWindowEnabled((HWND) lParam) ? btn[i].hbrLit : btn[i].hbrDark);

		case WM_TIMER:
			if (wParam == FLASH_TIMER_ID && hFlashTimer != 0) {
				KillTimer(hWnd, FLASH_TIMER_ID);
				hFlashTimer = 0;
			}
			update_gui(FALSE);
			break;

		case WM_DROPFILES:
			accept_dropped_file((HANDLE) wParam);		/* console window - dragged file is a script or card deck */
			break;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}

enum {PRINTER_OK = 0, FORMS_CHECK = 1, PRINT_CHECK = 2, BOTH_CHECK = 3} printerstatus = PRINTER_OK;

void forms_check (int set)
{
	COLORREF oldcolor = btn[IDC_FORMS_CHECK].clr;

	if (set)
		SETBIT(printerstatus, FORMS_CHECK);
	else
		CLRBIT(printerstatus, FORMS_CHECK);

	btn[IDC_FORMS_CHECK].clr = (printerstatus & PRINT_CHECK) ? RGB(255,0,0) : RGB(255,255,0);

	btn[IDC_FORMS_CHECK].state = printerstatus;

	if (btn[IDC_FORMS_CHECK].hBtn != NULL) {
		EnableWindow(btn[IDC_FORMS_CHECK].hBtn, printerstatus);

		if (btn[IDC_FORMS_CHECK].clr != oldcolor)
			InvalidateRect(btn[IDC_FORMS_CHECK].hBtn, NULL, TRUE);		/* change color in any case */
	}
}

void print_check (int set)
{
	COLORREF oldcolor = btn[IDC_FORMS_CHECK].clr;

	if (set)
		SETBIT(printerstatus, PRINT_CHECK);
	else
		CLRBIT(printerstatus, PRINT_CHECK);

	btn[IDC_FORMS_CHECK].clr = (printerstatus & PRINT_CHECK) ? RGB(255,0,0) : RGB(255,255,0);

	btn[IDC_FORMS_CHECK].state = printerstatus;

	if (btn[IDC_FORMS_CHECK].hBtn != NULL) {
		EnableWindow(btn[IDC_FORMS_CHECK].hBtn, printerstatus);

		if (btn[IDC_FORMS_CHECK].clr != oldcolor)
			InvalidateRect(btn[IDC_FORMS_CHECK].hBtn, NULL, TRUE);		/* change color in any case */
	}
}

void keyboard_selected (int select)
{
	btn[IDC_KEYBOARD_SELECT].state = select;

	if (btn[IDC_KEYBOARD_SELECT].hBtn != NULL)
		EnableWindow(btn[IDC_KEYBOARD_SELECT].hBtn, select);
}

void disk_ready (int ready)
{
	btn[IDC_FILE_READY].state = ready;

	if (btn[IDC_FILE_READY].hBtn != NULL)
		EnableWindow(btn[IDC_FILE_READY].hBtn, ready);
}

void disk_unlocked (int unlocked)
{
	btn[IDC_DISK_UNLOCK].state = unlocked;

	if (btn[IDC_DISK_UNLOCK].hBtn != NULL)
		EnableWindow(btn[IDC_DISK_UNLOCK].hBtn, unlocked);
}

static void accept_dropped_file (HANDLE hDrop)
{
	int nfiles;
	char fname[MAX_PATH], cmd[MAX_PATH+50], *deckfile;
	BOOL cardreader;
	POINT pt;
	HWND hWndDrop;

	nfiles = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);		/* get file count, */
	DragQueryFile(hDrop, 0, fname, sizeof(fname));			/* get first filename */
	DragQueryPoint(hDrop, &pt);								/* get location of drop */
	DragFinish(hDrop);

	if (nfiles <= 0)							/* hmm, this seems unlikely to occur, but better check */
		return;

	if (running) {								/* can only accept a drop while processor is stopped */
		MessageBeep(0);
		return;
	}

	if ((hWndDrop = ChildWindowFromPoint(hConsoleWnd, pt)) == btn[IDC_1442].hBtn)
		cardreader = TRUE;									/* file was dropped onto 1442 card reader */
	else if (hWndDrop == NULL || hWndDrop == hConsoleWnd)
		cardreader = FALSE;									/* file was dropped onto console window, not a button */
	else {
		MessageBeep(0);										/* file was dropped onto another button */
		return;
	}

	if (nfiles > 1) {							/* oops, we wouldn't know what order to read them in */
		MessageBox(hConsoleWnd, "You may only drop one file at a time", "", MB_OK);
		return;
	}

												/* if shift key is down, prepend @ to name (make it a deck file) */
	deckfile = ((GetKeyState(VK_SHIFT) & 0x8000) && cardreader) ? "@" : "";

	sprintf(cmd, "%s \"%s%s\"", cardreader ? "attach cr" : "do", deckfile, fname);
	stuff_cmd(cmd);
}

static void tear_printer (void)
{
	char cmd[MAX_PATH+100], filename[MAX_PATH];

	if ((prt_unit.flags & UNIT_ATT) == 0)
		return;

	strcpy(filename, prt_unit.filename);				/* save current attached filename */

	if (! stuff_and_wait("detach prt", 1000, 0))		/* detach it */
		return;

	sprintf(cmd, "view \"%s\"", filename);				/* spawn notepad to view it */
	if (! stuff_and_wait(cmd, 3000, 500))
		return;

	remove(filename);									/* delete the file */

	sprintf(cmd, "attach prt \"%s\"", filename);		/* reattach */
	stuff_cmd(cmd);
}

#ifdef XXX
	if ((hBtn = CreateWindow("BUTTON", btn[i].txt, WS_CHILD|WS_VISIBLE|BS_CENTER|BS_MULTILINE|BS_OWNERDRAW,
			x, y, BUTTON_WIDTH, BUTTON_HEIGHT, hwParent, (HMENU) i, hInstance, NULL)) == NULL)
		return NULL;

#endif

CRITICAL_SECTION critsect;

void begin_critical_section (void)
{
	static BOOL mustinit = TRUE;

	if (mustinit) {
		InitializeCriticalSection(&critsect);
		mustinit = FALSE;
	}

	EnterCriticalSection(&critsect);
}

void end_critical_section (void)
{
	LeaveCriticalSection(&critsect);
}

#ifndef MIN
#  define MIN(a,b) (((a) <= (b)) ? (a) : (b))
#endif

/* win32 - use a separate thread to read command lines so the GUI
 * can insert commands as well */

static HANDLE hCmdThread     = NULL;
static DWORD  iCmdThreadID   = 0;
static HANDLE hCmdReadEvent  = NULL;
static HANDLE hCmdReadyEvent = NULL;
static BOOL   scp_stuffed = FALSE, scp_reading = FALSE;
static char   cmdbuffer[256];

/* CmdThread - separate thread to read commands from stdin upon request */

static DWORD WINAPI CmdThread (LPVOID arg)
{
	for (;;) {
		WaitForSingleObject(hCmdReadEvent, INFINITE);		/* wait for request */
		read_line(cmdbuffer, sizeof(cmdbuffer), stdin);		/* read one line */
		scp_stuffed = FALSE;								/* say how we got it */
		scp_reading = FALSE;
		SetEvent(hCmdReadyEvent);							/* notify main thread a line is ready */
	}
	return 0;
}									

char *read_cmdline (char *ptr, int size, FILE *stream)
{
	char *cptr;

	if (hCmdThread == NULL)	{								/* set up command-reading thread */
		if ((hCmdReadEvent  = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL)
			scp_panic("Can't create command line read event");

		if ((hCmdReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL)
			scp_panic("Can't create command line ready event");
																/* start up the command thread */
		if ((hCmdThread = CreateThread(NULL, 0, CmdThread, NULL, 0, &iCmdThreadID)) == NULL)
			scp_panic("Unable to create command line reading thread");
	}

	scp_reading = TRUE;

	SetEvent(hCmdReadEvent);								/* let read thread get one line */
	WaitForSingleObject(hCmdReadyEvent, INFINITE);			/* wait for read thread or GUI to respond */
	strncpy(ptr, cmdbuffer, MIN(size, sizeof(cmdbuffer)));	/* copy line to caller's buffer */

	for (cptr = ptr; isspace(*cptr); cptr++)				/* absorb spaces */
		;

	if (scp_stuffed) {										/* stuffed command needs to be echoed */
		sim_printf("%s\n", cptr);
	}

	return cptr;
}

/* stuff_cmd - force a command into the read_cmdline output buffer. Called asynchronously by GUI */

void stuff_cmd (char *cmd)
{
	strcpy(cmdbuffer, cmd);									/* save the string */
	scp_stuffed = TRUE;										/* note where it came from */
	scp_reading = FALSE;
	ResetEvent(hCmdReadEvent);								/* clear read request event */
	SetEvent(hCmdReadyEvent);								/* notify main thread a line is ready */
}

/* my_yield - process GUI messages. It's not apparent why stuff_and_wait would block,
 * since it sleeps in the GUI thread while scp runs in the main thread. However,
 * at the end of every command scp calls update_gui, which can result in messages
 * being sent to the GUI thread. So, the GUI thread has to process messages while
 * stuff_and_wait is waiting.
 */
static void my_yield (void)
{
	MSG msg;
					/* multitask */
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
    }
}

/* stuff_and_wait -- stuff a command and wait for the emulator to process the command
 * and come back to prompt for another
 */

t_bool stuff_and_wait (char *cmd, int timeout, int delay)
{
	scp_reading = FALSE;

	stuff_cmd(cmd);

	while (! scp_reading) {
		if (timeout < 0)
			return FALSE;

		my_yield();
		if (scp_reading)
			break;

		Sleep(50);
		if (timeout)
			if ((timeout -= 50) <= 0)
				timeout = -1;

		my_yield();
	}

	if (delay)
		Sleep(delay);

	return TRUE;
}

/* remark_cmd - print a remark from inside a command processor. This routine takes
 * into account the possiblity that the command might have been stuffed, in which
 * case the sim> prompt needs to be reprinted.
 */

void remark_cmd (char *remark)
{
	if (scp_reading) {
		putchar('\n');
		if (sim_log) putc('\n', sim_log);
	}

	sim_printf("%s\n", remark);

	if (scp_reading)
		sim_printf("%s", sim_prompt);
}

#endif 		/* _WIN32 defined */
#endif		/* GUI_SUPPORT defined */
