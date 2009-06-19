/*
 * Regression tests for the diff/diff3 library -- parsing unidiffs
 *
 * ====================================================================
 * Copyright (c) 2003-2006, 2008-2009 CollabNet.  All rights reserved.
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


#include "../svn_test.h"

#include "svn_diff.h"
#include "private/svn_diff_private.h"
#include "svn_pools.h"
#include "svn_utf.h"

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR

static const char *unidiff = 
  "Index: A/mu (deleted)"                                               NL
  "===================================================================" NL
  "Index: A/C/gamma"                                                    NL
  "===================================================================" NL
  "--- A/C/gamma\t(revision 2)"                                         NL
  "+++ A/C/gamma\t(working copy)"                                       NL
  "@@ -1 +1,2 @@"                                                       NL
  " This is the file 'gamma'."                                          NL
  "+some more bytes to 'gamma'"                                         NL
  "Index: A/D/gamma"                                                    NL
  "===================================================================" NL
  "--- A/D/gamma.orig"                                                  NL
  "+++ A/D/gamma"                                                       NL
  "@@ -1 +1,2 @@"                                                       NL
  " This is the file 'gamma'."                                          NL
  "-some less bytes to 'gamma'"                                         NL
  ""                                                                    NL
  "Property changes on: mu-ng"                                          NL
  "___________________________________________________________________" NL
  "Name: newprop"                                                       NL
  "   + newpropval"                                                     NL
  "Name: svn:mergeinfo"                                                 NL
  ""                                                                    NL;

static svn_error_t *
test_parse_unidiff(apr_pool_t *pool)
{
  apr_file_t *patch_file;
  apr_status_t status;
  apr_size_t len;
  apr_off_t pos;
  const char *fname = "test_parse_unidiff.patch";
  svn_patch_t *patch;
  svn_hunk_t *hunk;
  svn_stringbuf_t *buf;
  svn_boolean_t eof;

  /* Create a patch file. */
  status = apr_file_open(&patch_file, fname,
                        (APR_READ | APR_WRITE | APR_CREATE | APR_TRUNCATE |
                         APR_DELONCLOSE), APR_OS_DEFAULT, pool);
  if (status != APR_SUCCESS)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL, "Cannot open '%s'",
                             fname);
  len = strlen(unidiff);
  status = apr_file_write_full(patch_file, unidiff, len, &len);
  if (status || len != strlen(unidiff))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Cannot write to '%s'", fname);

  /* Reset file pointer. */
  pos = 0;
  SVN_ERR(svn_io_file_seek(patch_file, APR_SET, &pos, pool));

  /* We have two patches with one hunk each.
   * Parse the first patch. */
  SVN_ERR(svn_diff__parse_next_patch(&patch, patch_file, NL, pool, pool));
  SVN_ERR_ASSERT(patch);
  SVN_ERR_ASSERT(! strcmp(patch->old_filename, "A/C/gamma"));
  SVN_ERR_ASSERT(! strcmp(patch->new_filename, "A/C/gamma"));
  SVN_ERR(svn_diff__parse_next_hunk(&hunk, patch, pool, pool));
  SVN_ERR_ASSERT(hunk);

  /* Make sure original text was parsed correctly. */
  SVN_ERR(svn_stream_readline(hunk->original_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(! eof);
  SVN_ERR_ASSERT(! strcmp(buf->data, " This is the file 'gamma'."));
  /* Now we should get EOF. */
  SVN_ERR(svn_stream_readline(hunk->original_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(eof);
  SVN_ERR_ASSERT(buf->len == 0);

  /* Make sure modified text was parsed correctly. */
  SVN_ERR(svn_stream_readline(hunk->modified_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(! eof);
  SVN_ERR_ASSERT(! strcmp(buf->data, " This is the file 'gamma'."));
  SVN_ERR(svn_stream_readline(hunk->modified_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(! eof);
  SVN_ERR_ASSERT(! strcmp(buf->data, "+some more bytes to 'gamma'"));
  /* Now we should get EOF. */
  SVN_ERR(svn_stream_readline(hunk->modified_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(eof);
  SVN_ERR_ASSERT(buf->len == 0);
   
  /* Parse the second patch. */
  SVN_ERR(svn_diff__parse_next_patch(&patch, patch_file, NL, pool, pool));
  SVN_ERR_ASSERT(patch);
  SVN_ERR_ASSERT(! strcmp(patch->old_filename, "A/D/gamma.orig"));
  SVN_ERR_ASSERT(! strcmp(patch->new_filename, "A/D/gamma"));
  SVN_ERR(svn_diff__parse_next_hunk(&hunk, patch, pool, pool));
  SVN_ERR_ASSERT(hunk);

  /* Make sure original text was parsed correctly. */
  SVN_ERR(svn_stream_readline(hunk->original_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(! eof);
  SVN_ERR_ASSERT(! strcmp(buf->data, " This is the file 'gamma'."));
  SVN_ERR(svn_stream_readline(hunk->original_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(! eof);
  SVN_ERR_ASSERT(! strcmp(buf->data, "-some less bytes to 'gamma'"));
  /* Now we should get EOF. */
  SVN_ERR(svn_stream_readline(hunk->original_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(eof);
  SVN_ERR_ASSERT(buf->len == 0);

  /* Make sure modified text was parsed correctly. */
  SVN_ERR(svn_stream_readline(hunk->modified_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(! eof);
  SVN_ERR_ASSERT(! strcmp(buf->data, " This is the file 'gamma'."));
  /* Now we should get EOF. */
  SVN_ERR(svn_stream_readline(hunk->modified_text, &buf, NL, &eof, pool));
  SVN_ERR_ASSERT(eof);
  SVN_ERR_ASSERT(buf->len == 0);

  return SVN_NO_ERROR;
}

/* ========================================================================== */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_parse_unidiff,
                   "test unidiff parsing"),
    SVN_TEST_NULL
  };
