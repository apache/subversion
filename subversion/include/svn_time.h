/*  svn_time.h: time/date utilities
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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


#ifndef SVN_TIME_H
#define SVN_TIME_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_time.h>

#include "svn_string.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Convert WHEN to a const char * representation allocated in POOL.
   Use svn_time_from_nts() for the reverse conversion. */
const char *svn_time_to_nts (apr_time_t when, apr_pool_t *pool);

/* Convert TIMESTR to an apr_time_t. */
apr_time_t svn_time_from_nts (const char *timestr);

/* Needed by getdate.y parser */
struct getdate_time {
  time_t time;
  short timezone;
};

/* The one interface in our getdate.y parser;  convert human-readable
   date TEXT into a standard C time_t.  The 2nd argument is unused;
   we always pass NULL. */
time_t svn_parse_date (char *text, struct getdate_time *now);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TIME_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
