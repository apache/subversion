/* log.c --- retrieving log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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


#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"



svn_error_t *
svn_repos_get_logs (svn_fs_t *fs,
                    apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  svn_revnum_t this_rev;
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *changed_paths = NULL;

  if (start == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&start, fs, pool));

  if (end == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&end, fs, pool));

  /* ### todo: ignoring `paths' for now.  Probably want to convert
     to a single hash containing absolute paths first.  */

  for (this_rev = start;
       ((start >= end) ? (this_rev >= end) : (this_rev <= end));
       ((start >= end) ? this_rev-- : this_rev++))
    {
      svn_string_t *author, *date, *message;

      SVN_ERR (svn_fs_revision_prop
               (&author, fs, this_rev, SVN_PROP_REVISION_AUTHOR, subpool));
      SVN_ERR (svn_fs_revision_prop
               (&date, fs, this_rev, SVN_PROP_REVISION_DATE, subpool));
      SVN_ERR (svn_fs_revision_prop
               (&message, fs, this_rev, SVN_PROP_REVISION_LOG, subpool));

      if (discover_changed_paths)
        {
          changed_paths = apr_hash_make (subpool);
          
#if 0
          svn_delta_edit_fns_t *editor;
          void *edit_baton;

          /* ### todo: not sure this needs an editor and dir_deltas.
             Might be easier to just walk around looking at
             created-rev fields... */

          SVN_ERR (svn_repos_node_editor (&editor, &edit_baton,
                                          svn_fs_t *fs,
                                          svn_fs_root_t *base_root,
                                          svn_fs_root_t *root,
                                          apr_pool_t *node_pool,
                                          apr_pool_t *pool));
          
          
          /* ### todo: everything we need for showing changed paths
             with "svn log -v" is done... except for the actual
             detection of the changed paths. :-)  Here, run
             dir_deltas() using the editor obtained above, traverse
             the node tree, calling apr_hash_set() when
             appropriate.

             But for now, just hardcode a bunch of paths.  If you
             don't want to see this, don't run "svn log -v". :-) */

#else /* 0/1 */

          apr_hash_set (changed_paths, "/not/implemented/yet",
                        APR_HASH_KEY_STRING, (void *) 1);
          apr_hash_set (changed_paths, "/not/implemented",
                        APR_HASH_KEY_STRING, (void *) 1);
          apr_hash_set (changed_paths, "/not/implemented/yet/i/really/mean/it",
                        APR_HASH_KEY_STRING, (void *) 1);
          apr_hash_set (changed_paths, "/this/is/just/a/placeholder",
                        APR_HASH_KEY_STRING, (void *) 1);
          apr_hash_set (changed_paths, "/blah",
                        APR_HASH_KEY_STRING, (void *) 1);
#endif /* 0 */
        }

      SVN_ERR ((*receiver) (receiver_baton,
                            changed_paths,
                            this_rev,
                            author ? author->data : "",
                            date ? date->data : "",
                            message ? message->data : "",
                            (this_rev == end)));
      
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
