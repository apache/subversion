/* key-test.c --- tests for the key gen functions
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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <apr.h>

#include "svn_error.h"
#include "svn_test.h"
#include "../../libsvn_fs/key-gen.h"



static svn_error_t *
next_key (const char **msg, 
          svn_boolean_t msg_only,
          apr_pool_t *pool)
{
  apr_size_t len, olen;

  const char this_1[] = "0";
  const char expected_1[] = "1";
  char next_1[3];

  const char this_2[] = "9";
  const char expected_2[] = "a";
  char next_2[3];

  const char this_3[] = "zzzzz";
  const char expected_3[] = "100000";
  char next_3[7];

  const char this_4[] = "z000000zzzzzz";
  const char expected_4[] = "z000001000000";
  char next_4[15];

  const char this_5[] = "97hnq33jx2a";
  const char expected_5[] = "97hnq33jx2b";
  char next_5[13];

  const char this_6[] = "97hnq33jx2z";
  const char expected_6[] = "97hnq33jx30";
  char next_6[13];

  const char this_7[] = "999";
  const char expected_7[] = "99a";
  char next_7[5];

  const char this_8[] = "a9z";
  const char expected_8[] = "aa0";
  char next_8[5];

  const char this_9[] = "z";
  const char expected_9[] = "10";
  char next_9[4];


  *msg = "testing sequential alphanumeric key generation";

  if (msg_only)
    return SVN_NO_ERROR;

  len = strlen (this_1);
  olen = len;
  svn_fs__next_key (this_1, &len, next_1);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_1) == len)
         && (strcmp (next_1, expected_1) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_1);
    }
  
  len = strlen (this_2);
  olen = len;
  svn_fs__next_key (this_2, &len, next_2);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_2) == len)
         && (strcmp (next_2, expected_2) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_2);
    }

  len = strlen (this_3);
  olen = len;
  svn_fs__next_key (this_3, &len, next_3);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_3) == len)
         && (strcmp (next_3, expected_3) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_3);
    }

  len = strlen (this_4);
  olen = len;
  svn_fs__next_key (this_4, &len, next_4);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_4) == len)
         && (strcmp (next_4, expected_4) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_4);
    }

  len = strlen (this_5);
  olen = len;
  svn_fs__next_key (this_5, &len, next_5);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_5) == len)
         && (strcmp (next_5, expected_5) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_5);
    }

  len = strlen (this_6);
  olen = len;
  svn_fs__next_key (this_6, &len, next_6);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_6) == len)
         && (strcmp (next_6, expected_6) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_6);
    }

  len = strlen (this_7);
  olen = len;
  svn_fs__next_key (this_7, &len, next_7);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_7) == len)
         && (strcmp (next_7, expected_7) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_7);
    }

  len = strlen (this_8);
  olen = len;
  svn_fs__next_key (this_8, &len, next_8);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_8) == len)
         && (strcmp (next_8, expected_8) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_8);
    }

  len = strlen (this_9);
  olen = len;
  svn_fs__next_key (this_9, &len, next_9);
  if (! (((len == olen) || (len == (olen + 1)))
         && (strlen (next_9) == len)
         && (strcmp (next_9, expected_9) == 0)))
    {
      return svn_error_createf (SVN_ERR_FS_GENERAL, NULL,
                                "failed to increment key \"%s\" correctly",
                                this_9);
    }

  return SVN_NO_ERROR;
}


/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS (next_key),
    SVN_TEST_NULL
  };
