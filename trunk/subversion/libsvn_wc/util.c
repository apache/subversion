/*
 * util.c:  general routines defying categorization; eventually I
 *          suspect they'll end up in libsvn_subr, but don't want to
 *          pollute that right now.  Note that nothing in here is
 *          specific to working copies.
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



#include <assert.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_io.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "wc.h"   /* just for prototypes of things in this .c file */


svn_error_t *
svn_wc__ensure_directory (svn_stringbuf_t *path, apr_pool_t *pool)
{
  enum svn_node_kind kind;
  svn_stringbuf_t *npath = svn_stringbuf_dup (path, pool);
  svn_error_t *err = svn_io_check_path (npath, &kind, pool);

  if (err)
    return err;

  if (kind != svn_node_none && kind != svn_node_dir)
    {
      /* If got an error other than dir non-existence, then we can't
         ensure this directory's existence, so just return the error.
         Might happen if there's a file in the way, for example. */
      return svn_error_create (APR_ENOTDIR, 0, NULL, pool, npath->data);
    }
  else if (kind == svn_node_none)
    {
      /* The dir doesn't exist, and it's our job to change that. */

      apr_status_t apr_err =
        apr_dir_make (npath->data, APR_OS_DEFAULT, pool);

      if (apr_err && !APR_STATUS_IS_ENOENT(apr_err))
        {
          /* Tried to create the dir, and encountered some problem
             other than non-existence of intermediate dirs.  We can't
             ensure the desired directory's existence, so just return
             the error. */ 
          return svn_error_create (apr_err, 0, NULL, pool, npath->data);
        }
      else if (APR_STATUS_IS_ENOENT(apr_err))
        /* (redundant conditional and comment) */
        {
          /* Okay, so the problem is a missing intermediate
             directory.  We don't know which one, so we recursively
             back up one level and try again. */
          svn_stringbuf_t *shorter = svn_stringbuf_dup (npath, pool);
          svn_path_remove_component (shorter);

          if (svn_stringbuf_isempty (shorter))
            {
              /* A weird and probably rare situation. */
              return svn_error_create (0, 0, NULL, pool,
                                       "unable to make any directories");
            }
          else  /* We have a valid path, so recursively ensure it. */
            {
              err = svn_wc__ensure_directory (shorter, pool);
          
              if (err)
                return (err);
              else
                return svn_wc__ensure_directory (npath, pool);
            }
        }

      if (apr_err)
        return svn_error_create (apr_err, 0, NULL, pool, npath->data);
    }
  else  /* No problem, the dir already existed, so just leave. */
    assert (kind == svn_node_dir);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
