/* svn_debug.h : handy little debug tools for the SVN developers
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifndef SVN_DEBUG_H
#define SVN_DEBUG_H

/* Only available when SVN_DEBUG is defined (ie. svn developers). Note that
   we do *not* provide replacement macros/functions for proper releases.
   The debug stuff should be removed before a commit.

   ### maybe we will eventually decide to allow certain debug stuff to
   ### remain in the code. at that point, we can rejigger this header.  */
#ifdef SVN_DEBUG

#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * The primary macro defined by this header is SVN_DBG(). It helps by
 * printing stuff to stdout (or however SVN_DBG_OUTPUT is defined) for
 * debugging purposes. Typical usage is like this:
 *
 *   SVN_DBG(("cleanup. type=%d  path='%s'\n", lock->type, lock->path));
 *
 * producing:
 *
 *   DBG: lock.c: 292: cleanup. type=2  path='include/private'
 *
 * Note that these output lines are filtered by our test suite automatically,
 * so you don't have to worry about throwing off expected output.
 */


/* A couple helper functions for the macros below.  */
void
svn_dbg__preamble(const char *file, long line, FILE *output);
void
svn_dbg__printf(const char *fmt, ...);


/* Print to stdout. Edit this line if you need stderr.  */
#define SVN_DBG_OUTPUT stdout


/* Defining this symbol in the source file, BEFORE INCLUDING THIS HEADER,
   will switch off the output. Calls will still be made to svn_dbg__preamble()
   for breakpoints.  */
#ifdef SVN_DBG_QUIET

#define SVN_DBG(ARGS) svn_dbg__preamble(__FILE__, __LINE__, NULL)

#else

#define SVN_DBG(ARGS) (svn_dbg__preamble(__FILE__, __LINE__, SVN_DBG_OUTPUT), \
                       svn_dbg__printf ARGS)

#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DEBUG */
#endif /* SVN_DEBUG_H */
