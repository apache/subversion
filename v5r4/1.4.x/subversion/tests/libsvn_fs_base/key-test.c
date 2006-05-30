/* key-test.c --- tests for the key gen functions
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <apr.h>

#include "svn_error.h"

#include "../svn_test.h"
#include "../../libsvn_fs_base/key-gen.h"



static svn_error_t *
key_test(const char **msg, 
         svn_boolean_t msg_only,
         svn_test_opts_t *opts,
         apr_pool_t *pool)
{
  int i;
  const char *keys[9][2] = {
    { "0", "1" },
    { "9", "a" },
    { "zzzzz", "100000" },
    { "z000000zzzzzz", "z000001000000" },
    { "97hnq33jx2a", "97hnq33jx2b" },
    { "97hnq33jx2z", "97hnq33jx30" },
    { "999", "99a" },
    { "a9z", "aa0" },
    { "z", "10" }
  };

  *msg = "testing sequential alphanumeric key generation";

  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 9; i++)
    {
      char gen_key[MAX_KEY_SIZE];
      const char *orig_key = keys[i][0];
      const char *next_key = keys[i][1];
      apr_size_t len, olen;
  
      len = strlen(orig_key);
      olen = len;

      svn_fs_base__next_key(orig_key, &len, gen_key);
      if (! (((len == olen) || (len == (olen + 1)))
             && (strlen(next_key) == len)
             && (strcmp(next_key, gen_key) == 0)))
        {
          return svn_error_createf 
            (SVN_ERR_FS_GENERAL, NULL,
             "failed to increment key \"%s\" correctly\n"
             "  expected: %s\n"
             "    actual: %s",
             orig_key, next_key, gen_key);
        }
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(key_test),
    SVN_TEST_NULL
  };
