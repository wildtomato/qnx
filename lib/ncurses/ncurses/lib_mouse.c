/*
 * $QNXtpLicenseC:
 * Copyright 2007, QNX Software Systems. All Rights Reserved.
 * 
 * You must obtain a written license from and pay applicable license fees to QNX 
 * Software Systems before you may reproduce, modify or distribute this software, 
 * or any work that includes all or part of this software.   Free development 
 * licenses are available for evaluation and non-commercial purposes.  For more 
 * information visit http://licensing.qnx.com or email licensing@qnx.com.
 *  
 * This file may contain contributions from others.  Please review this entire 
 * file for other proprietary rights or license notices, as well as the QNX 
 * Development Suite License Guide at http://licensing.qnx.com/license-guide/ 
 * for other information.
 * $
 */




/****************************************************************************
 * Copyright (c) 1998 Free Software Foundation, Inc.                        *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 * This module is intended to encapsulate ncurses's interface to pointing
 * devices.
 *
 * The first method used is xterm's internal mouse-tracking facility.
 * The second (not yet implemented) will be Alessandro Rubini's GPM server.
 *
 * Notes for implementors of new mouse-interface methods:
 *
 * The code is logically split into a lower level that accepts event reports
 * in a device-dependent format and an upper level that parses mouse gestures
 * and filters events.  The mediating data structure is a circular queue of
 * MEVENT structures.
 *
 * Functionally, the lower level's job is to pick up primitive events and
 * put them on the circular queue.  This can happen in one of two ways:
 * either (a) _nc_mouse_event() detects a series of incoming mouse reports
 * and queues them, or (b) code in lib_getch.c detects the kmous prefix in
 * the keyboard input stream and calls _nc_mouse_inline to queue up a series
 * of adjacent mouse reports.
 *
 * In either case, _nc_mouse_parse() should be called after the series is
 * accepted to parse the digested mouse reports (low-level MEVENTs) into
 * a gesture (a high-level or composite MEVENT).
 *
 * Don't be too shy about adding new event types or modifiers, if you can find
 * room for them in the 32-bit mask.  The API is written so that users get
 * feedback on which theoretical event types they won't see when they call
 * mousemask. There's one bit per button (the RESERVED_EVENT bit) not being
 * used yet, and a couple of bits open at the high end.
 */

#include <curses.priv.h>
#include <term.h>

#if	defined(__QNX__) && !defined(__QNXNTO__)
#include <sys/dev.h>
#include <sys/proxy.h>
#endif
#if USE_GPM_SUPPORT
#ifndef LINT		/* don't need this for llib-lncurses */
#undef buttons		/* term.h defines this, and gpm uses it! */
#include <gpm.h>
#endif
#endif

MODULE_ID("$Id: lib_mouse.c 153052 2007-11-02 21:10:56Z coreos $")

#define MY_TRACE TRACE_ICALLS|TRACE_IEVENT

#define INVALID_EVENT	-1

static int		mousetype;
#define M_XTERM		-1	/* use xterm's mouse tracking? */
#define M_NONE		0	/* no mouse device */
#define M_GPM		1	/* use GPM */
#define M_QNX		2	/* QNX mouse on console */
#define M_QNX_TERM	3	/* QNX mouse on pterm/xterm (using qansi-m) */

#if USE_GPM_SUPPORT
#ifndef LINT
static Gpm_Connect gpm_connect;
#endif
#endif

static mmask_t	eventmask;		/* current event mask */

static bool _nc_mouse_parse(int);
static void _nc_mouse_resume(SCREEN *);
static void _nc_mouse_wrap(SCREEN *);

/* maintain a circular list of mouse events */

/* The definition of the circular list size (EV_MAX), is in curses.priv.h, so
 * wgetch() may refer to the size and call _nc_mouse_parse() before circular
 * list overflow.
 */
static MEVENT	events[EV_MAX];		/* hold the last mouse event seen */
static MEVENT	*eventp = events;	/* next free slot in event queue */
#define NEXT(ep)	((ep == events + EV_MAX - 1) ? events : ep + 1)
#define PREV(ep)	((ep == events) ? events + EV_MAX - 1 : ep - 1)

#if	defined(__QNX__) && !defined(__QNXNTO__)
#undef	buttons
#include	<sys/mouse.h>
#include	<sys/kernel.h>
#include	<sys/console.h>
#include	<fcntl.h>
#include	<errno.h>

#define	MAX_KEY_MOUSE_LENGTH	14
#endif

#ifdef TRACE
static void _trace_slot(const char *tag)
{
	MEVENT *ep;

	_tracef(tag);

	for (ep = events; ep < events + EV_MAX; ep++)
		_tracef("mouse event queue slot %d = %s", ep-events, _tracemouse(ep));
}
#endif

/* FIXME: The list of names should be configurable */
static int is_xterm(const char *name)
{
    return (!strncmp(name, "xterm", 5)
      ||    !strncmp(name, "rxvt",  4)
      ||    !strncmp(name, "kterm", 5)
      ||    !strncmp(name, "color_xterm", 11));
}

#if	defined(__QNX__) && !defined(__QNXNTO__)
static int is_qnxterm(const char *name)
{
	return (!strncmp(name, "qansi", 5)
	  ||	!strncmp(name, "qnx", 3));
}
#endif

static void _nc_mouse_init(void)
/* initialize the mouse */
{
    int i;
    static int initialized;

    if (initialized) {
		return;
    }
    initialized = TRUE;

    TR(MY_TRACE, ("_nc_mouse_init() called"));

    for (i = 0; i < EV_MAX; i++)
		events[i].id = INVALID_EVENT;

#if defined(__QNX__) && !defined(__QNXNTO__)
    if ((key_mouse != 0 || change_res_horz != 0) && is_xterm(cur_term->type.term_names)) 
    {
		mousetype = M_XTERM;
		if (key_mouse == 0) {
			key_mouse = change_res_horz;
			_nc_add_to_try(&(SP->_key_ok), key_mouse, KEY_MOUSE);
		}
	} else if (change_res_horz !=0 && is_qnxterm(cur_term->type.term_names)) {

		if (key_mouse == 0) {
			/* QNX term don't have kmous entry, however, we need
			 * set KEY_MOUSE_SUB1, SUB2, SUB3 keys to map 3 different
			 * mouse event. see comment in _nc_mouse_inline for detail.
			 */
			char s[MAX_KEY_MOUSE_LENGTH];

			sprintf(s, "%s31;", change_res_horz);
			_nc_add_to_try(&(SP->_key_ok), s, KEY_MOUSE_SUB1);
			sprintf(s, "%s32;", change_res_horz);
			_nc_add_to_try(&(SP->_key_ok), s, KEY_MOUSE_SUB2);
			sprintf(s, "%s33;", change_res_horz);
			_nc_add_to_try(&(SP->_key_ok), s, KEY_MOUSE_SUB3); 
		}
		mousetype = M_QNX_TERM;
	} else if ((SP->_qnx_mouse_ctrl = mouse_open(0, 0, SP->_ifd)) != 0
				&& (SP->_qnx_mouse_proxy = qnx_proxy_attach(0, 0, 0, -1)) != -1)
	{
		mousetype = M_QNX;
	} else
		mousetype = M_NONE;
#else
    /* we know how to recognize mouse events under xterm */
    if (key_mouse != 0
     && is_xterm(cur_term->type.term_names))
		mousetype = M_XTERM;
#	if USE_GPM_SUPPORT
    else if (!strncmp(cur_term->type.term_names, "linux", 5))
    {
	/* GPM: initialize connection to gpm server */
	gpm_connect.eventMask = GPM_DOWN|GPM_UP;
	gpm_connect.defaultMask = ~gpm_connect.eventMask;
	gpm_connect.minMod = 0;
	gpm_connect.maxMod = ~0;
	if (Gpm_Open (&gpm_connect, 0) >= 0) { /* returns the file-descriptor */
	    mousetype = M_GPM;
	    SP->_mouse_fd = gpm_fd;
	}
    }
#	endif
#endif
    T(("_nc_mouse_init() set mousetype to %d", mousetype));
}

static bool _nc_mouse_event(SCREEN *sp GCC_UNUSED)
/* query to see if there is a pending mouse event */
{
#if	defined(__QNX__) && !defined(__QNXNTO__)
	static	float gain = 5;
	static	int	last_mousey = -1;
	static	int last_mousex;
	static int mousex= 0 ; /* mouse returns relative */
	static int mousey= 0 ; /* coordinates so we have to integrate */

	int dx, dy ;
   	int smx, smy ; /* sCALED mOUSE xy */
	int cursor_y, cursor_x ;
	chtype	char_on_mouse;
	MEVENT	*prev = PREV(eventp);
    struct	mouse_event m;

    if (mousetype == M_QNX)
	{
		if (mouse_read(SP->_qnx_mouse_ctrl, &m, 1, SP->_qnx_mouse_proxy, 
				 									&SP->_qnx_mouse_armed) > 0)
		{
			T(("mouse_read() armed = %d", SP->_qnx_mouse_armed));

			dx = m.dx, dy = m.dy;

			/* integrate */
			mousex += dx ; /* units are in text coords */
			mousey += -1 * dy ;

			/* clip */
        	if (mousex < 0)
				mousex = 0 ;
	        else if (mousex > (COLS-1) * gain) 
				mousex = (COLS-1) * gain ;

        	if (mousey < 0) 
				mousey= 0 ;
	        else if (mousey > (LINES-1) * gain)
				mousey= (LINES-1)*gain ;

 			/* scale */
	   		smx = mousex / gain ;
    	    smy = mousey / gain ;

			/* show the mouse_cursor. */

			/* first of all, restore the last_mousey, last_mousex. */
			getsyx(cursor_y, cursor_x) ;

			if (last_mousey != -1) {
				putp(tparm(cursor_address, last_mousey, last_mousex));
				char_on_mouse = SP->_curscr->_line[last_mousey].text[last_mousex];
				char_on_mouse ^= A_REVERSE;
				SP->_curscr->_line[last_mousey].text[last_mousex] = char_on_mouse;
				UpdateAttrs(char_on_mouse);
				putc((int)TextOf(char_on_mouse), SP->_ofp);
			}

			/* then, moveto new mouse cursor on screen */
			putp(tparm(cursor_address, smy, smx));
			char_on_mouse = SP->_curscr->_line[smy].text[smx];
			char_on_mouse ^= A_REVERSE;
			SP->_curscr->_line[smy].text[smx] = char_on_mouse;
			UpdateAttrs(char_on_mouse);
			putc((int)TextOf(char_on_mouse), SP->_ofp);

			/* restore cursor to position */
			putp(tparm(cursor_address, cursor_y, cursor_x));

			fflush(stdout);

			last_mousey= smy ;
			last_mousex= smx ;

			eventp->id = 0 ;
			eventp->x = smx ;
			eventp->y = smy ;
			eventp->z = 0 ;
			eventp->bstate= 0 ;

			if (m.buttons & _MOUSE_LEFT)
				eventp->bstate |= BUTTON1_PRESSED ;
			if (m.buttons & _MOUSE_MIDDLE)
				eventp->bstate |= BUTTON2_PRESSED ;
			if (m.buttons & _MOUSE_RIGHT)
				eventp->bstate |= BUTTON3_PRESSED ;
			if (m.buttons & _MOUSE_BUT4)
				eventp->bstate |= BUTTON4_PRESSED ;

			/* mapping a BUTTON RELEASE */
			if (eventp->bstate == 0 && (prev->bstate & (BUTTON1_PRESSED|BUTTON2_PRESSED|BUTTON3_PRESSED|BUTTON4_PRESSED)))
				eventp->bstate = prev->bstate >> 1;

			T(("mouse-event at (%d,%d) bstate %x", eventp->y, eventp->x, eventp->bstate));
			eventp= NEXT(eventp);
			return (TRUE);
		} 
		return (FALSE);
	}
#elif USE_GPM_SUPPORT
    /* GPM: query server for event, return TRUE if we find one */
    Gpm_Event ev;

    if (gpm_fd >= 0
     && _nc_timed_wait(2, 0, (int *)0)
     && Gpm_GetEvent(&ev) == 1)
    {
	eventp->id = 0;		/* there's only one mouse... */

	eventp->bstate = 0;
	switch (ev.type & 0x0f)
	{
	case(GPM_DOWN):
	    if (ev.buttons & GPM_B_LEFT)   eventp->bstate |= BUTTON1_PRESSED;
	    if (ev.buttons & GPM_B_MIDDLE) eventp->bstate |= BUTTON2_PRESSED;
	    if (ev.buttons & GPM_B_RIGHT)  eventp->bstate |= BUTTON3_PRESSED;
	    break;
	case(GPM_UP):
	    if (ev.buttons & GPM_B_LEFT)   eventp->bstate |= BUTTON1_RELEASED;
	    if (ev.buttons & GPM_B_MIDDLE) eventp->bstate |= BUTTON2_RELEASED;
	    if (ev.buttons & GPM_B_RIGHT)  eventp->bstate |= BUTTON3_RELEASED;
	    break;
	default:
	    break;
	}

	eventp->x = ev.x - 1;
	eventp->y = ev.y - 1;
	eventp->z = 0;

	/* bump the next-free pointer into the circular list */
	eventp = NEXT(eventp);
	return (TRUE);
    }
#endif

    /* xterm: never have to query, mouse events are in the keyboard stream */
    return(FALSE);	/* no event waiting */
}

static bool _nc_mouse_inline(SCREEN *sp)
/* mouse report received in the keyboard stream -- parse its info */
{
    TR(MY_TRACE, ("_nc_mouse_inline() called"));

    if (mousetype == M_XTERM)
    {
	unsigned char	kbuf[4];
	MEVENT	*prev;
	size_t	grabbed;

	/* This code requires that your xterm entry contain the kmous
	 * capability and that it be set to the \E[M documented in the
	 * Xterm Control Sequences reference.  This is how we
	 * arrange for mouse events to be reported via a KEY_MOUSE
	 * return value from wgetch().  After this value is received,
	 * _nc_mouse_inline() gets called and is immediately
	 * responsible for parsing the mouse status information
	 * following the prefix.
	 *
	 * The following quotes from the ctrlseqs.ms document in the
	 * X distribution, describing the X mouse tracking feature:
	 *
	 * Parameters for all mouse tracking escape sequences
	 * generated by xterm encode numeric parameters in a single
	 * character as value+040.  For example, !  is 1.
	 *
	 * On button press or release, xterm sends ESC [ M CbCxCy.
	 * The low two bits of Cb encode button information: 0=MB1
	 * pressed, 1=MB2 pressed, 2=MB3 pressed, 3=release.  The
	 * upper bits encode what modifiers were down when the
	 * button was pressed and are added together.  4=Shift,
	 * 8=Meta, 16=Control.  Cx and Cy are the x and y coordinates
	 * of the mouse event.  The upper left corner is (1,1).
	 *
	 * (End quote)  By the time we get here, we've eaten the
	 * key prefix.  FYI, the loop below is necessary because
	 * mouse click info isn't guaranteed to present as a
	 * single clist item.  It always does under Linux but often
	 * fails to under Solaris.
	 */
#if	defined(__QNX__) && !defined(__QNXNTO__)
	for (grabbed = 0; grabbed < 3; grabbed ++)
	{
		if (dev_read(SP->_ifd, &kbuf[grabbed], 1, 1, 0, 0, 0, 0) == -1)
			return (FALSE);
		Creceive(SP->_qnx_kbd_proxy, 0 ,0);
	}
#else
	int res;
	for (grabbed = 0; grabbed < 3; grabbed += res)
	{
	     res = read(sp->_ifd, kbuf + grabbed, 3-grabbed);
	     if (res == -1)
		 break;
	}
#endif
	kbuf[3] = '\0';

	TR(TRACE_IEVENT, ("_nc_mouse_inline sees the following xterm data: '%s'", kbuf));

	eventp->id = 0;		/* there's only one mouse... */

	/* processing code goes here */
	eventp->bstate = 0;
	switch (kbuf[0] & 0x3)
	{
	case 0x0:
	    eventp->bstate = BUTTON1_PRESSED;
	    break;

	case 0x1:
	    eventp->bstate = BUTTON2_PRESSED;
	    break;

	case 0x2:
	    eventp->bstate = BUTTON3_PRESSED;
	    break;

	case 0x3:
	    /*
	     * Release events aren't reported for individual buttons,
	     * just for the button set as a whole...
	     */
	    eventp->bstate =
		(BUTTON1_RELEASED |
		 BUTTON2_RELEASED |
		 BUTTON3_RELEASED);
	    /*
	     * ...however, because there are no kinds of mouse events under
	     * xterm that can intervene between press and release, we can
	     * deduce which buttons were actually released by looking at the
	     * previous event.
	     */
	    prev = PREV(eventp);
	    if (!(prev->bstate & BUTTON1_PRESSED))
		eventp->bstate &=~ BUTTON1_RELEASED;
	    if (!(prev->bstate & BUTTON2_PRESSED))
		eventp->bstate &=~ BUTTON2_RELEASED;
	    if (!(prev->bstate & BUTTON3_PRESSED))
		eventp->bstate &=~ BUTTON3_RELEASED;
	    break;
	}

	if (kbuf[0] & 4) {
	    eventp->bstate |= BUTTON_SHIFT;
	}
	if (kbuf[0] & 8) {
	    eventp->bstate |= BUTTON_ALT;
	}
	if (kbuf[0] & 16) {
	    eventp->bstate |= BUTTON_CTRL;
	}

	eventp->x = (kbuf[1] - ' ') - 1;
	eventp->y = (kbuf[2] - ' ') - 1;
	TR(MY_TRACE, ("_nc_mouse_inline: primitive mouse-event %s has slot %d", _tracemouse(eventp), eventp - events));

	/* bump the next-free pointer into the circular list */
	eventp = NEXT(eventp);
	return(TRUE);
    }
#if	defined(__QNX__) && !defined(__QNXNTO__)
	else if (mousetype == M_QNX_TERM) {
		unsigned char	kbuf[MAX_KEY_MOUSE_LENGTH];
//		MEVENT	*prev;
		int	grabbed, type;

		/* A QNX term on photon (pterm or wterm) will return mouse
		 * event in keystream (as a escape sequences). Here is the
		 * defination:
		 *
		 * There 3 kind of Mouse escape sequences report:
		 *
    	 * 	<header>31;<row>;<col>;<type>;<clicks>t
		 *		report mouse button pressed. 
		 *		<header> is "chr"
		 *		<row><col> is the mouse position
		 *		<type> is 1=right, 2-middle, 4=left;
		 *		<clicks> is click count.
		 *			a double click reported as 2 sequences with
		 *			<clicks>=1 on first click and <clicks>=2 on
    	 *			second click.
		 *
		 *  <header>32;<row>;<col>;<type>t
		 *		report mouse button released.
		 *
		 *	<header>33;<row>;<col>;<type>t
		 *		report mouse is dragged as.
		 *
		 * Here we map "31;" to a press, no metter what his <click> is.
		 * (let _nc_mouse_parse() do this. Map "32;" to a release. And
		 * map "33;" to "RESERVED_EVENT". (Drag is not support in ncurses)
		 */

		for (grabbed = 0; grabbed < 14; grabbed ++)
		{
			if (dev_read(SP->_ifd, &kbuf[grabbed], 1, 1, 0, 0, 0, 0) == -1)
				return (FALSE);
			Creceive(SP->_qnx_kbd_proxy, 0 ,0);
			if (kbuf[grabbed] == 't')
				break;
		}

		kbuf[grabbed < MAX_KEY_MOUSE_LENGTH - 1 ? grabbed + 1 : grabbed] = '\0';

		TR(TRACE_IEVENT, ("_nc_mouse_inline sees the following xterm data: '%s'", kbuf));

		eventp->id = 0;		/* there's only one mouse... */

		/* processing code goes here */
		sscanf(kbuf, "%d;%d;%d", &eventp->y, &eventp->x, &type);

		/* map <type> to button no. */
		if (type == 1) type = 3;
		else if (type == 4) type = 1;
		type--;
		eventp->bstate = 0;

		switch (sp->_qnx_term_mouse_subkey) {
			case KEY_MOUSE_SUB1:		/* PRESSED */
				eventp->bstate = (002 << (6 * type));
				break;
			case KEY_MOUSE_SUB2:		/* RELEASED */
				eventp->bstate = (001 << (6 * type));
				break;
			case KEY_MOUSE_SUB3:		/* Map a drag as a RESERVED_EVENT */
				eventp->bstate = (040 << (6 * type));
				break;
		}
		TR(MY_TRACE, ("_nc_mouse_inline: primitive mouse-event %s has slot %d", _tracemouse(eventp), eventp - events));

		/* bump the next-free pointer into the circular list */
		eventp = NEXT(eventp);
		return(TRUE);
	}
#endif

    return(FALSE);
}

static void mouse_activate(bool on)
{
    _nc_mouse_init();

    if (on) {

	switch (mousetype) {
	case M_XTERM:
#ifdef NCURSES_EXT_FUNCS
	    keyok(KEY_MOUSE, on);
#endif
	    TPUTS_TRACE("xterm mouse initialization");
	    putp("\033[?1000h");
	    break;
#if	defined(__QNX__) && !defined(__QNXNTO__)
	case M_QNX_TERM:
#ifdef NCURSES_EXT_FUNCS
		keyok(KEY_MOUSE_SUB1, on);
		keyok(KEY_MOUSE_SUB2, on);
		keyok(KEY_MOUSE_SUB3, on);
#endif
		TPUTS_TRACE("qnx_term mouse initialization");
		putp(parm_left_micro);		/* return SELECT press */
		putp(micro_left);			/* return mouse movement */
		putp(parm_down_micro);		/* return ADJUST press */
		putp(parm_right_micro);		/* return MENU press */
		putp(parm_up_micro);		/* report button release */
		break;
	case M_QNX:
		/* Mouse Ctrl & Mouse Proxy is set in mouse_init(),
		 * just read 0 event to arm mouse
		 */
		mouse_read(SP->_qnx_mouse_ctrl, NULL, 0, SP->_qnx_mouse_proxy, &SP->_qnx_mouse_armed);
		break;
#endif
#if USE_GPM_SUPPORT
	case M_GPM:
	    SP->_mouse_fd = gpm_fd;
	    break;
#endif
	}
	/* Make runtime binding to cut down on object size of applications that
	 * do not use the mouse (e.g., 'clear').
	 */
	SP->_mouse_event  = _nc_mouse_event;
	SP->_mouse_inline = _nc_mouse_inline;
	SP->_mouse_parse  = _nc_mouse_parse;
	SP->_mouse_resume = _nc_mouse_resume;
	SP->_mouse_wrap   = _nc_mouse_wrap;

    } else {

	switch (mousetype) {
	case M_XTERM:
	    TPUTS_TRACE("xterm mouse deinitialization");
	    putp("\033[?1000l");
	    break;
#if	defined(__QNX__) && !defined(__QNXNTO__)
	case M_QNX_TERM:
		TPUTS_TRACE("qnx term mouse deinitialization");
		putp(micro_down);			/* Don't return press */
		putp(micro_right);			/* Don't return mouse movement */
		putp(micro_up);				/* Don't report button release */
		break;
	case M_QNX:
		mouse_read(SP->_qnx_mouse_ctrl, NULL, 0, 0, 0);
		mouse_flush(SP->_qnx_mouse_ctrl);
		SP->_qnx_mouse_armed = ~0;
		break;
#endif
#if USE_GPM_SUPPORT
	case M_GPM:
	    break;
#endif
	}
    }
    (void) fflush(SP->_ofp);
}

/**************************************************************************
 *
 * Device-independent code
 *
 **************************************************************************/

static bool _nc_mouse_parse(int runcount)
/* parse a run of atomic mouse events into a gesture */
{
    MEVENT	*ep, *runp, *next, *prev = PREV(eventp);
    int		n;
    bool	merge;

    TR(MY_TRACE, ("_nc_mouse_parse(%d) called", runcount));

	if (runcount < 1)
		return FALSE;
    /*
     * When we enter this routine, the event list next-free pointer
     * points just past a run of mouse events that we know were separated
     * in time by less than the critical click interval. The job of this
     * routine is to collaps this run into a single higher-level event
     * or gesture.
     *
     * We accomplish this in two passes.  The first pass merges press/release
     * pairs into click events.  The second merges runs of click events into
     * double or triple-click events.
     *
     * It's possible that the run may not resolve to a single event (for
     * example, if the user quadruple-clicks).  If so, leading events
     * in the run are ignored.
     *
     * Note that this routine is independent of the format of the specific
     * format of the pointing-device's reports.  We can use it to parse
     * gestures on anything that reports press/release events on a per-
     * button basis, as long as the device-dependent mouse code puts stuff
     * on the queue in MEVENT format.
     */
    if (runcount == 1)
    {
	TR(MY_TRACE, ("_nc_mouse_parse: returning simple mouse event %s at slot %d",
	   _tracemouse(prev), prev-events));
#if	defined(__QNX__) && !defined(__QNXNTO__)
		if (prev->id < 0 || !(prev->bstate & eventmask)) {
			prev->id = INVALID_EVENT;
			eventp = prev;
			return FALSE;
		} else
			return TRUE;
#else		
	return (prev->id >= 0)
		? ((prev->bstate & eventmask) ? TRUE : FALSE)
		: FALSE;
#endif
	}

    if (runcount > EV_MAX)
		runcount =  EV_MAX;

    /* find the start of the run */
    runp = eventp;
    for (n = runcount; n > 0; n--) {
		runp = PREV(runp);
    }

#ifdef TRACE
    if (_nc_tracing & TRACE_IEVENT)
    {
	_trace_slot("before mouse press/release merge:");
	_tracef("_nc_mouse_parse: run starts at %d, ends at %d, count %d",
	    runp-events, ((eventp - events) + (EV_MAX-1)) % EV_MAX, runcount);
    }
#endif /* TRACE */

    /* first pass; merge press/release pairs */
    do {
	merge = FALSE;
	for (ep = runp; next = NEXT(ep), next != eventp; ep = next)
	{
	    if (ep->x == next->x && ep->y == next->y
		&& (ep->bstate & (BUTTON1_PRESSED|BUTTON2_PRESSED|BUTTON3_PRESSED))
		&& (!(ep->bstate & BUTTON1_PRESSED)
		    == !(next->bstate & BUTTON1_RELEASED))
		&& (!(ep->bstate & BUTTON2_PRESSED)
		    == !(next->bstate & BUTTON2_RELEASED))
		&& (!(ep->bstate & BUTTON3_PRESSED)
		    == !(next->bstate & BUTTON3_RELEASED))
		)
	    {
		if ((eventmask & BUTTON1_CLICKED)
			&& (ep->bstate & BUTTON1_PRESSED))
		{
		    ep->bstate &=~ BUTTON1_PRESSED;
		    ep->bstate |= BUTTON1_CLICKED;
		    merge = TRUE;
		}
		if ((eventmask & BUTTON2_CLICKED)
			&& (ep->bstate & BUTTON2_PRESSED))
		{
		    ep->bstate &=~ BUTTON2_PRESSED;
		    ep->bstate |= BUTTON2_CLICKED;
		    merge = TRUE;
		}
		if ((eventmask & BUTTON3_CLICKED)
			&& (ep->bstate & BUTTON3_PRESSED))
		{
		    ep->bstate &=~ BUTTON3_PRESSED;
		    ep->bstate |= BUTTON3_CLICKED;
		    merge = TRUE;
		}
		if (merge)
		    next->id = INVALID_EVENT;
	    }
	}
    } while
	(merge);

#ifdef TRACE
    if (_nc_tracing & TRACE_IEVENT)
    {
	_trace_slot("before mouse click merge:");
	_tracef("_nc_mouse_parse: run starts at %d, ends at %d, count %d",
	    runp-events, ((eventp - events) + (EV_MAX-1)) % EV_MAX, runcount);
    }
#endif /* TRACE */

    /*
     * Second pass; merge click runs.  At this point, click events are
     * each followed by one invalid event. We merge click events
     * forward in the queue.
     *
     * NOTE: There is a problem with this design!  If the application
     * allows enough click events to pile up in the circular queue so
     * they wrap around, it will cheerfully merge the newest forward
     * into the oldest, creating a bogus doubleclick and confusing
     * the queue-traversal logic rather badly.  Generally this won't
     * happen, because calling getmouse() marks old events invalid and
     * ineligible for merges.  The true solution to this problem would
     * be to timestamp each MEVENT and perform the obvious sanity check,
     * but the timer element would have to have sub-second resolution,
     * which would get us into portability trouble.
     */
    do {
	MEVENT	*follower;

	merge = FALSE;
	for (ep = runp; next = NEXT(ep), next != eventp; ep = next)
	    if (ep->id != INVALID_EVENT)
	    {
		if (next->id != INVALID_EVENT)
		    continue;
		follower = NEXT(next);
		if (follower->id == INVALID_EVENT)
		    continue;

		/* merge click events forward */
		if ((ep->bstate &
			(BUTTON1_CLICKED | BUTTON2_CLICKED | BUTTON3_CLICKED))
		    && (follower->bstate &
			(BUTTON1_CLICKED | BUTTON2_CLICKED | BUTTON3_CLICKED)))
		{
		    if ((eventmask & BUTTON1_DOUBLE_CLICKED)
			&& (follower->bstate & BUTTON1_CLICKED))
		    {
			follower->bstate &=~ BUTTON1_CLICKED;
			follower->bstate |= BUTTON1_DOUBLE_CLICKED;
			merge = TRUE;
		    }
		    if ((eventmask & BUTTON2_DOUBLE_CLICKED)
			&& (follower->bstate & BUTTON2_CLICKED))
		    {
			follower->bstate &=~ BUTTON2_CLICKED;
			follower->bstate |= BUTTON2_DOUBLE_CLICKED;
			merge = TRUE;
		    }
		    if ((eventmask & BUTTON3_DOUBLE_CLICKED)
			&& (follower->bstate & BUTTON3_CLICKED))
		    {
			follower->bstate &=~ BUTTON3_CLICKED;
			follower->bstate |= BUTTON3_DOUBLE_CLICKED;
			merge = TRUE;
		    }
		    if (merge)
			ep->id = INVALID_EVENT;
		}

		/* merge double-click events forward */
		if ((ep->bstate &
			(BUTTON1_DOUBLE_CLICKED
			 | BUTTON2_DOUBLE_CLICKED
			 | BUTTON3_DOUBLE_CLICKED))
		    && (follower->bstate &
			(BUTTON1_CLICKED | BUTTON2_CLICKED | BUTTON3_CLICKED)))
		{
		    if ((eventmask & BUTTON1_TRIPLE_CLICKED)
			&& (follower->bstate & BUTTON1_CLICKED))
		    {
			follower->bstate &=~ BUTTON1_CLICKED;
			follower->bstate |= BUTTON1_TRIPLE_CLICKED;
			merge = TRUE;
		    }
		    if ((eventmask & BUTTON2_TRIPLE_CLICKED)
			&& (follower->bstate & BUTTON2_CLICKED))
		    {
			follower->bstate &=~ BUTTON2_CLICKED;
			follower->bstate |= BUTTON2_TRIPLE_CLICKED;
			merge = TRUE;
		    }
		    if ((eventmask & BUTTON3_TRIPLE_CLICKED)
			&& (follower->bstate & BUTTON3_CLICKED))
		    {
			follower->bstate &=~ BUTTON3_CLICKED;
			follower->bstate |= BUTTON3_TRIPLE_CLICKED;
			merge = TRUE;
		    }
		    if (merge)
			ep->id = INVALID_EVENT;
		}
	    }
    } while
	(merge);

#ifdef TRACE
    if (_nc_tracing & TRACE_IEVENT)
    {
	_trace_slot("before mouse event queue compaction:");
	_tracef("_nc_mouse_parse: run starts at %d, ends at %d, count %d",
	    runp-events, ((eventp - events) + (EV_MAX-1)) % EV_MAX, runcount);
    }
#endif /* TRACE */

    /*
     * Now try to throw away trailing events flagged invalid, or that
     * don't match the current event mask.
     */
    for (; runcount; prev = PREV(eventp), runcount--)
	if (prev->id == INVALID_EVENT || !(prev->bstate & eventmask)) {
#if	defined(__QNX__) && !defined(__QNXNTO__)
		prev->id = INVALID_EVENT;
#endif
	    eventp = prev;
	}

#ifdef TRACE
    if (_nc_tracing & TRACE_IEVENT)
    {
	_trace_slot("after mouse event queue compaction:");
	_tracef("_nc_mouse_parse: run starts at %d, ends at %d, count %d",
	    runp-events, ((eventp - events) + (EV_MAX-1)) % EV_MAX, runcount);
    }
    for (ep = runp; ep != eventp; ep = NEXT(ep))
	if (ep->id != INVALID_EVENT)
	    TR(MY_TRACE, ("_nc_mouse_parse: returning composite mouse event %s at slot %d",
		_tracemouse(ep), ep-events));
#endif /* TRACE */

    /* after all this, do we have a valid event? */
    return(PREV(eventp)->id != INVALID_EVENT);
}

static void _nc_mouse_wrap(SCREEN *sp GCC_UNUSED)
/* release mouse -- called by endwin() before shellout/exit */
{
    TR(MY_TRACE, ("_nc_mouse_wrap() called"));

    switch (mousetype) {
    case M_XTERM:
        if (eventmask)
            mouse_activate(FALSE);
        break;
#if	defined(__QNX__) && !defined(__QNXNTO__)
	case M_QNX_TERM:
	case M_QNX:
		if (eventmask)
			mouse_activate(FALSE);
		break;
//		mouse_close(sp->_qnx_mouse_ctrl);
//		qnx_proxy_detach(sp->_qnx_mouse_proxy);
//		break;
#endif
#if USE_GPM_SUPPORT
	/* GPM: pass all mouse events to next client */
	case M_GPM:
	    break;
#endif
    }
}

static void _nc_mouse_resume(SCREEN *sp GCC_UNUSED)
/* re-connect to mouse -- called by doupdate() after shellout */
{
    TR(MY_TRACE, ("_nc_mouse_resume() called"));

#if	defined(__QNX__) && !defined(__QNXNTO__)
	switch (mousetype) {
	case M_XTERM:
	case M_QNX_TERM:
	case M_QNX:
		if (eventmask)
			mouse_activate(TRUE);
		break;
	}
#else
    /* xterm: re-enable reporting */
    if (mousetype == M_XTERM && eventmask)
	mouse_activate(TRUE);
    /* GPM: reclaim our event set */
#endif
}

/**************************************************************************
 *
 * Mouse interface entry points for the API
 *
 **************************************************************************/

int getmouse(MEVENT *aevent)
/* grab a copy of the current mouse event */
{
    T((T_CALLED("getmouse(%p)"), aevent));

    if (aevent && (mousetype != M_NONE))
    {
	/* compute the current-event pointer */
	MEVENT	*prev = PREV(eventp);

	/* copy the event we find there */
	*aevent = *prev;

	TR(TRACE_IEVENT, ("getmouse: returning event %s from slot %d",
	    _tracemouse(prev), prev-events));

	prev->id = INVALID_EVENT;	/* so the queue slot becomes free */
	returnCode(OK);
    }
    returnCode(ERR);
}

int ungetmouse(MEVENT *aevent)
/* enqueue a synthesized mouse event to be seen by the next wgetch() */
{
    /* stick the given event in the next-free slot */
    *eventp = *aevent;

    /* bump the next-free pointer into the circular list */
    eventp = NEXT(eventp);

    /* push back the notification event on the keyboard queue */
    return ungetch(KEY_MOUSE);
}

mmask_t mousemask(mmask_t newmask, mmask_t *oldmask)
/* set the mouse event mask */
{
    mmask_t result = 0;

    T((T_CALLED("mousemask(%#lx,%p)"), newmask, oldmask));

    if (oldmask)
	*oldmask = eventmask;

    _nc_mouse_init();
    if ( mousetype != M_NONE )
    {
	eventmask = newmask &
	    (BUTTON_ALT | BUTTON_CTRL | BUTTON_SHIFT
	     | BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED
	     | BUTTON1_DOUBLE_CLICKED | BUTTON1_TRIPLE_CLICKED
	     | BUTTON2_PRESSED | BUTTON2_RELEASED | BUTTON2_CLICKED
	     | BUTTON2_DOUBLE_CLICKED | BUTTON2_TRIPLE_CLICKED
	     | BUTTON3_PRESSED | BUTTON3_RELEASED | BUTTON3_CLICKED
	     | BUTTON3_DOUBLE_CLICKED | BUTTON3_TRIPLE_CLICKED);

	mouse_activate(eventmask != 0);

	result = eventmask;
    }

    returnCode(result);
}

bool wenclose(const WINDOW *win, int y, int x)
/* check to see if given window encloses given screen location */
{
    if (win)
    {
	y -= win->_yoffset;
	return ((win->_begy <= y &&
		 win->_begx <= x &&
		 (win->_begx + win->_maxx) >= x &&
		 (win->_begy + win->_maxy) >= y) ? TRUE : FALSE);
    }
    return FALSE;
}

int mouseinterval(int maxclick)
/* set the maximum mouse interval within which to recognize a click */
{
    int oldval;

    if (SP != 0) {
	oldval = SP->_maxclick;
	if (maxclick >= 0)
	    SP->_maxclick = maxclick;
    } else {
	oldval = DEFAULT_MAXCLICK;
    }

    return(oldval);
}

/* This may be used by other routines to ask for the existence of mouse
   support */
int _nc_has_mouse(void) {
  return (mousetype==M_NONE ? 0:1);
}

/* lib_mouse.c ends here */
