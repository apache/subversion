/*
 * util.c:  FS utility routines
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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
#include "svn_fs.h"

svn_fs_path_change2_t *
svn_fs_path_change2_create(const svn_fs_id_t *node_rev_id, 
                           svn_fs_path_change_kind_t change_kind,
                           apr_pool_t *pool)
{
  svn_fs_path_change2_t *change;

  change = apr_pcalloc(pool, sizeof(*change));
  change->node_rev_id = node_rev_id;
  change->change_kind = change_kind;

  return change;
}
