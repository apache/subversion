/*
 * svnlook.h:  a repository inspection tool
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */



#include <stdio.h>
#include "apr_pools.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_fs.h"



/*** Node tree stuff ***/

typedef struct repos_node_t
{
  enum svn_node_kind kind;
  char action; /* 'A'dd, 'D'elete, 'R'eplace */
  svn_boolean_t text_mod;
  svn_boolean_t prop_mod;
  const char *name;
  struct repos_node_t *sibling;
  struct repos_node_t *child;
  
} repos_node_t;


repos_node_t *
svnlook_create_node (const char *name, 
                     apr_pool_t *pool);


repos_node_t *
svnlook_create_sibling_node (repos_node_t *elder, 
                             const char *name, 
                             apr_pool_t *pool);


repos_node_t *
svnlook_create_child_node (repos_node_t *parent, 
                           const char *name, 
                           apr_pool_t *pool);


repos_node_t *
svnlook_find_child_by_name (repos_node_t *parent, 
                            const char *name);




/*** Editor stuff ***/

svn_error_t *
svnlook_rev_changes_editor (const svn_delta_edit_fns_t **editor,
                            void **edit_baton,
                            svn_fs_t *fs,
                            svn_fs_root_t *root,
                            svn_fs_root_t *base_root,
                            apr_pool_t *pool);


svn_error_t *
svnlook_txn_changes_editor (const svn_delta_edit_fns_t **editor,
                            void **edit_baton,
                            svn_fs_t *fs,
                            svn_fs_root_t *root,
                            apr_pool_t *pool);

repos_node_t *
svnlook_edit_baton_tree (void *edit_baton);




/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
