/*
 * svn_test.h:  public interfaces for test programs
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

/* ==================================================================== */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_TEST_H
#define SVN_TEST_H

#include <apr_pools.h>
#include "svn_delta.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"



/* Retrieve a dummy editor that simply prints info to stdout.   This
   is implemented in tests-common/libsvn_test_editor.la */

svn_error_t *svn_test_get_editor (const svn_delta_edit_fns_t **editor,
                                  void **root_dir_baton,
                                  svn_string_t *path,
                                  svn_revnum_t revision,
                                  apr_pool_t *pool);
     
#endif /* SVN_TEST_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
