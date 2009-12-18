/*
 * checksum-test.c:  tests checksum functions.
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

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_version.h"

#include "../svn_test.h"

static svn_error_t *
test_checksum_parse(const char **msg,
                    svn_boolean_t msg_only,
                    svn_test_opts_t *opts,
                    apr_pool_t *pool)
{
  const char *md5_digest = "8518b76f7a45fe4de2d0955085b41f98";
  const char *sha1_digest = "74d82379bcc6771454377db03b912c2b62704139";
  const char *checksum_display;
  svn_checksum_t *checksum;

  *msg = "checksum parse";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_md5, md5_digest, pool));
  checksum_display = svn_checksum_to_cstring_display(checksum, pool);

  if (strcmp(checksum_display, md5_digest) != 0)
    return svn_error_createf
      (SVN_ERR_CHECKSUM_MISMATCH, NULL,
       "verify-checksum: md5 checksum mismatch:\n"
       "   expected:  %s\n"
       "     actual:  %s\n", md5_digest, checksum_display);

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1, sha1_digest,
                                 pool));
  checksum_display = svn_checksum_to_cstring_display(checksum, pool);

  if (strcmp(checksum_display, sha1_digest) != 0)
    return svn_error_createf
      (SVN_ERR_CHECKSUM_MISMATCH, NULL,
       "verify-checksum: sha1 checksum mismatch:\n"
       "   expected:  %s\n"
       "     actual:  %s\n", sha1_digest, checksum_display);

  return SVN_NO_ERROR;
}

/* An array of all test functions */
struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(test_checksum_parse),
    SVN_TEST_NULL
  };
