/*
 * eol-test.c -- test the eol conversion subroutines
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <svn_io.h>
#include "svn_test.h"



/*** Helpers ***/

/* All the tests share the same test data. */
const char *lines[] =
  {
    "This is line 1 of the eol test data.",
    "This is line 2 of the eol test data.",
    "This is line 3 of the eol test data.",
    "This is line 4 of the eol test data.",
    "This is line 5 of the eol test data.",
    "This is line 6 of the eol test data.",
    "This is line 7 of the eol test data.",
    "This is line 8 of the eol test data.",
    "This is line 9 of the eol test data.",
    "This is line 10 of the eol test data.",
    "This is line 11 of the eol test data.",
    "This is line 12 of the eol test data.",
    "This is line 13 of the eol test data.",
    "This is line 14 of the eol test data.",
    "This is line 15 of the eol test data.",
    "This is line 16 of the eol test data.",
    "This is line 17 of the eol test data.",
    "This is line 18 of the eol test data.",
    "This is line 19 of the eol test data.",
    "This is line 20 of the eol test data.",
    "This is line 21 of the eol test data.",
    "This is line 22 of the eol test data.",
    "This is line 23 of the eol test data.",
    "This is line 24 of the eol test data.",
    "This is line 25 of the eol test data.",
    "This is line 26 of the eol test data.",
    "This is line 27 of the eol test data.",
    "This is line 28 of the eol test data.",
    "This is line 29 of the eol test data.",
    "This is line 30 of the eol test data.",
    "This is line 31 of the eol test data.",
    "This is line 32 of the eol test data."
  };


/* Return a randomly selected eol sequence. */
static const char *
random_eol_marker (void)
{
  /* Select a random eol marker from this set. */
  const char *eol_markers[] = { "\n", "\n\r", "\r\n", "\r" };
  static int seeded = 0;

  if (! seeded)
    {
      srand (1729);  /* we want errors to be reproducible */
      seeded = 1;
    }

  return eol_markers[rand()
                     % ((sizeof (eol_markers)) / (sizeof (*eol_markers)))];
}


/* Create FNAME with global `lines' as initial data.  Use EOL_STR as
 * the end-of-line marker between lines, or if EOL_STR is NULL, choose
 * a random marker at each opportunity.  Use POOL for any temporary
 * allocation.
 */
static svn_error_t *
create_file (const char *fname, const char *eol_str, apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *f;
  int i, j;

  apr_err = apr_file_open (&f, fname,
                           (APR_WRITE | APR_CREATE | APR_EXCL | APR_BINARY),
                           APR_OS_DEFAULT, pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_create (apr_err, 0, NULL, pool, fname);
  
  for (i = 0; i < (sizeof (lines) / sizeof (*lines)); i++)
    {
      const char *this_eol_str = eol_str ? eol_str : random_eol_marker ();
          
      apr_err = apr_file_printf (f, lines[i]);

      /* Is it overly paranoid to use putc(), because of worry about
         fprintf() doing a newline conversion? */ 
      for (j = 0; this_eol_str[j]; j++)
        {
          apr_err = apr_file_putc (this_eol_str[j], f);
          if (! APR_STATUS_IS_SUCCESS (apr_err))
            return svn_error_create (apr_err, 0, NULL, pool, fname);
        }
    }

  apr_err = apr_file_close (f);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return svn_error_create (apr_err, 0, NULL, pool, fname);
  
  return SVN_NO_ERROR;
}


/* Verify that file FNAME contains the eol test data and uses EOL_STR
 * as its eol marker consistently.  If the test data itself appears to
 * be wrong, return SVN_ERR_MALFORMED_FILE, or if the eol marker is
 * wrong return SVN_ERR_CORRUPT_EOL.  Otherwise, return SVN_NO_ERROR.
 * Use pool for any temporary allocation.
 */
static svn_error_t *
verify_file (const char *fname, const char *eol_str, apr_pool_t *pool)
{
  svn_stringbuf_t *contents;
  int idx = 0;
  int i;

  SVN_ERR (svn_string_from_file (&contents, fname, pool));

  for (i = 0; i < (sizeof (lines) / sizeof (*lines)); i++)
    {
      if (contents->len < idx)
        return svn_error_createf
          (SVN_ERR_MALFORMED_FILE, 0, NULL, pool, 
           "%s has short contents: \"%s\"", fname, contents->data);

      if (strncmp (contents->data + idx, lines[i], strlen (lines[i])) != 0)
        return svn_error_createf
          (SVN_ERR_MALFORMED_FILE, 0, NULL, pool, 
           "%s has wrong contents: \"%s\"", fname, contents->data + idx);

      /* else */

      idx += strlen (lines[i]);

      if (strncmp (contents->data + idx, eol_str, strlen (eol_str)) != 0)
        return svn_error_createf
          (SVN_ERR_IO_CORRUPT_EOL, 0, NULL, pool, 
           "%s has wrong eol: \"%s\"", fname, contents->data + idx);

      idx += strlen (eol_str);
    }

  return SVN_NO_ERROR;
}


/* Remove file FNAME if it exists; just return success if it doesn't. */
static svn_error_t *
remove_file (const char *fname, apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_finfo_t finfo;

  if (APR_STATUS_IS_SUCCESS (apr_stat (&finfo, fname, APR_FINFO_TYPE, pool)))
    {
      if (finfo.filetype == APR_REG)
        {
          apr_err = apr_file_remove (fname, pool);
          if (! APR_STATUS_IS_SUCCESS (apr_err))
            return svn_error_create (apr_err, 0, NULL, pool, fname);
        }
      else
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                  "non-file `%s' is in the way", fname);
    }

  return SVN_NO_ERROR;
}


/*** Tests ***/

static svn_error_t *
test_crlf_crlf (const char **msg,
                svn_boolean_t msg_only,
                apr_pool_t *pool)
{
  const char *src = "crlf_to_crlf.src";
  const char *dst = "crlf_to_crlf.dst";

  *msg = "convert CRLF to CRLF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, "\r\n", pool));
  SVN_ERR (verify_file (src, "\r\n", pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\r\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_lf_crlf (const char **msg,
              svn_boolean_t msg_only,
              apr_pool_t *pool)
{
  const char *src = "lf_to_crlf.src";
  const char *dst = "lf_to_crlf.dst";

  *msg = "convert LF to CRLF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, "\n", pool));
  SVN_ERR (verify_file (src, "\n", pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\r\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_cr_crlf (const char **msg,
              svn_boolean_t msg_only,
              apr_pool_t *pool)
{
  const char *src = "cr_to_crlf.src";
  const char *dst = "cr_to_crlf.dst";

  *msg = "convert CR to CRLF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, "\r", pool));
  SVN_ERR (verify_file (src, "\r", pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\r\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_mixed_to_crlf (const char **msg,
                     svn_boolean_t msg_only,
                     apr_pool_t *pool)
{
  const char *src = "mixed_to_crlf.src";
  const char *dst = "mixed_to_crlf.dst";

  *msg = "convert mixed line endings to CRLF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\r\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_lf_lf (const char **msg,
            svn_boolean_t msg_only,
            apr_pool_t *pool)
{
  const char *src = "lf_to_lf.src";
  const char *dst = "lf_to_lf.dst";

  *msg = "convert LF to LF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, "\n", pool));
  SVN_ERR (verify_file (src, "\n", pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_crlf_lf (const char **msg,
              svn_boolean_t msg_only,
              apr_pool_t *pool)
{
  const char *src = "crlf_to_lf.src";
  const char *dst = "crlf_to_lf.dst";

  *msg = "convert CRLF to LF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, "\r\n", pool));
  SVN_ERR (verify_file (src, "\r\n", pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_cr_lf (const char **msg,
            svn_boolean_t msg_only,
            apr_pool_t *pool)
{
  const char *src = "cr_to_lf.src";
  const char *dst = "cr_to_lf.dst";

  *msg = "convert CR to LF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, "\r", pool));
  SVN_ERR (verify_file (src, "\r", pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\n", pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_mixed_to_lf (const char **msg,
                   svn_boolean_t msg_only,
                   apr_pool_t *pool)
{
  const char *src = "mixed_to_lf.src";
  const char *dst = "mixed_to_lf.dst";

  *msg = "convert mixed line endings to LF";

  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR (remove_file (src, pool));
  SVN_ERR (create_file (src, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_file (dst, "\n", pool));

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  /* Conversions resulting in crlf. */
  test_crlf_crlf,
  test_lf_crlf,
  test_cr_crlf,
  test_mixed_to_crlf,
  /* Conversions resulting in lf. */
  test_lf_lf,
  test_crlf_lf,
  test_cr_lf,
  test_mixed_to_lf,
  /* ### Is there any compelling reason to test CR or LFCR? */
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */

