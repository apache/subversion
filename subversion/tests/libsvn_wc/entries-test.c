/*
 * db-test.c :  test the wc_db subsystem
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
#include <apr_general.h>

#include "svn_types.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"

#include "../svn_test.h"


#define WCROOT "entries-wc/root"

#define TEST_UUID "uuid"
#define TEST_URL "http://example.com/A"
#define TEST_REPOS "http://example.com/"


static svn_error_t *
test_entries(apr_pool_t *pool)
{
  SVN_ERR(svn_io_make_dir_recursively(WCROOT, pool));
  SVN_ERR(svn_wc_ensure_adm3(WCROOT, TEST_UUID, TEST_URL, TEST_REPOS, 1,
                             svn_depth_infinity, pool));

  return SVN_NO_ERROR;
}


struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_entries, "use the old entries interface"),
    SVN_TEST_NULL
  };
