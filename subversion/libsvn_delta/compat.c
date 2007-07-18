/*
 * compat.c :  Wrappers and callbacks for compatibility.
 *
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"


struct file_rev_handler_wrapper_baton {
  void *baton;
  svn_file_rev_handler_old_t handler;
};

/* This implements svn_repos_file_rev_handler2_t. */
static svn_error_t *
file_rev_handler_wrapper(void *baton,
                         const char *path,
                         svn_revnum_t rev,
                         apr_hash_t *rev_props,
                         svn_boolean_t result_of_merge,
                         svn_txdelta_window_handler_t *delta_handler,
                         void **delta_baton,
                         apr_array_header_t *prop_diffs,
                         apr_pool_t *pool)
{
  struct file_rev_handler_wrapper_baton *fwb = baton;

  if (fwb->handler)
    return fwb->handler(fwb->baton,
                        path,
                        rev,
                        rev_props,
                        delta_handler,
                        delta_baton,
                        prop_diffs,
                        pool);

  return SVN_NO_ERROR;
}

void
svn_compat_wrap_file_rev_handler(svn_file_rev_handler_t *handler2,
                                 void **handler2_baton,
                                 svn_file_rev_handler_old_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool)
{
  struct file_rev_handler_wrapper_baton *fwb = apr_palloc(pool, sizeof(*fwb));

  /* Set the user provided old format callback in the baton. */
  fwb->baton = handler_baton;
  fwb->handler = handler;

  *handler2_baton = fwb;
  *handler2 = file_rev_handler_wrapper;
}
