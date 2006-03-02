/*
 * compat-test.c:  tests svn_ver_compatible
 *
 * ====================================================================
 * Copyright (c) 2004 CollabNet.  All rights reserved.
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

#include "svn_error.h"
#include "svn_version.h"

#include "../svn_test.h"

static svn_error_t *
test_version_compatibility(const char **msg, 
                           svn_boolean_t msg_only,
                           svn_test_opts_t *opts,
                           apr_pool_t *pool)
{
  unsigned int i;

  struct version_pair {
    svn_version_t my_version;
    svn_version_t lib_version;
    svn_boolean_t result;
  } versions[] = {
    { {1, 0, 0, ""}, {1, 0, 0, ""}, TRUE },
    { {1, 0, 0, ""}, {2, 0, 0, ""}, FALSE },
    { {2, 0, 0, ""}, {1, 0, 0, ""}, FALSE },

    { {1, 0, 0, ""}, {1, 0, 1, ""}, TRUE },
    { {1, 0, 1, ""}, {1, 0, 0, ""}, TRUE },
    { {1, 0, 1, ""}, {1, 0, 1, ""}, TRUE },

    { {1, 0, 0, ""}, {1, 1, 0, ""}, TRUE },
    { {1, 0, 1, ""}, {1, 1, 0, ""}, TRUE },
    { {1, 0, 0, ""}, {1, 1, 1, ""}, TRUE },
    { {1, 1, 0, ""}, {1, 0, 0, ""}, FALSE },

    { {1, 0, 0, "dev"}, {1, 0, 0, "dev"}, TRUE },
    { {1, 0, 1, "dev"}, {1, 0, 1, "dev"}, TRUE },
    { {1, 1, 0, "dev"}, {1, 1, 0, "dev"}, TRUE },
    { {1, 1, 1, "dev"}, {1, 1, 1, "dev"}, TRUE },
    { {1, 0, 0, "dev"}, {1, 0, 1, "dev"}, FALSE },
    { {1, 0, 0, "dev"}, {1, 1, 0, "dev"}, FALSE },
    { {1, 0, 0, "cev"}, {1, 0, 0, "dev"}, FALSE },
    { {1, 0, 0, "eev"}, {1, 0, 0, "dev"}, FALSE },
    { {1, 0, 1, "dev"}, {1, 0, 0, "dev"}, FALSE },
    { {1, 1, 0, "dev"}, {1, 0, 0, "dev"}, FALSE },

    { {1, 0, 0, ""},    {1, 0, 0, "dev"}, FALSE },

    { {1, 0, 0, "dev"}, {1, 0, 0, ""}, FALSE },
    { {1, 0, 1, "dev"}, {1, 0, 0, ""}, TRUE },
    { {1, 1, 0, "dev"}, {1, 0, 0, ""}, FALSE },
    { {1, 1, 1, "dev"}, {1, 1, 0, ""}, TRUE },
    { {1, 1, 1, "dev"}, {1, 0, 0, ""}, FALSE },
    { {2, 0, 0, "dev"}, {1, 0, 0, ""}, FALSE },
    { {1, 0, 0, "dev"}, {2, 0, 0, ""}, FALSE },
  };

  *msg = "svn_ver_compatible";
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < sizeof(versions)/sizeof(versions[0]); ++i)
    {
      if (svn_ver_compatible(&versions[i].my_version,
                             &versions[i].lib_version) != versions[i].result)
        return svn_error_createf
          (SVN_ERR_TEST_FAILED, NULL,
           "svn_ver_compatible (%d.%d.%d(%s), %d.%d.%d(%s)) failed",
           versions[i].my_version.major,
           versions[i].my_version.minor,
           versions[i].my_version.patch,
           versions[i].my_version.tag,
           versions[i].lib_version.major,
           versions[i].lib_version.minor,
           versions[i].lib_version.patch,
           versions[i].lib_version.tag);
    }

  return SVN_NO_ERROR;
}

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_version_compatibility),
    SVN_TEST_NULL
  };
