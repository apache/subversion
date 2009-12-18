/*
 * tree-conflicts.h: Tree conflicts.
 *
 * ====================================================================
 * Copyright (c) 2007-2008 CollabNet.  All rights reserved.
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



#ifndef SVN_TREE_CONFLICTS_H
#define SVN_TREE_CONFLICTS_H

/*** Includes. ***/
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * Return in @a desc a possibly localized human readable
 * description of a tree conflict described by @a conflict.
 *
 * Allocate the result in @a pool.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_cl__get_human_readable_tree_conflict_description(
  const char **desc,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool);

/**
 * Append to @a str an XML representation of the tree conflict data
 * for @a conflict, in a format suitable for 'svn info --xml'.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_cl__append_tree_conflict_info_xml(
  svn_stringbuf_t *str,
  const svn_wc_conflict_description_t *conflict,
  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TREE_CONFLICTS_H */
