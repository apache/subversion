/*
 * crop.c: Cropping the WC 
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

/* ==================================================================== */

#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_client.h"
#include "svn_error_codes.h"
#include "svn_path.h"
#include "entries.h"

#define SVN_ERR_IGNORE_LOCAL_MOD(expr)                           \
  do {                                                           \
    svn_error_t *svn_err__temp = (expr);                         \
    if (svn_err__temp->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)     \
      svn_error_clear(svn_err__temp);                            \
    else if (svn_err__temp)                                      \
      return svn_err__temp;                                      \
  } while (0)

/* Helper function that crops the children of the TARGET, under the constraint
 * of DEPTH. The TARGET itself should have a proper depth and will never be
 * cropped.
 */
svn_error_t *
crop_children(svn_wc_adm_access_t *adm_access,
              const char *dir_path,
              svn_depth_t depth,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_adm_access_t *dir_access;
//  const char *full_path;
  svn_wc_entry_t *dot_entry;
  apr_pool_t *subpool = svn_pool_create(pool), *iterpool;

//  full_path = svn_path_join(svn_wc_adm_access_path(adm_access),
//                            dir_path, subpool);
  SVN_ERR(svn_wc_adm_retrieve(&dir_access, adm_access, dir_path, subpool));
  SVN_ERR(svn_wc_entries_read(&entries, dir_access, TRUE, subpool));
  dot_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                           APR_HASH_KEY_STRING);

  /* Update the depth of target first, if needed*/
  if (dot_entry->depth > depth)
    {
      /* XXX: Do we need to restore the modified depth if the user cancel this
         operation?*/
      dot_entry->depth = depth;
      SVN_ERR(svn_wc__entries_write(entries, dir_access, subpool));
    }

  /* Looping over current directory's SVN entries: */
  iterpool = svn_pool_create(subpool);

  for (hi = apr_hash_first(subpool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      apr_ssize_t klen;
      svn_wc_entry_t *current_entry;
      svn_pool_clear(iterpool);

      /* Get the next entry */
      apr_hash_this(hi, &key, &klen, &val);
      if (! strcmp(key, SVN_WC_ENTRY_THIS_DIR))
        continue;

      current_entry = val;

      if (current_entry->kind == svn_node_file && depth == svn_depth_empty)
        {
          SVN_ERR_IGNORE_LOCAL_MOD
            (svn_wc_remove_from_revision_control(dir_access,
                                                 current_entry->name,
                                                 TRUE, /* destroy */
                                                 FALSE, /* instant error */
                                                 cancel_func,
                                                 cancel_baton,
                                                 iterpool));

        }
      /* XXX: What about svn_node_none & svn_node_unkown? Currently assume
         svn_node_dir*/
      else if (depth < svn_depth_immediates)
        {
          svn_wc_adm_access_t *child_access;
          SVN_ERR(svn_wc_adm_retrieve(&child_access, dir_access,
                                      current_entry->name, iterpool));

          SVN_ERR_IGNORE_LOCAL_MOD
            (svn_wc_remove_from_revision_control(child_access,
                                                 SVN_WC_ENTRY_THIS_DIR,
                                                 TRUE, /* destroy */
                                                 FALSE, /* instant error */
                                                 cancel_func,
                                                 cancel_baton,
                                                 iterpool));
        }
      else
        return crop_children(dir_access,
                             current_entry->name, 
                             svn_depth_empty, 
                             cancel_func, 
                             cancel_baton, 
                             iterpool);
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_crop_tree(svn_wc_adm_access_t *anchor,
                 const char *target,
                 svn_depth_t depth,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  /* Only make sense to when the depth is restrictive. 
     Currently does not support svn_depth_exclude*/
  if (!(depth > svn_depth_exclude && depth < svn_depth_infinity))
    return SVN_NO_ERROR;

  /* Only make sense to crop a dir target*/
  SVN_ERR(svn_wc_entry(&entry, target, anchor, FALSE, pool));
  if (entry->kind != svn_node_dir)
    return SVN_NO_ERROR;

  /* Check to see if the target itself should be cropped */
  if (depth == svn_depth_empty)
    {
      const svn_wc_entry_t *parent_entry;
      SVN_ERR(svn_wc_entry(&parent_entry,
                           svn_path_dirname(target, pool),
                           anchor, FALSE, pool));

      if (parent_entry && parent_entry->depth <= svn_depth_files)
        {
          svn_wc_adm_access_t *dir_access;
          /* Crop the target with the subtree alltogher if the parent does
             not want sub-directories */
          SVN_ERR(svn_wc_adm_retrieve(&dir_access, anchor, target, pool));
          SVN_ERR_IGNORE_LOCAL_MOD
            (svn_wc_remove_from_revision_control(dir_access,
                                                 SVN_WC_ENTRY_THIS_DIR,
                                                 TRUE, /* destroy */
                                                 FALSE, /* instant error */
                                                 cancel_func,
                                                 cancel_baton,
                                                 pool));
          return SVN_NO_ERROR;
        }
    }

  return crop_children(anchor, target, depth, cancel_func, cancel_baton, pool);
}
