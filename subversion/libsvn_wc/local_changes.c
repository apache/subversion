/*
 * local_changes.c:  preserving local mods across updates.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_time.h>
#include <apr_strings.h>
#include "svn_wc.h"
#include "wc.h"




/*** Timestamp generation and comparison. ***/

svn_error_t *
svn_wc__file_affected_time (apr_time_t *apr_time,
                            svn_string_t *path,
                            apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_error_t *err;
  svn_string_t *prop_path, *shorter_path, *basename;
  enum svn_node_kind pkind;
  apr_finfo_t finfo, pinfo;
  apr_time_t latest_file_time = 0;
  apr_time_t latest_prop_time = 0;

  /* finfo holds the status of the working file */
  apr_err = apr_stat (&finfo, path->data, pool);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "svn_wc__file_affected_time: cannot stat %s", path->data);

  /* Figure out if this file has properties */
  svn_path_split (path, &shorter_path, &basename,
                  svn_path_local_style, pool);
  prop_path = svn_wc__adm_path (shorter_path,
                                0, /* no tmp */
                                pool,
                                SVN_WC__ADM_PROPS,
                                basename->data,
                                NULL);
  err = svn_io_check_path (prop_path, &pkind, pool);
  if (err) return err;
  if (pkind == svn_node_file)
    {
      /* Aha, this file has properties! */
      /* pinfo holds the status of the property file */
      apr_err = apr_stat (&pinfo, prop_path->data, pool);
      if (apr_err)
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "svn_wc__file_affected_time: cannot stat %s", prop_path->data);
    }

  /* Compare timestamps all around, return whichever is latest. */
  if (finfo.mtime > finfo.ctime)
    latest_file_time = finfo.mtime;
  else
    latest_file_time = finfo.ctime;

  if (pkind == svn_node_file)
    {
      if (pinfo.mtime > pinfo.ctime)
        latest_prop_time = pinfo.mtime;
      else
        latest_prop_time = pinfo.ctime;
    }

  if (latest_file_time > latest_prop_time)
    *apr_time = latest_file_time;
  else
    *apr_time = latest_prop_time;

  return SVN_NO_ERROR;
}


/** kff todo: these are quite general and could go into
    libsvn_subr or a libsvn_time. **/

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


svn_string_t *
svn_wc__time_to_string (apr_time_t t, apr_pool_t *pool)
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

  return svn_string_create (t_cstr, pool);
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


apr_time_t
svn_wc__string_to_time (svn_string_t *tstr)
{
  apr_exploded_time_t exploded_time;
  char wday[4], month[4];
  apr_time_t time;

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

  apr_implode_time (&time, &exploded_time);

  return time;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
