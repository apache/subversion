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
    "Line 1: fairly boring subst test data... blah blah",
    "Line 2: fairly boring subst test data... blah blah.",
    "Line 3: Valid $LastChangedRevision$, started unexpanded.",
    "Line 4: fairly boring subst test data... blah blah.",
    "Line 5: Valid $Rev$, started unexpanded.",
    "Line 6: fairly boring subst test data... blah blah.",
    "Line 7: fairly boring subst test data... blah blah.",
    "Line 8: Valid $LastChangedBy$, started unexpanded.",
    "Line 9: Valid $Author$, started unexpanded.",
    "Line 10: fairly boring subst test data... blah blah.",
    "Line 11: fairly boring subst test data... blah blah.",
    "Line 12: Valid $LastChangedDate$, started unexpanded.",
    "Line 13: Valid $Date$, started unexpanded.",
    "Line 14: fairly boring subst test data... blah blah.",
    "Line 15: fairly boring subst test data... blah blah.",
    "Line 16: Valid $HeadURL$, started unexpanded.",
    "Line 17: Valid $URL$, started unexpanded.",
    "Line 18: fairly boring subst test data... blah blah.",
    "Line 19: Invalid expanded keyword spanning two lines: $Author: ",
    /* The idea here is that, were it not broken across two lines,
       "$Author: Line 20: jrandom$" would be a valid if odd, keyword. */ 
    "Line 20: jrandom$ remainder of invalid keyword spanning two lines.",
    "Line 21: fairly boring subst test data... blah blah.",
    "Line 22: an unknown keyword $LastChangedSocks$.",
    "Line 23: fairly boring subst test data... blah blah.",
    /* In line 24, the third dollar sign terminates the first, and the
       fourth should therefore remain a literal dollar sign. */
    "Line 24: keyword in a keyword: $Author: $Date$ $",
    "Line 25: fairly boring subst test data... blah blah.",
    "Line 26: Emptily expanded keyword $Rev: $.",
    "Line 27: fairly boring subst test data... blah blah.",
    "Line 28: fairly boring subst test data... blah blah.",
    "Line 29: Valid $LastChangedRevision: 1729 $, started expanded.",
    "Line 30: Valid $Rev: 1729 $, started expanded.",
    "Line 31: fairly boring subst test data... blah blah.",
    "Line 32: fairly boring subst test data... blah blah."
    "Line 33: Valid $LastChangedDate: 2002-01-01 $, started expanded.",
    "Line 34: Valid $Date: 2002-01-01 $, started expanded.",
    "Line 35: fairly boring subst test data... blah blah.",
    "Line 36: fairly boring subst test data... blah blah.",
    "Line 37: Valid $LastChangedBy: jrandom $ , started expanded.",
    "Line 38: Valid $Author: jrandom $, started expanded.",
    "Line 39: fairly boring subst test data... blah blah.",
    "Line 40: fairly boring subst test data... blah blah.",
    "Line 41: Valid $HeadURL: http://tomato/mauve $, started expanded.",
    "Line 42: Valid $URL: http://tomato/mauve $, started expanded.",
    "Line 43: fairly boring subst test data... blah blah.",
    "Line 44: fairly boring subst test data... blah blah.",
    "Line 45: Invalid $LastChangedRevisionWithSuffix$, started unexpanded.",
    "Line 46: Invalid $Rev:$ is missing a space.",
    "Line 47: fairly boring subst test data... blah blah.",
    "Line 48: Two keywords back to back: $Author$$Rev$.",
    "Line 49: One keyword, one not, back to back: $Author$Rev$.",
    "Line 50: a series of dollar signs $$$$$$$$$$$$$$$$$$$$$$$$$$$$.",
    "Line 51: same, but with embedded keyword $$$$$$$$Date$$$$$$$$$$$.",
    "Line 52: same, with expanded, empty keyword $$$$$$Date: $$$$$$.",
    "Line 53: end of subst test data."
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


/* Verify that file FNAME contains the test data `lines' (with
 * keywords expanded according to REV, AUTHOR, DATE, and URL), and
 * uses EOL_STR as its eol marker consistently.  If the test data
 * itself appears to be wrong, return SVN_ERR_MALFORMED_FILE, or if
 * the eol marker is wrong return SVN_ERR_CORRUPT_EOL.  Otherwise,
 * return SVN_NO_ERROR.  Use pool for any temporary allocation.
 *
 * Note that, as with svn_io_copy_and_translate(), if any of REV,
 * AUTHOR, DATE, and/or URL is null, then that keyword is not
 * expanded.
 */
static svn_error_t *
verify_substitution (const char *fname,
                     const char *eol_str,
                     svn_revnum_t rev,
                     const char *date,
                     const char *author,
                     const char *url,
                     apr_pool_t *pool)
{
  svn_stringbuf_t *contents;
  int idx = 0;
  int i;

  const char *expect[(sizeof (lines) / sizeof (*lines))];

  for (i = 0; i < (sizeof (expect) / sizeof (*expect)); i++)
    expect[i] = lines[i];

  /* Certain lines contain keywords; expect their expansions. */
  if (SVN_IS_VALID_REVNUM (rev))
    {
      const char *rev_str = apr_psprintf (pool, "%ld", rev);

      expect[3 - 1] =
        apr_pstrcat (pool, "Line 3: ",
                     "Valid $LastChangedRevision: ",
                     rev_str,
                     " $, started unexpanded.",
                     NULL);
      expect[5 - 1] =
        apr_pstrcat (pool, "Line 5: ",
                     "Valid $Rev: ", rev_str, " $, started unexpanded.",
                     NULL);
      expect[26 - 1] =
        apr_pstrcat (pool, "Line 26: ",
                     "Emptily expanded keyword $Rev: ", rev_str," $.",
                     NULL);
      expect[29 - 1] =
        apr_pstrcat (pool, "Line 29: ",
                     "Valid $LastChangedRevision: ",
                     rev_str,
                     " $, started expanded.",
                     NULL);
    }

  if (date)
    {
      expect[12 - 1] =
        apr_pstrcat (pool, "Line 12: ",
                     "Valid $LastChangedDate: ",
                     date,
                     " $, started unexpanded.",
                     NULL);
      expect[13 - 1] =
        apr_pstrcat (pool, "Line 13: ",
                     "Valid $Date: ", date, " $, started unexpanded.",
                     NULL);
      expect[33 - 1] =
        apr_pstrcat (pool, "Line 33: ",
                     "Valid $LastChangedDate: ", date, " $, started expanded.",
                     NULL);
      expect[34 - 1] =
        apr_pstrcat (pool, "Line 34: ",
                     "Valid $Date: ", date, " $, started expanded.",
                     NULL);
      expect[51 - 1] =
        apr_pstrcat (pool, "Line 51: ",
                     "same, but with embedded keyword ",
                     "$$$$$$$$Date: ", date, "$$$$$$$$$$$.",
                     NULL);
      expect[52 - 1] =
        apr_pstrcat (pool, "Line 52: ",
                     "same, with expanded, empty keyword ",
                     "$$$$$$Date: ", date, " $$$$$$.",
                     NULL);
    }

  if (author)
    {
      expect[8 - 1] =
        apr_pstrcat (pool, "Line 8: ",
                     "Valid $LastChangedBy: ",
                     author,
                     " $, started unexpanded.",
                     NULL);
      expect[9 - 1] =
        apr_pstrcat (pool, "Line 9: ",
                     "Valid $Author: ", author, " $, started unexpanded.",
                     NULL);
      expect[24 - 1] =
        apr_pstrcat (pool, "Line 24: ",
                     "keyword in a keyword: $Author: ", author, " $Date$ $",
                     NULL);
      expect[37 - 1] =
        apr_pstrcat (pool, "Line 37: ",
                     "Valid $LastChangedBy: ",
                     author,
                     " $ , started expanded.",
                     NULL);
      expect[38 - 1] =
        apr_pstrcat (pool, "Line 38: ",
                     "Valid $Author: ", author, " $, started expanded.",
                     NULL);
      expect[49 - 1] =
        apr_pstrcat (pool, "Line 49: ",
                     "One keyword, one not, back to back: "
                     "$Author: ", author, " $Rev$.",
                     NULL);
    }

  if (url)
    {
      expect[16 - 1] =
        apr_pstrcat (pool, "Line 16: ",
                     "Valid $HeadURL: ", url, " $, started unexpanded.",
                     NULL);
      expect[17 - 1] =
        apr_pstrcat (pool, "Line 17: ",
                     "Valid $URL: ", url, " $, started unexpanded.",
                     NULL);
      expect[41 - 1] =
        apr_pstrcat (pool, "Line 41: ",
                     "Valid $HeadURL: ", url, " $, started expanded.",
                     NULL);
      expect[42 - 1] =
        apr_pstrcat (pool, "Line 42: ",
                     "Valid $URL: ", url, " $, started expanded.",
                     NULL);
    }

  /* Handle line 48 specially, as it contains two valid keywords. */
  if ((SVN_IS_VALID_REVNUM (rev)) && author)
    {
      const char *rev_str = apr_psprintf (pool, "%ld", rev);

      expect[48 - 1] =
        apr_pstrcat (pool, "Line 48: ",
                     "Two keywords back to back: "
                     "$Author: ", author, " $"
                     "$Rev: ", rev_str, " $.",
                     NULL);
    }
  else if ((SVN_IS_VALID_REVNUM (rev)) && (! author))
    {
      const char *rev_str = apr_psprintf (pool, "%ld", rev);

      expect[48 - 1] =
        apr_pstrcat (pool, "Line 48: ",
                     "Two keywords back to back: "
                     "$Author$$Rev: ", rev_str, " $.",
                     NULL);
    }
  else if ((! SVN_IS_VALID_REVNUM (rev)) && author)
    {
      expect[48 - 1] =
        apr_pstrcat (pool, "Line 48: ",
                     "Two keywords back to back: "
                     "$Author: ", author, " $$Rev$.",
                     NULL);
    }
  /* Else neither rev nor author, so line 48 remains unchanged. */


  /** Ready to verify. **/

  SVN_ERR (svn_string_from_file (&contents, fname, pool));

  for (i = 0; i < (sizeof (expect) / sizeof (*expect)); i++)
    {
      if (contents->len < idx)
        return svn_error_createf
          (SVN_ERR_MALFORMED_FILE, 0, NULL, pool, 
           "%s has short contents: \"%s\"", fname, contents->data);

      if (strncmp (contents->data + idx, expect[i], strlen (expect[i])) != 0)
        return svn_error_createf
          (SVN_ERR_MALFORMED_FILE, 0, NULL, pool, 
           "%s has wrong contents: \"%s\"", fname, contents->data + idx);

      /* else */

      idx += strlen (expect[i]);

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
crlf_to_crlf (const char **msg,
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
  SVN_ERR (verify_substitution
           (src, "\r\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_substitution
           (dst, "\r\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
lf_to_crlf (const char **msg,
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
  SVN_ERR (verify_substitution
           (src, "\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_substitution
           (dst, "\r\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
cr_to_crlf (const char **msg,
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
  SVN_ERR (verify_substitution
           (src, "\r", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\r\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_substitution
           (dst, "\r\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
mixed_to_crlf (const char **msg,
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
  SVN_ERR (verify_substitution
           (dst, "\r\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
lf_to_lf (const char **msg,
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
  SVN_ERR (verify_substitution
           (src, "\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_substitution
           (dst, "\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
crlf_to_lf (const char **msg,
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
  SVN_ERR (verify_substitution
           (src, "\r\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_substitution
           (dst, "\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
cr_to_lf (const char **msg,
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
  SVN_ERR (verify_substitution
           (src, "\r", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));
  SVN_ERR (svn_io_copy_and_translate
           (src, dst, "\n", 0, NULL, NULL, NULL, NULL, pool));
  SVN_ERR (verify_substitution
           (dst, "\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
mixed_to_lf (const char **msg,
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
  SVN_ERR (verify_substitution
           (dst, "\n", SVN_INVALID_REVNUM, NULL, NULL, NULL, pool));

  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  /* Conversions resulting in crlf. */
  crlf_to_crlf,
  lf_to_crlf,
  cr_to_crlf,
  mixed_to_crlf,
  /* Conversions resulting in lf. */
  lf_to_lf,
  crlf_to_lf,
  cr_to_lf,
  mixed_to_lf,
  /* ### Is there any compelling reason to test CR or LFCR? */
  0
};



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */

