/* rev_hunt.c --- routines to hunt down particular fs revisions.
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


#include <string.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"




/* Note:  this binary search assumes that the datestamp properties on
   each revision are in chronological order.  That is if revision A >
   revision B, then A's datestamp is younger then B's datestamp.

   If some moron comes along and sets a bogus datestamp, this routine
   might not work right.

   ### todo:  you know, we *could* have svn_fs_change_rev_prop() do
   some semantic checking when it's asked to change special reserved
   svn: properties.  It could prevent such a problem. */


/* helper for svn_repos_dated_revision().

   Set *TIME to the apr_time_t datestamp on revision REV in FS. */
static svn_error_t *
get_time (apr_time_t *time,
          svn_fs_t *fs,
          svn_revnum_t rev,
          apr_pool_t *pool)
{
  svn_stringbuf_t *datestamp;
  svn_string_t date_prop = {SVN_PROP_REVISION_DATE,
                                   strlen(SVN_PROP_REVISION_DATE)};

  SVN_ERR (svn_fs_revision_prop (&datestamp, fs, rev, &date_prop, pool));
  if (! datestamp)    
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "failed to find datestamp on revision %ld", rev);

  *time = svn_time_from_string (datestamp);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_fs_t *fs,
                          apr_time_t time,
                          apr_pool_t *pool)
{
  svn_revnum_t rev_mid, rev_top, rev_bot, rev_latest;
  apr_time_t this_time;

  /* Initialize top and bottom values of binary search. */
  SVN_ERR (svn_fs_youngest_rev (&rev_latest, fs, pool));
  rev_bot = 0;
  rev_top = rev_latest;

  while (rev_bot <= rev_top)
    {
      rev_mid = (rev_top + rev_bot) / 2;
      SVN_ERR (get_time (&this_time, fs, rev_mid, pool));
      
      if (this_time > time) /* we've overshot */
        {
          apr_time_t previous_time;

          if ((rev_mid - 1) < 0)
            {
              *revision = 0;
              break;
            }

          /* see if time falls between rev_mid and rev_mid-1: */
          SVN_ERR (get_time (&previous_time, fs, rev_mid - 1, pool));
          if (previous_time <= time)
            {
              *revision = rev_mid - 1;
              break;
            }

          rev_top = rev_mid - 1;
        }

      else if (this_time < time) /* we've undershot */
        {
          apr_time_t next_time;

          if ((rev_mid + 1) > rev_latest)
            {
              *revision = rev_latest;
              break;
            }
          
          /* see if time falls between rev_mid and rev_mid+1: */
          SVN_ERR (get_time (&next_time, fs, rev_mid + 1, pool));
          if (next_time > time)
            {
              *revision = rev_mid + 1;
              break;
            }

          rev_bot = rev_mid + 1;
        }

      else
        {
          *revision = rev_mid;  /* exact match! */
          break;
        }
    }

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
