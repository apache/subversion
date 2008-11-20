/* fs-pack-test.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>

#include "../svn_test.h"

#include "svn_pools.h"
#include "svn_fs.h"

#include "../svn_test_fs.h"


/*-----------------------------------------------------------------*/

/** The actual fs-tests called by `make check` **/

/* Create a filesystem.  */
static svn_error_t *
pack_filesystem(const char **msg,
                svn_boolean_t msg_only,
                svn_test_opts_t *opts,
                apr_pool_t *pool)
{
  svn_fs_t *fs;

  *msg = "pack a FSFS filesystem";

  if (msg_only)
    return SVN_NO_ERROR;

  /* XXX: Flesh out this (and other) tests. */
  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(pack_filesystem),
    SVN_TEST_NULL
  };
