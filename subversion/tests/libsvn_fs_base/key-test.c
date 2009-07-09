/* key-test.c --- tests for the key gen functions
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
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
key_test(apr_pool_t *pool)
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
    SVN_TEST_PASS2(key_test,
                   "testing sequential alphanumeric key generation"),
    SVN_TEST_NULL
  };
