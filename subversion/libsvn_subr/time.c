/*
 * time.c:  time/date utilities
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



#include <apr_pools.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_time.h"



/*** Code. ***/

/* Our timestamp strings look like this:
 * 
 *    "Tue 3 Oct 2000 HH:MM:SS.UUU (day 277, dst 1, gmt_off -18000)"
 *
 * The idea is that they are conventionally human-readable for the
 * first part, and then in parentheses comes everything else required
 * to completely fill in an apr_exploded_time_t: tm_yday, tm_isdst,
 * and tm_gmtoff.
 *
 * kff todo: what about portability problems resulting from the
 * plain int assumptions below, though?  Using apr_strftime() would
 * fix that, but converting the strings back is still a problem (see
 * the comment in svn_wc__time_to_string()).
 */
static const char *timestamp_format =
"%s %d %s %d %02d:%02d:%02d.%03d (day %03d, dst %d, gmt_off %06d)";


svn_stringbuf_t *
svn_time_to_string (apr_time_t t, apr_pool_t *pool)
{
  char *t_cstr;
  apr_exploded_time_t exploded_time;

  /* We toss apr_status_t return value here -- for one thing, caller
     should pass in good information.  But also, where APR's own code
     calls these functions it tosses the return values, and
     furthermore their current implementations can only return success
     anyway. */

  apr_explode_localtime (&exploded_time, t);

  /* It would be nice to use apr_strftime(), but APR doesn't give a way
     to convert back, so we wouldn't be able to share the format string
     between the writer and reader.  Sigh.  Also, apr_strftime() doesn't
     offer format codes for its special tm_usec and tm_gmtoff fields. */
  t_cstr = apr_psprintf (pool,
                         timestamp_format,
                         apr_day_snames[exploded_time.tm_wday],
                         exploded_time.tm_mday,
                         apr_month_snames[exploded_time.tm_mon],
                         exploded_time.tm_year + 1900,
                         exploded_time.tm_hour,
                         exploded_time.tm_min,
                         exploded_time.tm_sec,
                         exploded_time.tm_usec,
                         exploded_time.tm_yday + 1,
                         exploded_time.tm_isdst,
                         exploded_time.tm_gmtoff);

  return svn_stringbuf_create (t_cstr, pool);
}


static int
find_matching_string (char *str, const char strings[][4])
{
  int i;

  for (i = 0; ; i++)
    if (strings[i] && (strcmp (str, strings[i]) == 0))
      return i;

  return -1;
}


/* ### todo:

   Recently Branko changed svn_time_from_string (or rather, he changed
   svn_wc__string_to_time, but the function's name has changed since
   then) to use apr_implode_gmt.  So now that function is using GMT,
   but its inverse above, svn_time_to_string, is using localtime.

   I'm not sure what the right thing to do is; see issue #404.

   Note, however, that repositories want to record commit dates in
   GMT.  Maybe we should just make Subversion's string representation
   for times always use GMT -- that's good for repositories, and for
   working copies it doesn't really matter since humans don't have to
   read the timestamps in SVN/entries files much (and when they do,
   they can easily do the conversion).
*/


apr_time_t
svn_time_from_string (svn_stringbuf_t *tstr)
{
  apr_exploded_time_t exploded_time;
  char wday[4], month[4];
  apr_time_t when;

  sscanf (tstr->data,
          timestamp_format,
          wday,
          &exploded_time.tm_mday,
          month,
          &exploded_time.tm_year,
          &exploded_time.tm_hour,
          &exploded_time.tm_min,
          &exploded_time.tm_sec,
          &exploded_time.tm_usec,
          &exploded_time.tm_yday,
          &exploded_time.tm_isdst,
          &exploded_time.tm_gmtoff);
  
  exploded_time.tm_year -= 1900;
  exploded_time.tm_yday -= 1;
  exploded_time.tm_wday = find_matching_string (wday, apr_day_snames);
  exploded_time.tm_mon = find_matching_string (month, apr_month_snames);

  apr_implode_gmt (&when, &exploded_time);

  return when;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
