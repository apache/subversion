/*  svn_time.h: time/date utilities
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_TIME_H
#define SVN_TIME_H



#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_time.h>
#include "svn_string.h"
#include "svn_error.h"


/* Convert WHEN to an svn string representation allocated in POOL.
   Use svn_time_from_string() for the reverse conversion. */
svn_stringbuf_t *svn_time_to_string (apr_time_t when, apr_pool_t *pool);


/* Convert TIMESTR to an apr_time_t.  TIMESTR should be of the form
   returned by svn_wc__time_to_string(). */
apr_time_t svn_time_from_string (svn_stringbuf_t *timestr);


/* Needed by getdate.y parser */
struct getdate_time {
  time_t time;
  short timezone;
};

/* The one interface in our getdate.y parser;  convert human-readable
   date TEXT into a standard C time_t.  The 2nd argument is unused;
   we always pass NULL. */
time_t svn_parse_date (char *text, struct getdate_time *now);

#endif /* SVN_TIME_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
