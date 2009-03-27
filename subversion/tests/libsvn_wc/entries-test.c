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
#define TEST_URL "http://example.com/repos/A"
#define TEST_REPOS "http://example.com/repos"


static svn_error_t *
set_up_wc(apr_pool_t *pool)
{
  const char *path;
  svn_wc_adm_access_t *adm_access;

  SVN_ERR(svn_io_remove_dir2(WCROOT, TRUE, NULL, NULL, pool));
  SVN_ERR(svn_io_make_dir_recursively(WCROOT, pool));
  SVN_ERR(svn_wc_ensure_adm3(WCROOT, TEST_UUID, TEST_URL, TEST_REPOS, 0,
                             svn_depth_infinity, pool));

  SVN_ERR(svn_wc_adm_open3(&adm_access, NULL, WCROOT, TRUE, -1,
                           NULL, NULL, pool));

  /* Create/add an "f1" child.  */
  path = svn_dirent_join(WCROOT, "f1", pool);
  SVN_ERR(svn_io_file_create(path, "root/f1 contents", pool));
  SVN_ERR(svn_wc_add3(path, adm_access, svn_depth_unknown, NULL,
                      SVN_INVALID_REVNUM, NULL, NULL, NULL, NULL, pool));


  /* All done. We're outta here...  */
  SVN_ERR(svn_wc_adm_close2(adm_access, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
test_entries(apr_pool_t *pool)
{
  SVN_ERR(set_up_wc(pool));

  /* ### now query it. make sure it all looks good.  */

  return SVN_NO_ERROR;
}


struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_entries, "use the old entries interface"),
    SVN_TEST_NULL
  };
