/*
 * svn_tests_editor.c:  a `dummy' editor implementation for testing
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

/* ==================================================================== */



#include <stdio.h>
#include "apr_pools.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"


svn_error_t *
dir_delta_get_editor (const svn_delta_edit_fns_t **editor,
                      void **edit_baton,
                      svn_fs_t *fs,
                      svn_fs_root_t *txn_root,
                      svn_stringbuf_t *path,
                      apr_pool_t *pool);



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
