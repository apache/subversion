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

      if (discover_changed_paths && (this_rev > 0))
        {
          const svn_delta_edit_fns_t *editor;
          svn_fs_root_t *base_root, *this_root;
          void *edit_baton;
          svn_repos_node_t *i, *j;
          svn_stringbuf_t *this_path = svn_stringbuf_create ("", subpool);

          /* ### dir_delta wants these as non-const stringbufs; does
             it actually need them to be that?  It might telescope
             them... */
          svn_stringbuf_t *base_top = svn_stringbuf_create ("", subpool);
          svn_stringbuf_t *this_top = svn_stringbuf_create ("", subpool);

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
                                        base_top,
                                        NULL,
                                        NULL,
                                        this_root,
                                        this_top,
                                        editor,
                                        edit_baton,
                                        0,
                                        1,
                                        subpool));

          for (i = svn_repos_node_from_baton (edit_baton); i; i = i->child)
            {
              for (j = i; j; j = j->sibling)
                {
                  svn_path_add_component_nts (this_path,
                                              i->name,
                                              svn_path_repos_style);

                  /* We register all differences except for
                     directories without prop mods, because those must
                     result from bubble-up and don't belong in a
                     change list. */
                  if (! ((j->kind == svn_node_dir) && (! j->prop_mod)))
                    {
                      /* ### this is kind of bogus, we're assuming we
                         know something about the path style.  But the
                         path library doesn't like a lone slash. */
                      char *p = apr_pstrcat (subpool,
                                             "/", this_path->data, NULL);
                      
                      apr_hash_set (changed_paths, p,
                                    this_path->len + 1, (void *) 1);
                    }

                  svn_path_remove_component (this_path, svn_path_repos_style);
                }
            }
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
