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
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"
#include "repos.h"




/* Store as keys in CHANGED the paths of all nodes at or below NODE
 * that show a significant change.  "Significant" means that the text
 * or properties of the node were changed, or that the node was added
 * or deleted.  
 *
 * The key is allocated in POOL; the value is (void *) 'A', 'D', or
 * 'R', for added, deleted, or opened, respectively.
 * 
 * Standard practice is to call this on the root node of delta tree
 * generated from svn_repos_dir_delta() and its node accessor,
 * svn_repos_node_from_baton(), with PATH representing "/".
 */
static void
detect_changed (apr_hash_t *changed,
                svn_repos_node_t *node,
                svn_stringbuf_t *path,
                apr_pool_t *pool)
{
  /* Recurse sideways first. */
  if (node->sibling)
    detect_changed (changed, node->sibling, path, pool);
    
  /* Then "enter" this node; but if its name is the empty string, then
     there's no need to extend path (and indeed, the behavior
     svn_path_add_component_nts is to strip the trailing slash even
     when the new path is "/", so we'd end up with "", which would
     screw everything up anyway). */ 
  if (node->name && *(node->name))
    {
      svn_path_add_component_nts (path, node->name);
    }

  /* Recurse downward before processing this node. */
  if (node->child)
    detect_changed (changed, node->child, path, pool);
    
  /* Process this node.
     We register all differences except for directory opens that don't
     involve any prop mods, because those are the result from
     bubble-up and don't belong in a change list. */
  if (! ((node->kind == svn_node_dir)
         && (node->action == 'R')
         && (! node->prop_mod)))
    {
      apr_hash_set (changed,
                    apr_pstrdup (pool, path->data), path->len,
                    (void *) ((int) node->action));
    }

  /* "Leave" this node. */
  svn_path_remove_component (path);

  /* ### todo: See issue #559.  This workaround is slated for
     demolition in the next ten microseconds. */
  if (path->len == 0)
    svn_stringbuf_appendcstr (path, "/");
}


svn_error_t *
svn_repos_get_logs (svn_repos_t *repos,
                    const apr_array_header_t *paths,
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
  svn_fs_t *fs = repos->fs;

  if (start == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&start, fs, pool));

  if (end == SVN_INVALID_REVNUM)
    SVN_ERR (svn_fs_youngest_rev (&end, fs, pool));

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

      /* ### Below, we discover changed paths if the user requested
         them (i.e., "svn log -v" means `discover_changed_paths' will
         be non-zero here), OR if we're filtering on paths, in which
         case we use changed_paths to determine whether or not to even
         invoke the log receiver on this log item.

         Note that there is another, more efficient way to filter by
         path.  Instead of looking at every revision, and eliminating
         those that didn't change any of the paths in question, you
         start at `start', and grab the fs node for every path in
         paths.  Check the created rev field; if any of them equal
         `start', include this log item.  Then jump immediately to the
         next highest/lowest created-rev field of any path in paths,
         and do the same thing, until you jump to a rev that's beyond
         `end'.  Premature optimization right now, however.
      */

#ifndef SVN_REPOS_ALLOW_LOG_WITH_PATHS
      discover_changed_paths = FALSE;
#else      
      if ((this_rev > 0) && 
          (discover_changed_paths || (paths && paths->nelts > 0)))
        {
          const svn_delta_edit_fns_t *editor;
          svn_fs_root_t *base_root, *this_root;
          void *edit_baton;

          changed_paths = apr_hash_make (subpool);
          
          SVN_ERR (svn_fs_revision_root (&base_root, fs, this_rev - 1, pool));
          SVN_ERR (svn_fs_revision_root (&this_root, fs, this_rev, pool));

          /* ### todo: not sure this needs an editor and dir_deltas.
             Might be easier to just walk the one revision tree,
             looking at created-rev fields... */

          SVN_ERR (svn_repos_node_editor (&editor, &edit_baton,
                                          fs,
                                          base_root,
                                          this_root,
                                          subpool,
                                          subpool));

          SVN_ERR (svn_repos_dir_delta (base_root,
                                        "",
                                        NULL,
                                        NULL,
                                        this_root,
                                        "",
                                        editor,
                                        edit_baton,
                                        0,
                                        1,
                                        subpool));

          detect_changed (changed_paths,
                          svn_repos_node_from_baton (edit_baton),
                          svn_stringbuf_create ("/", subpool),
                          subpool);
          /* ### Feels slightly bogus to assume "/" as the right start
             for repository style. */
        }

      /* Check if any of the filter paths changed in this revision. */
      if (paths && paths->nelts > 0)
        {
          int i;
          void *val;

          for (i = 0; i < paths->nelts; i++)
            {
              svn_stringbuf_t *this_path;
              this_path = (((svn_stringbuf_t **)(paths)->elts)[i]);
              val = apr_hash_get (changed_paths,
                                  this_path->data, this_path->len); 
              
              if (val)   /* Stop looking -- we've found a match. */
                break;
            }

          /* The check below happens outside the `for' loop
             immediately above, so that the `continue' will apply to
             the outermost `for' loop.  The logic is: if we are doing
             path filtering, and this revision *doesn't* affect one of
             the filter paths, then we skip the invocation of the log
             receiver and `continue' on to the next revision. */
          if (! val)
            continue;
        }
#endif /* SVN_REPOS_ALLOW_LOG_WITH_PATHS */

      SVN_ERR ((*receiver) (receiver_baton,
                            (discover_changed_paths ? changed_paths : NULL),
                            this_rev,
                            author ? author->data : "",
                            date ? date->data : "",
                            message ? message->data : ""));
      
      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
