/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_time.h
 * @brief Time/date utilities
 * @{
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


/** Convert a time value to a string.
 *
 * Convert @a when to a <tt>const char *</tt> representation allocated
 * in @a pool.  Use svn_time_from_cstring() for the reverse
 * conversion.
 */
const char *svn_time_to_cstring (apr_time_t when, apr_pool_t *pool);

/** Convert a string to a time value.
 *
 * Convert @a timestr to an @c apr_time_t @a when, allocated in @a
 * pool.
 */
svn_error_t *svn_time_from_cstring (apr_time_t *when, const char *data,
                                    apr_pool_t *pool);

/** Convert a time value to a display string.
 *
 * Convert @a when to a <tt>const char *</tt> representation allocated
 * in @a pool, suitable for human display.
 */
const char *svn_time_to_human_cstring (apr_time_t when, apr_pool_t *pool);


/** Needed by @c getdate.y parser. */
struct getdate_time {
  time_t time;
  short timezone;
};

/** Parse a date to a time value.
 *
 * The one interface in our @c getdate.y parser; convert
 * human-readable date @a text into a standard C @c time_t.  The 2nd
 * argument is unused; we always pass @c NULL.
 */
time_t svn_parse_date (char *text, struct getdate_time *now);

#ifdef __cplusplus
}
#endif /* __cplusplus */
/** @} */

#endif /* SVN_TIME_H */
