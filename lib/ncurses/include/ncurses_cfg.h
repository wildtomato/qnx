/* include/ncurses_cfg.h.  Generated automatically by configure.  */
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
 *  Author: Thomas E. Dickey <dickey@clark.net> 1997                        *
 ****************************************************************************/
/*
 * $Id: ncurses_cfg.h 153052 2007-11-02 21:10:56Z coreos $
 *
 * This is a template-file used to generate the "ncurses_cfg.h" file.
 *
 * Rather than list every definition, the configuration script substitutes
 * the definitions that it finds using 'sed'.  You need a patch (971222)
 * to autoconf 2.12 to do this.
 */
#ifndef NC_CONFIG_H
#define NC_CONFIG_H

#define BROKEN_LINKER 1
#define CC_HAS_INLINE_FUNCS 1
#define CC_HAS_PROTOS 1
#define HAVE_DIRENT_H 1
#define HAVE_ERRNO 1
#define HAVE_FCNTL_H 1
#define HAVE_FORM_H 1
#define HAVE_ISASCII 1
#define HAVE_LIBC_H 1
#define HAVE_LIBFORM 1
#define HAVE_LIBMENU 1
#define HAVE_LIBPANEL 1
#define HAVE_LIMITS_H 1
#define HAVE_LINK 1
#define HAVE_LOCALE_H 1
#define HAVE_LONG_FILE_NAMES 1
#define HAVE_MEMCCPY 1
#define HAVE_MENU_H 1
#define HAVE_NANOSLEEP 1
#define HAVE_NC_ALLOC_H 1
#define HAVE_PANEL_H 1
#define HAVE_REGEX_H_FUNCS 1
#define HAVE_REMOVE 1
#define HAVE_SELECT 1
#define HAVE_SETBUF 1
#define HAVE_SETVBUF 1
#define HAVE_SIGACTION 1
#define HAVE_SIZECHANGE 1
#define HAVE_STRDUP 1
#define HAVE_SYMLINK 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_TIMES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TIME_SELECT 1
#define HAVE_TCGETATTR 1
#define HAVE_TERMIOS_H 1
#define HAVE_TERMIO_H 1
#define HAVE_TIMES 1
#define HAVE_TYPEINFO 1
#define HAVE_UNISTD_H 1
#define HAVE_VFSCANF 1
#define HAVE_VSSCANF 1
#define NCURSES_EXT_FUNCS 1
#define PURE_TERMINFO 1
#define RETSIGTYPE void
#define STDC_HEADERS 1
#define SYSTEM_NAME "qnx"
#define TYPEOF_CHTYPE long
#define USE_DATABASE 1
#define USE_HASHMAP 1
#define USE_SCROLL_HINTS 1
#define inline __inline
	/* The C compiler may not treat these properly but C++ has to */
#ifdef __cplusplus
#undef const
#undef inline
#else
#if defined(lint) || defined(TRACE) || defined(__WATCOMC__)
#undef inline
#define inline /* nothing */
#endif
#endif

#endif /* NC_CONFIG_H */
