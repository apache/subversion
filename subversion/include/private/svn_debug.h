/* svn_debug.h : handy little debug tools for the SVN developers
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
