/* rev_hunt.c --- routines to hunt down particular fs revisions and
 *                their properties.
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


#include <string.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"
#include "repos.h"



/* Note:  this binary search assumes that the datestamp properties on
   each revision are in chronological order.  That is if revision A >
   revision B, then A's datestamp is younger then B's datestamp.

   If some moron comes along and sets a bogus datestamp, this routine
   might not work right.

   ### todo:  you know, we *could* have svn_fs_change_rev_prop() do
   some semantic checking when it's asked to change special reserved
   svn: properties.  It could prevent such a problem. */


/* helper for svn_repos_dated_revision().

   Set *TM to the apr_time_t datestamp on revision REV in FS. */
static svn_error_t *
get_time (apr_time_t *tm,
          svn_fs_t *fs,
          svn_revnum_t rev,
          apr_pool_t *pool)
{
  svn_string_t *date_str;

  SVN_ERR (svn_fs_revision_prop (&date_str, fs, rev, SVN_PROP_REVISION_DATE,
                                 pool));
  if (! date_str)    
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "failed to find tm on revision %" SVN_REVNUM_T_FMT, rev);

  *tm = svn_time_from_nts (date_str->data);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_repos_t *repos,
                          apr_time_t tm,
                          apr_pool_t *pool)
{
  svn_revnum_t rev_mid, rev_top, rev_bot, rev_latest;
  apr_time_t this_time;
  svn_fs_t *fs = repos->fs;

  /* Initialize top and bottom values of binary search. */
  SVN_ERR (svn_fs_youngest_rev (&rev_latest, fs, pool));
  rev_bot = 0;
  rev_top = rev_latest;

  while (rev_bot <= rev_top)
    {
      rev_mid = (rev_top + rev_bot) / 2;
      SVN_ERR (get_time (&this_time, fs, rev_mid, pool));
      
      if (this_time > tm)/* we've overshot */
        {
          apr_time_t previous_time;

          if ((rev_mid - 1) < 0)
            {
              *revision = 0;
              break;
            }

          /* see if time falls between rev_mid and rev_mid-1: */
          SVN_ERR (get_time (&previous_time, fs, rev_mid - 1, pool));
          if (previous_time <= tm)
            {
              *revision = rev_mid - 1;
              break;
            }

          rev_top = rev_mid - 1;
        }

      else if (this_time < tm) /* we've undershot */
        {
          apr_time_t next_time;

          if ((rev_mid + 1) > rev_latest)
            {
              *revision = rev_latest;
              break;
            }
          
          /* see if time falls between rev_mid and rev_mid+1: */
          SVN_ERR (get_time (&next_time, fs, rev_mid + 1, pool));
          if (next_time > tm)
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




/*  Given a ROOT/PATH within some filesystem, return three pieces of
    information allocated in POOL:

      - set *COMMITTED_REV to the revision in which the object was
        last modified.  (In fs parlance, this is the revision in which
        the particular node-rev-id was 'created'.)
    
      - set *COMMITTED_DATE to the date of said revision.

      - set *LAST_AUTHOR to the author of said revision.    
 */
svn_error_t *
svn_repos_get_committed_info (svn_revnum_t *committed_rev,
                              const char **committed_date,
                              const char **last_author,
                              svn_fs_root_t *root,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_fs_t *fs = svn_fs_root_fs (root);

  /* ### It might be simpler just to declare that revision
     properties have char * (i.e., UTF-8) values, not arbitrary
     binary values, hmmm. */
  svn_string_t *committed_date_s, *last_author_s;
  
  /* Get the CR field out of the node's skel. */
  SVN_ERR (svn_fs_node_created_rev (committed_rev, root, path, pool));

  /* Get the date property of this revision. */
  SVN_ERR (svn_fs_revision_prop (&committed_date_s, fs, *committed_rev,
                                 SVN_PROP_REVISION_DATE, pool));

  /* Get the author property of this revision. */
  SVN_ERR (svn_fs_revision_prop (&last_author_s, fs, *committed_rev,
                                 SVN_PROP_REVISION_AUTHOR, pool));

  *committed_date = committed_date_s->data;
  *last_author = last_author_s->data;
  
  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
