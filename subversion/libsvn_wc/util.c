/*
 * util.c:  general routines defying categorization; eventually I
 *          suspect they'll end up in libsvn_subr, but don't want to
 *          pollute that right now.  Note that nothing in here is
 *          specific to working copies.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include <assert.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include "svn_io.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "wc.h"   /* just for prototypes of things in this .c file */

#include "svn_private_config.h"


svn_error_t *
svn_wc__ensure_directory (const char *path, apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_error_t *err = svn_io_check_path (path, &kind, pool);

  if (err)
    return err;

  if (kind != svn_node_none && kind != svn_node_dir)
    {
      /* If got an error other than dir non-existence, then we can't
         ensure this directory's existence, so just return the error.
         Might happen if there's a file in the way, for example. */
      return svn_error_createf (APR_ENOTDIR, NULL,
                                _("'%s' is not a directory"),
                                svn_path_local_style (path, pool));
    }
  else if (kind == svn_node_none)
    {
      /* The dir doesn't exist, and it's our job to change that. */

      err = svn_io_dir_make (path, APR_OS_DEFAULT, pool);

      if (err && !APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* Tried to create the dir, and encountered some problem
             other than non-existence of intermediate dirs.  We can't
             ensure the desired directory's existence, so just return
             the error. */ 
          return err;
        }
      else if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        /* (redundant conditional and comment) */
        {
          /* Okay, so the problem is a missing intermediate
             directory.  We don't know which one, so we recursively
             back up one level and try again. */
          const char *shorter = svn_path_dirname (path, pool);

          /* Clear the error. */
          svn_error_clear (err);

          if (shorter[0] == '\0')
            {
              /* A weird and probably rare situation. */
              return svn_error_create (0, NULL,
                                       _("Unable to make any directories"));
            }
          else  /* We have a valid path, so recursively ensure it. */
            {
              err = svn_wc__ensure_directory (shorter, pool);
          
              if (err)
                return (err);
              else
                return svn_wc__ensure_directory (path, pool);
            }
        }

      if (err)
        return err;
    }
  else  /* No problem, the dir already existed, so just leave. */
    assert (kind == svn_node_dir);

  return SVN_NO_ERROR;
}

/* Return the library version number. */
const svn_version_t *
svn_wc_version (void)
{
  SVN_VERSION_BODY;
}

svn_wc_notify_t *
svn_wc_create_notify (const char *path, svn_wc_notify_action_t action,
                      apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_palloc (pool, sizeof (*ret));
  ret->path = path;
  ret->action = action;
  ret->kind = svn_node_unknown;
  ret->mime_type = NULL;
  ret->content_state = ret->prop_state = svn_wc_notify_state_unknown;
  ret->revision = SVN_INVALID_REVNUM;

  return ret;
}

svn_wc_notify_t *
svn_wc_dup_notify (const svn_wc_notify_t *notify, apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_palloc (pool, sizeof (*ret));

  memcpy (ret, notify, sizeof (*ret));

  if (ret->path)
    ret->path = apr_pstrdup (pool, ret->path);
  if (ret->mime_type)
    ret->mime_type = apr_pstrdup (pool, ret->mime_type);

  return ret;
}
 

void svn_wc__compat_call_notify_func (void *baton,
                                      const svn_wc_notify_t *n,
                                      apr_pool_t *pool)
{
  svn_wc__compat_notify_baton_t *nb = baton;

  if (nb->func)
    (*nb->func) (nb->baton, n->path, n->action, n->kind, n->mime_type,
                 n->content_state, n->prop_state, n->revision);
}
