/*
 * Incomplete regression tests for the diff/diff3 library.
 *
 * ====================================================================
 * Copyright (c) 2003-2006, 2008 CollabNet.  All rights reserved.
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
#include "svn_pools.h"
#include "svn_utf.h"

/* Used to terminate lines in large multi-line string literals. */
#define NL APR_EOL_STR

/* Random number seed.  Yes, it's global, just pretend you can't see it. */
static apr_uint32_t diff_diff3_seed;

/* Return the value of the current random number seed, initializing it if
   necessary */
static apr_uint32_t
seed_val(void)
{
  static svn_boolean_t first = TRUE;

  if (first)
    {
      diff_diff3_seed = (apr_uint32_t) apr_time_now();
      first = FALSE;
    }

  return diff_diff3_seed;
}

/* Return a random number N such that MIN_VAL <= N <= MAX_VAL */
static apr_uint32_t
range_rand(apr_uint32_t min_val,
           apr_uint32_t max_val)
{
  apr_uint64_t diff = max_val - min_val;
  apr_uint64_t val = diff * svn_test_rand(&diff_diff3_seed);
  val /= 0xffffffff;
  return min_val + (apr_uint32_t) val;
}

/* Make a file that is between MIN_LINES and MAX_LINES lines long, with at
   most VAR_LINES distinct lines.  If BLOCK_LINES is non-zero then every
   other block of BLOCK_LINES lines will be identical, if BLOCK_LINES is
   zero all lines will have contents chosen at random.  If TRAILING_NEWLINE
   is TRUE then the file will have a trailing newline, if not then it wont. */
static svn_error_t *
make_random_file(const char *filename,
                 int min_lines,
                 int max_lines,
                 int var_lines,
                 int block_lines,
                 svn_boolean_t trailing_newline,
                 apr_pool_t *pool)
{
  apr_file_t *file;
  apr_status_t status;
  int num_lines;

  num_lines = range_rand(min_lines, max_lines);

  status = apr_file_open(&file, filename,
                         APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT,
                         pool);
  if (status)
    return svn_error_createf(status, NULL, "failed to open '%s'", filename);

  while (num_lines--)
    {
      int x;
      if (! (block_lines && (num_lines / block_lines % 2)))
        x = range_rand(1, var_lines);
      else
        x = 0;
      if (num_lines || trailing_newline)
        apr_file_printf(file, "line %d line %d line %d\n", x, x, x);
      else
        apr_file_printf(file, "line %d line %d line %d", x, x, x);
    }

  status = apr_file_close(file);
  if (status)
    return svn_error_createf(status, NULL, "failed to close '%s'", filename);

  return SVN_NO_ERROR;
}


/* Create a file called FILENAME containing CONTENTS */
static svn_error_t *
make_file(const char *filename,
          const char *contents,
          apr_pool_t *pool)
{
  apr_file_t *file;
  apr_status_t status;

  status = apr_file_open(&file, filename,
                         APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT,
                         pool);
  if (status)
    return svn_error_createf(status, NULL, "failed to open '%s'", filename);

  status = apr_file_write_full(file, contents, strlen(contents), NULL);
  if (status)
    return svn_error_createf(status, NULL, "failed to write '%s'", filename);

  status = apr_file_close(file);
  if (status)
    return svn_error_createf(status, NULL, "failed to close '%s'", filename);

  return SVN_NO_ERROR;
}


/* Create three files called FILENAME1, FILENAME2 and FILENAME3
   containing CONTENTS1, CONTENTS2 and CONTENTS3 respectively.  Run a
   three way merge to merge the difference between CONTENTS1 and
   CONTENTS2 into CONTENTS3, using OPTIONS, and verify that it results
   in EXPECTED.  The files FILENAME1, FILENAME2 and FILENAME3 will be
   deleted if the merge is successful, and preserved otherwise.  If
   the merge fails the merge output will be in a file called
   "merge-FILENAME1-FILENAME2-FILENAME3".  The conflict style STYLE is
   used. */
static svn_error_t *
three_way_merge(const char *filename1,
                const char *filename2,
                const char *filename3,
                const char *contents1,
                const char *contents2,
                const char *contents3,
                const char *expected,
                const svn_diff_file_options_t *options,
                svn_diff_conflict_display_style_t style,
                apr_pool_t *pool)
{
  svn_diff_t *diff;
  apr_file_t *output;
  svn_stream_t *ostream;
  apr_status_t status;
  svn_stringbuf_t *actual;
  char *merge_name = apr_psprintf(pool, "merge-%s-%s-%s",
                                  filename1, filename2, filename3);

  /* We have an EXPECTED string we can match, because we don't support
     any other combinations (yet) than the ones above. */
  svn_string_t *original = svn_string_create(contents1, pool);
  svn_string_t *modified = svn_string_create(contents2, pool);
  svn_string_t *latest = svn_string_create(contents3, pool);

  options = options ? options : svn_diff_file_options_create(pool);

  SVN_ERR(svn_diff_mem_string_diff3(&diff,
                                    original, modified, latest, options, pool));

  actual = svn_stringbuf_create("", pool);
  ostream = svn_stream_from_stringbuf(actual, pool);

  SVN_ERR(svn_diff_mem_string_output_merge2
          (ostream, diff, original, modified, latest,
           apr_psprintf(pool, "||||||| %s", filename1),
           apr_psprintf(pool, "<<<<<<< %s", filename2),
           apr_psprintf(pool, ">>>>>>> %s", filename3),
           NULL, /* separator */
           style, pool));

  SVN_ERR(svn_stream_close(ostream));
  if (strcmp(actual->data, expected) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Failed mem-diff, expected and actual "
                             "outputs differ.\nEXPECTED:\n%s\n"
                             "ACTUAL:\n%s\n", expected, actual->data);

  SVN_ERR(make_file(filename1, contents1, pool));
  SVN_ERR(make_file(filename2, contents2, pool));
  SVN_ERR(make_file(filename3, contents3, pool));

  SVN_ERR(svn_diff_file_diff3_2(&diff, filename1, filename2, filename3,
                                options, pool));
  status = apr_file_open(&output, merge_name,
                         APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT,
                         pool);
  if (status)
    return svn_error_createf(status, NULL, "failed to open '%s'", merge_name);

  ostream = svn_stream_from_aprfile(output, pool);
  SVN_ERR(svn_diff_file_output_merge2(ostream, diff,
                                      filename1, filename2, filename3,
                                      NULL, NULL, NULL, NULL,
                                      style,
                                      pool));
  SVN_ERR(svn_stream_close(ostream));
  status = apr_file_close(output);
  if (status)
    return svn_error_createf(status, NULL, "failed to close '%s'", merge_name);
  SVN_ERR(svn_stringbuf_from_file(&actual, merge_name, pool));
  if (strcmp(actual->data, expected))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "failed merging diff '%s' to '%s' into '%s'",
                             filename1, filename2, filename3);

  SVN_ERR(svn_io_remove_file(filename1, pool));
  if (strcmp(filename1, filename2))
    SVN_ERR(svn_io_remove_file(filename2, pool));
  if (strcmp(filename1, filename3) && strcmp(filename2, filename3))
    SVN_ERR(svn_io_remove_file(filename3, pool));
  SVN_ERR(svn_io_remove_file(merge_name, pool));

  return SVN_NO_ERROR;
}


/* Create two files called FILENAME1 and FILENAME2 containing
   CONTENTS1 and CONTENTS2 respectively.  Run a two way diff between
   CONTENTS1 and CONTENTS2, using OPTIONS, and verify that it results
   in EXPECTED.  Then run the trivial merges to update CONTENTS1 to
   CONTENTS2 and CONTENTS2 to CONTENTS1. The files FILENAME1,
   FILENAME2 and be deleted if the diff and merges are successful, and
   preserved otherwise.  If the diff fails the diff output will be in
   a file called "diff-FILENAME1-FILENAME2".  */
static svn_error_t *
two_way_diff(const char *filename1,
             const char *filename2,
             const char *contents1,
             const char *contents2,
             const char *expected,
             const svn_diff_file_options_t *options,
             apr_pool_t *pool)
{
  svn_diff_t *diff;
  apr_file_t *output;
  svn_stream_t *ostream;
  apr_status_t status;
  svn_stringbuf_t *actual;
  char *diff_name = apr_psprintf(pool, "diff-%s-%s", filename1, filename2);

  /* We have an EXPECTED string we can match, because we don't support
     any other combinations (yet) than the ones above. */
  svn_string_t *original = svn_string_create(contents1, pool);
  svn_string_t *modified = svn_string_create(contents2, pool);

  options = options ? options : svn_diff_file_options_create(pool);

  SVN_ERR(svn_diff_mem_string_diff(&diff, original, modified, options, pool));

  actual = svn_stringbuf_create("", pool);
  ostream = svn_stream_from_stringbuf(actual, pool);

  SVN_ERR(svn_diff_mem_string_output_unified(ostream, diff,
                                             filename1, filename2,
                                             SVN_APR_LOCALE_CHARSET,
                                             original, modified, pool));
  SVN_ERR(svn_stream_close(ostream));
  if (strcmp(actual->data, expected) != 0)
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "Failed mem-diff, expected and actual "
                             "outputs differ.\nEXPECTED:\n%s\n"
                             "ACTUAL:\n%s\n", expected, actual->data);

  SVN_ERR(make_file(filename1, contents1, pool));
  SVN_ERR(make_file(filename2, contents2, pool));

  /* Check that two-way diff between contents1 and contents2 produces
     expected output. */
  SVN_ERR(svn_diff_file_diff_2(&diff, filename1, filename2, options, pool));
  status = apr_file_open(&output, diff_name,
                         APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT,
                         pool);
  if (status)
    return svn_error_createf(status, NULL, "failed to open '%s'", diff_name);

  ostream = svn_stream_from_aprfile(output, pool);
  SVN_ERR(svn_diff_file_output_unified2(ostream, diff,
                                        filename1, filename2,
                                        filename1, filename2,
                                        SVN_APR_LOCALE_CHARSET, pool));
  SVN_ERR(svn_stream_close(ostream));
  status = apr_file_close(output);
  if (status)
    return svn_error_createf(status, NULL, "failed to close '%s'", diff_name);

  SVN_ERR(svn_stringbuf_from_file(&actual, diff_name, pool));
  if (strcmp(actual->data, expected))
    return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                             "failed comparing '%s' and '%s'",
                             filename1, filename2);

  /* May as well do the trivial merges while we are here */
  SVN_ERR(three_way_merge(filename1, filename2, filename1,
                          contents1, contents2, contents1, contents2, NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));
  SVN_ERR(three_way_merge(filename2, filename1, filename2,
                          contents2, contents1, contents2, contents1, NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(svn_io_remove_file(diff_name, pool));

  return SVN_NO_ERROR;
}

struct random_mod
{
  int index;  /* Zero based line number */
  int mod;    /* Type of mod: 0, 1, 2 (can be interpreted as you like just
                 do it consistently) */
};

/* Fill the SELECTED array of length NUM to select with randomly chosen
   values, ensuring that none of SELECTED.INDEX are duplicates and that all
   the SELECTED.INDEX values are less than NUM_LINES.  Also ensure that for
   each SELECTED.INDEX the three elements of LINES from SELECTED.INDEX-1 to
   SELECTED.INDEX+1 are unset.  Set all LINES[SELECTED.INDEX]. */
static void
select_lines(struct random_mod *selected,
             int num_to_select,
             svn_boolean_t *lines,
             int num_lines)
{
  int i;
  for (i = 0; i < num_to_select; ++i)
    {
      int j;
      for (;;)
        {
          j= range_rand(0, num_lines - 1);
          if (lines[j] /* already selected */
              ||
              (j > 0 && lines[j - 1]) /* previous selected */
              ||
              (j < num_lines - 1 && lines[j + 1])) /* next selected */
            continue; /* try again */
          break; /* got one */
        }
      selected[i].index = j;
      selected[i].mod = range_rand(0, 2);
      lines[j] = TRUE;
    }
}


/* Create a file called FILENAME where the contents are obtained by
   applying the modifications in MOD_LINES, of which there are NUM_MODS, to
   a theoretical pristine file of length NUM_LINES lines. */
static svn_error_t *
make_random_merge_file(const char *filename,
                       int num_lines,
                       struct random_mod *mod_lines,
                       int num_mods,
                       apr_pool_t *pool)
{
  apr_file_t *file;
  apr_status_t status;
  int i;

  status = apr_file_open(&file, filename,
                         APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT,
                         pool);
  if (status)
    return svn_error_createf(status, NULL, "failed to open '%s'", filename);

  for (i = 0; i < num_lines; ++i)
    {
      int j;
      for (j = 0; j < num_mods; ++j)
        if (mod_lines[j].index == i)
          break;

      if (j < num_mods)
        {
          switch (mod_lines[j].mod)
            {
            case 0:
              apr_file_printf(file, "replace line %d\n", i);
              break;
            case 1:
              apr_file_printf(file,
                              "added line %d\n"
                              "unmodified line %d\n"
                              "added line %d\n",
                              i, i, i);
              break;
            default:
              ; /* Delete the line */
            }
        }
      else
        {
          apr_file_printf(file, "unmodified line %d\n", i);
        }
    }

  status = apr_file_close(file);
  if (status)
    return svn_error_createf(status, NULL, "failed to close '%s'", filename);

  return SVN_NO_ERROR;
}


/* ========================================================================== */

static svn_error_t *
dump_core(const char **msg,
          svn_boolean_t msg_only,
          svn_test_opts_t *opts,
          apr_pool_t *pool)
{
  *msg = "these dump core";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(two_way_diff("foo1", "bar1",
                       "",
                       "",
                       "",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo2", "bar2",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "",

                       "--- foo2"         NL
                       "+++ bar2"         NL
                       "@@ -1,3 +0,0 @@"  NL
                       "-Aa\n"
                       "-Bb\n"
                       "-Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo3", "bar3",
                       "",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "--- foo3"         NL
                       "+++ bar3"         NL
                       "@@ -0,0 +1,3 @@"  NL
                       "+Aa\n"
                       "+Bb\n"
                       "+Cc\n",
                       NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_two_way_unified(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  svn_diff_file_options_t *diff_opts = svn_diff_file_options_create(pool);
  *msg = "2-way unified diff and trivial merge";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(two_way_diff("foo4", "bar4",
                       "Aa\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "--- foo4"       NL
                       "+++ bar4"       NL
                       "@@ -1 +1,3 @@"  NL
                       " Aa\n"
                       "+Bb\n"
                       "+Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo4b", "bar4b",
                       "Cc\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "--- foo4b"      NL
                       "+++ bar4b"      NL
                       "@@ -1 +1,3 @@"  NL
                       "+Aa\n"
                       "+Bb\n"
                       " Cc\n",
                       NULL, pool));

  diff_opts->ignore_eol_style = TRUE;
  SVN_ERR(two_way_diff("foo4c", "bar4c",
                       "Cc\n",

                       "Aa\r"
                       "Bb\r"
                       "Cc\r",

                       "--- foo4c"      NL
                       "+++ bar4c"      NL
                       "@@ -1 +1,3 @@"  NL
                       "+Aa\r"
                       "+Bb\r"
                       " Cc\n",
                       diff_opts, pool));
  diff_opts->ignore_eol_style = FALSE;

  SVN_ERR(two_way_diff("foo5", "bar5",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "Aa\n",

                       "--- foo5"       NL
                       "+++ bar5"       NL
                       "@@ -1,3 +1 @@"  NL
                       " Aa\n"
                       "-Bb\n"
                       "-Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo5b", "bar5b",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "Cc\n",

                       "--- foo5b"      NL
                       "+++ bar5b"      NL
                       "@@ -1,3 +1 @@"  NL
                       "-Aa\n"
                       "-Bb\n"
                       " Cc\n",
                       NULL, pool));

  diff_opts->ignore_eol_style = TRUE;
  SVN_ERR(two_way_diff("foo5c", "bar5c",
                       "Aa\r\n"
                       "Bb\r\n"
                       "Cc\r\n",

                       "Cc\n",

                       "--- foo5c"      NL
                       "+++ bar5c"      NL
                       "@@ -1,3 +1 @@"  NL
                       "-Aa\r\n"
                       "-Bb\r\n"
                       " Cc\r\n",
                       diff_opts, pool));


  SVN_ERR(two_way_diff("foo5d", "bar5d",
                       "Aa\r\n"
					   "\r\n"
                       "Bb\r\n"
					   "\r\n"
                       "Cc\r\n"
					   "\r\n",

                       "Aa\n"
					   "\n"
                       "Bb\n"
					   "\n"
                       "Cc\n"
					   "\n",

					   "",
                       diff_opts, pool));
  diff_opts->ignore_eol_style = FALSE;

  SVN_ERR(two_way_diff("foo6", "bar6",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo6b", "bar6b",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "Aa\n"
                       "Xx\n"
                       "Cc\n",

                       "--- foo6b"        NL
                       "+++ bar6b"        NL
                       "@@ -1,3 +1,3 @@"  NL
                       " Aa\n"
                       "-Bb\n"
                       "+Xx\n"
                       " Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo6c", "bar6c",
                       "Aa\r\n"
                       "Bb\r\n"
                       "Cc\r\n",

                       "Aa\r\n"
                       "Xx\r\n"
                       "Cc\r\n",

                       "--- foo6c"        NL
                       "+++ bar6c"        NL
                       "@@ -1,3 +1,3 @@"  NL
                       " Aa\r\n"
                       "-Bb\r\n"
                       "+Xx\r\n"
                       " Cc\r\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo6d", "bar6d",
                       "Aa\r"
                       "Bb\r"
                       "Cc\r",

                       "Aa\r"
                       "Xx\r"
                       "Cc\r",

                       "--- foo6d"        NL
                       "+++ bar6d"        NL
                       "@@ -1,3 +1,3 @@"  NL
                       " Aa\r"
                       "-Bb\r"
                       "+Xx\r"
                       " Cc\r",
                       NULL, pool));

  diff_opts->ignore_space = svn_diff_file_ignore_space_change;
  SVN_ERR(two_way_diff("foo6e", "bar6e",
                       " A a \n"
                       " B b \r"
                       " C c \r\n",

                       " A  a   \n"
                       "   B b  \r"
                       "    C    c    \r\n",

                       "",
                       diff_opts, pool));
  diff_opts->ignore_space = svn_diff_file_ignore_space_none;

  diff_opts->ignore_space = svn_diff_file_ignore_space_all;
  SVN_ERR(two_way_diff("foo6f", "bar6f",
                       "Aa\n"
                       "Bb\r"
                       "Cc\r\n",

                       " A  a   \n"
                       "   B b  \r"
                       "    C    c    \r\n",

                       "",
                       diff_opts, pool));
  diff_opts->ignore_space = svn_diff_file_ignore_space_none;

  diff_opts->ignore_space = svn_diff_file_ignore_space_all;
  diff_opts->ignore_eol_style = TRUE;
  SVN_ERR(two_way_diff("foo6f", "bar6f",
                       "Aa\n"
                       "Bb\r"
                       "Cc\r\n",

                       " A  a   \r"
                       "   B b  \r\n"
                       "    C    c    \n",

                       "",
                       diff_opts, pool));
  diff_opts->ignore_space = svn_diff_file_ignore_space_none;
  diff_opts->ignore_eol_style = FALSE;

  SVN_ERR(two_way_diff("foo7", "bar7",
                       "Aa\n",

                       "Bb\n",

                       "--- foo7"     NL
                       "+++ bar7"     NL
                       "@@ -1 +1 @@"  NL
                       "-Aa\n"
                       "+Bb\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo7a", "bar7a",
                       "Aa\n"
                       "Cc\n",

                       "Bb\n"
                       "Cc\n",

                       "--- foo7a"        NL
                       "+++ bar7a"        NL
                       "@@ -1,2 +1,2 @@"  NL
                       "-Aa\n"
                       "+Bb\n"
                       " Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo7b", "bar7b",
                       "Aa\r"
                       "Cc\n",

                       "Bb\n"
                       "Cc\n",

                       "--- foo7b"        NL
                       "+++ bar7b"        NL
                       "@@ -1,2 +1,2 @@"  NL
                       "-Aa\r"
                       "+Bb\n"
                       " Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo8", "bar8",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "Xx\n"
                       "Yy\n",

                       "--- foo8"         NL
                       "+++ bar8"         NL
                       "@@ -1,3 +1,2 @@"  NL
                       "-Aa\n"
                       "-Bb\n"
                       "-Cc\n"
                       "+Xx\n"
                       "+Yy\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo9", "bar9",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "Bb\n",

                       "--- foo9"       NL
                       "+++ bar9"       NL
                       "@@ -1,3 +1 @@"  NL
                       "-Aa\n"
                       " Bb\n"
                       "-Cc\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo10", "bar10",
                       "Aa\n"
                       "Bb\n"
                       "Cc",

                       "Aa\n"
                       "Xx\n"
                       "Yy\n",

                       "--- foo10"                     NL
                       "+++ bar10"                     NL
                       "@@ -1,3 +1,3 @@"               NL
                       " Aa\n"
                       "-Bb\n"
                       "-Cc"                           NL
                       "\\ No newline at end of file"  NL
                       "+Xx\n"
                       "+Yy\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo11", "bar11",
                       "Aa\n"
                       "Xx\n"
                       "Yy\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc",

                       "--- foo11"                     NL
                       "+++ bar11"                     NL
                       "@@ -1,3 +1,3 @@"               NL
                       " Aa\n"
                       "-Xx\n"
                       "-Yy\n"
                       "+Bb\n"
                       "+Cc"                           NL
                       "\\ No newline at end of file"  NL,
                       NULL, pool));

  SVN_ERR(two_way_diff("foo12", "bar12",
                       "Aa\n"
                       "Xx\n"
                       "Yy",

                       "Aa\n"
                       "Bb\n"
                       "Cc",

                       "--- foo12"                     NL
                       "+++ bar12"                     NL
                       "@@ -1,3 +1,3 @@"               NL
                       " Aa\n"
                       "-Xx\n"
                       "-Yy"                           NL
                       "\\ No newline at end of file"  NL
                       "+Bb\n"
                       "+Cc"                           NL
                       "\\ No newline at end of file"  NL,
                       NULL, pool));

  SVN_ERR(two_way_diff("foo13", "bar13",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Ff\n"
                       "Gg\n",

                       "Xx\n"
                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Ff\n"
                       "Gg\n"
                       "Yy\n",

                       "--- foo13"        NL
                       "+++ bar13"        NL
                       "@@ -1,3 +1,4 @@"  NL
                       "+Xx\n"
                       " Aa\n"
                       " Bb\n"
                       " Cc\n"
                       "@@ -5,3 +6,4 @@"  NL
                       " Ee\n"
                       " Ff\n"
                       " Gg\n"
                       "+Yy\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo14", "bar14",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Ff\n"
                       "Gg\n",

                       "Bb\n"
                       "Aa\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Gg\n"
                       "Ff\n",

                       "--- foo14"        NL
                       "+++ bar14"        NL
                       "@@ -1,7 +1,7 @@"  NL
                       "+Bb\n"
                       " Aa\n"
                       "-Bb\n"
                       " Cc\n"
                       " Dd\n"
                       " Ee\n"
                       "+Gg\n"
                       " Ff\n"
                       "-Gg\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo16", "bar16",
                       "Aa\n"
                       "\n"
                       "Cc\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n",

                       "--- foo16"        NL
                       "+++ bar16"        NL
                       "@@ -1,3 +1,3 @@"  NL
                       " Aa\n"
                       "-\n"
                       "+Bb\n"
                       " Cc\n",
                       NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_two_way_unified_suspect(const char **msg,
                             svn_boolean_t msg_only,
                             svn_test_opts_t *opts,
                             apr_pool_t *pool)
{
  *msg = "2-way unified diff where output is suspect";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(two_way_diff("foo15a", "bar15a",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Ff\n"
                       "Gg\n"
                       "Hh\n"
                       "Ii\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ff\n"
                       "Gg\n"
                       "Hh\n"
                       "Ii\n",

                       "--- foo15a"       NL
                       "+++ bar15a"       NL
                       "@@ -2,7 +2,6 @@"  NL
                       " Bb\n"
                       " Cc\n"
                       " Dd\n"
                       "-Ee\n"
                       " Ff\n"
                       " Gg\n"
                       " Hh\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo15b", "bar15b",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Ff\n"
                       "Gg\n"
                       "Hh\n"
                       "Ii\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Xx\n"
                       "Yy\n"
                       "Ff\n"
                       "Gg\n"
                       "Hh\n"
                       "Ii\n",

                       "--- foo15b"       NL
                       "+++ bar15b"       NL
                       "@@ -3,6 +3,8 @@"  NL
                       " Cc\n"
                       " Dd\n"
                       " Ee\n"
                       "+Xx\n"
                       "+Yy\n"
                       " Ff\n"
                       " Gg\n"
                       " Hh\n",
                       NULL, pool));

  SVN_ERR(two_way_diff("foo15c", "bar15c",
                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Ee\n"
                       "Ff\n"
                       "Gg\n"
                       "Hh\n"
                       "Ii\n",

                       "Aa\n"
                       "Bb\n"
                       "Cc\n"
                       "Dd\n"
                       "Xx\n"
                       "Yy\n"
                       "Ff\n"
                       "Gg\n"
                       "Hh\n"
                       "Ii\n",

                       "--- foo15c"       NL
                       "+++ bar15c"       NL
                       "@@ -2,7 +2,8 @@"  NL
                       " Bb\n"
                       " Cc\n"
                       " Dd\n"
                       "-Ee\n"
                       "+Xx\n"
                       "+Yy\n"
                       " Ff\n"
                       " Gg\n"
                       " Hh\n",
                       NULL, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_three_way_merge_no_overlap(const char **msg,
                                svn_boolean_t msg_only,
                                svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  svn_diff_file_options_t *diff_opts = svn_diff_file_options_create(pool);
  *msg = "3-way merge, non-overlapping changes";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(three_way_merge("zig1", "zag1", "zog1",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\n"
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n",

                          "Xx\n"
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig1a", "zag1a", "zog1a",
                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n",

                          "Xx\r\n"
                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n",

                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n"
                          "Yy\r\n",

                          "Xx\r\n"
                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n"
                          "Yy\r\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig1b", "zag1b", "zog1b",
                          "Aa\r"
                          "Bb\r"
                          "Cc\r",

                          "Xx\r"
                          "Aa\r"
                          "Bb\r"
                          "Cc\r",

                          "Aa\r"
                          "Bb\r"
                          "Cc\r"
                          "Yy\r",

                          "Xx\r"
                          "Aa\r"
                          "Bb\r"
                          "Cc\r"
                          "Yy\r",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  diff_opts->ignore_space = svn_diff_file_ignore_space_all;
  SVN_ERR(three_way_merge("zig1c", "zag1c", "zog1c",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "X x\n"
                          "A a\n"
                          "B b\n"
                          "C c\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n",

                          "X x\n"
                          "A a\n"
                          "B b\n"
                          "C c\n"
                          "Yy\n",
                          diff_opts,
                          svn_diff_conflict_display_modified_latest,
                          pool));
  diff_opts->ignore_space = svn_diff_file_ignore_space_none;

  SVN_ERR(three_way_merge("zig2", "zag2", "zog2",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\n"
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n",

                          "Aa\n"
                          "Bb\n"
                          "Zz\n"
                          "Cc\n",

                          "Xx\n"
                          "Aa\n"
                          "Bb\n"
                          "Zz\n"
                          "Cc\n"
                          "Yy\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig3a", "zag3a", "zog3a",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc",

                          "Xx\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\n"
                          "Bb\n"
                          "Cc",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig3b", "zag3b", "zog3b",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc",

                          "Xx\n"
                          "Bb\n"
                          "Cc",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  diff_opts->ignore_space = svn_diff_file_ignore_space_all;
  diff_opts->ignore_eol_style = TRUE;
  SVN_ERR(three_way_merge("zig2c", "zag2c", "zog2c",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          " Xx\r\n"
                          " Aa\r\n"
                          " Bb\r\n"
                          " Cc\r\n"
                          " Yy\r\n",

                          "Aa\n"
                          "Bb\n"
                          "Zz\n"
                          "Cc\n",

                          " Xx\r\n"
                          " Aa\r\n"
                          " Bb\r\n"
                          "Zz\n"
                          " Cc\r\n"
                          " Yy\r\n",
                          diff_opts,
                          svn_diff_conflict_display_modified_latest,
                          pool));
  diff_opts->ignore_space = svn_diff_file_ignore_space_none;
  diff_opts->ignore_eol_style = FALSE;

  SVN_ERR(three_way_merge("zig4", "zag4", "zog4",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "Gg\n"
                          "Hh\n"
                          "Ii\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "Yy\n"
                          "Zz\n"
                          "Hh\n"
                          "Ii\n",

                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "Gg\n"
                          "Hh\n"
                          "Ii\n",

                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "Yy\n"
                          "Zz\n"
                          "Hh\n"
                          "Ii\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig5", "zag5", "zog5",
                          "Aa\r\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\r\n"
                          "Aa\r\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\r\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\r\n",

                          "Xx\r\n"
                          "Aa\r\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\r\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig6", "zag6", "zog6",
                          "AaAaAaAaAaAa\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\n"
                          "Bb\n"
                          "Cc\n",

                          "AaAaAaAaAaAa\n"
                          "Bb\n"
                          "CcCcCcCcCcCc\n"
                          "Yy\n",

                          "Xx\n"
                          "Bb\n"
                          "CcCcCcCcCcCc\n"
                          "Yy\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("zig7", "zag7", "zog7",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  diff_opts->ignore_space = svn_diff_file_ignore_space_all;
  diff_opts->ignore_eol_style = FALSE;
  SVN_ERR(three_way_merge("zig8", "zag8", "zog8",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "   Aa\n"
                          "B b\n"
                          "C c\n",

                          "A a\n"
                          "Bb \n"
                          " Cc\n"
                          "New line in zog8\n",

                          "   Aa\n"
                          "B b\n"
                          "C c\n"
                          "New line in zog8\n",
                          diff_opts,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_three_way_merge_with_overlap(const char **msg,
                                  svn_boolean_t msg_only,
                                  svn_test_opts_t *opts,
                                  apr_pool_t *pool)
{
  *msg = "3-way merge, non-conflicting overlapping changes";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(three_way_merge("splish1", "splash1", "splosh1",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n",

                          "Aa\n"
                          "Xx\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n"
                          "Ee\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n"
                          "Ee\n"
                          "Zz\n",

                          "Aa\n"
                          "Xx\n"
                          "Bb\n"
                          "Cc\n"
                          "Yy\n"
                          "Ee\n"
                          "Zz\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("splish2", "splash2", "splosh2",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n",

                          "Aa\n"
                          "Yy\n"
                          "Zz\n"
                          "Dd\n"
                          "Pp\n"
                          "Qq\n"
                          "Ff\n",

                          "Pp\n"
                          "Qq\n"
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Pp\n"
                          "Qq\n"
                          "Ff\n"
                          "Pp\n"
                          "Qq\n",

                          "Pp\n"
                          "Qq\n"
                          "Aa\n"
                          "Yy\n"
                          "Zz\n"
                          "Dd\n"
                          "Pp\n"
                          "Qq\n"
                          "Ff\n"
                          "Pp\n"
                          "Qq\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("splish3", "splash3", "splosh3",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Xx\n"
                          "Aa\n"
                          "Bb\n"
                          "Cc",

                          "Aa\n"
                          "Xx\n"
                          "Bb\n"
                          "Cc",

                          "Xx\n"
                          "Aa\n"
                          "Xx\n"
                          "Bb\n"
                          "Cc",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("splish4", "splash4", "splosh4",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "Gg\n"
                          "Hh\n",

                          "Aa\n"
                          "Ff\n"
                          "Gg\n"
                          "Hh\n"
                          "Bb\n"
                          "Cc\n"
                          "Xx\n"
                          "Dd\n"
                          "Ee\n"
                          "Yy\n"
                          "Ff\n"
                          "Gg\n"
                          "Hh\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Xx\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "Gg\n"
                          "Zz\n"
                          "Hh\n",

                          "Aa\n"
                          "Ff\n"
                          "Gg\n"
                          "Hh\n"
                          "Bb\n"
                          "Cc\n"
                          "Xx\n"
                          "Dd\n"
                          "Ee\n"
                          "Yy\n"
                          "Ff\n"
                          "Gg\n"
                          "Zz\n"
                          "Hh\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_three_way_merge_with_conflict(const char **msg,
                                   svn_boolean_t msg_only,
                                   svn_test_opts_t *opts,
                                   apr_pool_t *pool)
{
  *msg = "3-way merge, conflicting overlapping changes";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(three_way_merge("dig1", "dug1", "dag1",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "",

                          "",

                          "",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("dig2", "dug2", "dag2",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n",

                          "",

                          "<<<<<<< dug2\n"
                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "=======\n"
                          ">>>>>>> dag2\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("dig2a", "dug2a", "dag2a",
                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n",

                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n"
                          "Dd\r\n"
                          "Ee\r\n"
                          "Ff\r\n",

                          "",

                          "<<<<<<< dug2a\r\n"
                          "Aa\r\n"
                          "Bb\r\n"
                          "Cc\r\n"
                          "Dd\r\n"
                          "Ee\r\n"
                          "Ff\r\n"
                          "=======\r\n"
                          ">>>>>>> dag2a\r\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("dig2b", "dug2b", "dag2b",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\r"
                          "Bb\r"
                          "Cc\r"
                          "Dd\r"
                          "Ee\r"
                          "Ff\r",

                          "",

                          "<<<<<<< dug2b\r"
                          "Aa\r"
                          "Bb\r"
                          "Cc\r"
                          "Dd\r"
                          "Ee\r"
                          "Ff\r"
                          "=======\r"
                          ">>>>>>> dag2b\r",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("dig3", "dug3", "dag3",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n",

                          "Aa\n"
                          "Bb\n",

                          "Aa\n"
                          "Bb\n"
                          "<<<<<<< dug3\n"
                          "Cc\n"
                          "Dd\n"
                          "Ee\n"
                          "Ff\n"
                          "=======\n"
                          ">>>>>>> dag3\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("dig4", "dug4", "dag4",
                          "Aa\n"
                          "Bb\n"
                          "Cc\n",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Dd",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "Ee",

                          "Aa\n"
                          "Bb\n"
                          "Cc\n"
                          "<<<<<<< dug4\n"
                          "Dd=======\n"
                          "Ee>>>>>>> dag4\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
test_three_way_merge_conflict_styles(const char **msg,
                                     svn_boolean_t msg_only,
                                     svn_test_opts_t *opts,
                                     apr_pool_t *pool)
{
  static const char *original =
    "a\n"
    "b\n"
    "c\n"
    "d\n"
    "e\n"
    "f\n"
    "g\n"
    "h\n"
    "i\n"
    "j\n"
    "k\n"
    "l\n"
    "m\n"
    "n\n"
    "o\n"
    "p\n"
    "q\n"
    "r\n"
    "s\n"
    "t\n"
    "u\n"
    "v\n"
    "w\n"
    "x\n"
    "y\n"
    "z\n"
    ;
  static const char *modified =
    "A\n"
    "b\n"
    "c\n"
    "d\n"
    "e\n"
    "f\n"
    "g\n"
    "h\n"
    "iMOD\n"
    "j\n"
    "k\n"
    "l\n"
    "m\n"
    "N\n"
    "O\n"
    "hello\n"
    "world\n"
    "yay\n"
    "P\n"
    "Q\n"
    "r\n"
    "s\n"
    "t\n"
    "u\n"
    "v\n"
    "w\n"
    "x\n"
    "y\n"
    "z\n"
    ;
  static const char *latest =
    "a\n"
    "b\n"
    "c\n"
    "d\n"
    "e\n"
    "f\n"
    "g\n"
    "h\n"
    "i\n"
    "j\n"
    "k1\n"
    "l2\n"
    "m3\n"
    "n4\n"
    "o5\n"
    "hello\n"
    "world\n"
    "yay\n"
    "p\n"
    "q\n"
    "r\n"
    "sLAT\n"
    "t\n"
    "u\n"
    "v\n"
    "w\n"
    "x\n"
    "y\n"
    "Z\n"
    ;
  /* So, 'modified' capitalized N through Q; 'latest' added numbers to
     'k' through 'o'; and they both inserted "hello world yay" in the
     middle.  Also, there are non-conflicting changes to the first and
     last lines. */

  *msg = "3-way merge with conflict styles";
  if (msg_only)
    return SVN_NO_ERROR;


  SVN_ERR(three_way_merge("style-normal1", "style-normal2", "style-normal3",
                          original, modified, latest,
                          "A\n"
                          "b\n"
                          "c\n"
                          "d\n"
                          "e\n"
                          "f\n"
                          "g\n"
                          "h\n"
                          "iMOD\n"
                          "j\n"
                          "<<<<<<< style-normal2\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "N\n"
                          "O\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "P\n"
                          "Q\n"
                          "=======\n"
                          "k1\n"
                          "l2\n"
                          "m3\n"
                          "n4\n"
                          "o5\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "p\n"
                          "q\n"
                          ">>>>>>> style-normal3\n"
                          "r\n"
                          "sLAT\n"
                          "t\n"
                          "u\n"
                          "v\n"
                          "w\n"
                          "x\n"
                          "y\n"
                          "Z\n",
                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("style-resolved1", "style-resolved2",
                          "style-resolved3",
                          original, modified, latest,
                          "A\n"
                          "b\n"
                          "c\n"
                          "d\n"
                          "e\n"
                          "f\n"
                          "g\n"
                          "h\n"
                          "iMOD\n"
                          "j\n"
                          "<<<<<<< style-resolved2\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "N\n"
                          "O\n"
                          "=======\n"
                          "k1\n"
                          "l2\n"
                          "m3\n"
                          "n4\n"
                          "o5\n"
                          ">>>>>>> style-resolved3\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "<<<<<<< style-resolved2\n"
                          "P\n"
                          "Q\n"
                          "=======\n"
                          "p\n"
                          "q\n"
                          ">>>>>>> style-resolved3\n"
                          "r\n"
                          "sLAT\n"
                          "t\n"
                          "u\n"
                          "v\n"
                          "w\n"
                          "x\n"
                          "y\n"
                          "Z\n",
                          NULL,
                          svn_diff_conflict_display_resolved_modified_latest,
                          pool));

  SVN_ERR(three_way_merge("style-three1", "style-three2", "style-three3",
                          original, modified, latest,
                          "A\n"
                          "b\n"
                          "c\n"
                          "d\n"
                          "e\n"
                          "f\n"
                          "g\n"
                          "h\n"
                          "iMOD\n"
                          "j\n"
                          "<<<<<<< style-three2\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "N\n"
                          "O\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "P\n"
                          "Q\n"
                          "||||||| style-three1\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "n\n"
                          "o\n"
                          "p\n"
                          "q\n"
                          "=======\n"
                          "k1\n"
                          "l2\n"
                          "m3\n"
                          "n4\n"
                          "o5\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "p\n"
                          "q\n"
                          ">>>>>>> style-three3\n"
                          "r\n"
                          "sLAT\n"
                          "t\n"
                          "u\n"
                          "v\n"
                          "w\n"
                          "x\n"
                          "y\n"
                          "Z\n",
                          NULL,
                          svn_diff_conflict_display_modified_original_latest,
                          pool));

  SVN_ERR(three_way_merge("style-only1", "style-only2", "style-only3",
                          original, modified, latest,
                          "@@\n"
                          "h\n"
                          "iMOD\n"
                          "j\n"
                          "<<<<<<< style-only2 (11,10)\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "N\n"
                          "O\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "P\n"
                          "Q\n"
                          "||||||| style-only1 (11,7)\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "n\n"
                          "o\n"
                          "p\n"
                          "q\n"
                          "=======\n"
                          "k1\n"
                          "l2\n"
                          "m3\n"
                          "n4\n"
                          "o5\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "p\n"
                          "q\n"
                          ">>>>>>> style-only3 (11,10)\n"
                          "r\n"
                          "sLAT\n"
                          "t\n",
                          NULL,
                          svn_diff_conflict_display_only_conflicts,
                          pool));

  SVN_ERR(three_way_merge("style-mod1", "style-mod2", "style-mod3",
                          original, modified, latest,
                          "A\n"
                          "b\n"
                          "c\n"
                          "d\n"
                          "e\n"
                          "f\n"
                          "g\n"
                          "h\n"
                          "iMOD\n"
                          "j\n"
                          "k\n"
                          "l\n"
                          "m\n"
                          "N\n"
                          "O\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "P\n"
                          "Q\n"
                          "r\n"
                          "sLAT\n"
                          "t\n"
                          "u\n"
                          "v\n"
                          "w\n"
                          "x\n"
                          "y\n"
                          "Z\n",
                          NULL,
                          svn_diff_conflict_display_modified,
                          pool));

  SVN_ERR(three_way_merge("style-latest1", "style-latest2", "style-latest3",
                          original, modified, latest,
                          "A\n"
                          "b\n"
                          "c\n"
                          "d\n"
                          "e\n"
                          "f\n"
                          "g\n"
                          "h\n"
                          "iMOD\n"
                          "j\n"
                          "k1\n"
                          "l2\n"
                          "m3\n"
                          "n4\n"
                          "o5\n"
                          "hello\n"
                          "world\n"
                          "yay\n"
                          "p\n"
                          "q\n"
                          "r\n"
                          "sLAT\n"
                          "t\n"
                          "u\n"
                          "v\n"
                          "w\n"
                          "x\n"
                          "y\n"
                          "Z\n",
                          NULL,
                          svn_diff_conflict_display_latest,
                          pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
random_trivial_merge(const char **msg,
                     svn_boolean_t msg_only,
                     svn_test_opts_t *opts,
                     apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

  *msg = apr_psprintf(pool, "random trivial merge (seed:%u)", seed_val());
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 5; ++i)
    {
      const char *filename1 = "trivial1";
      const char *filename2 = "trivial2";
      int min_lines = 1000;
      int max_lines = 1100;
      int var_lines = 50;
      int block_lines = 10;
      svn_stringbuf_t *contents1, *contents2;

      SVN_ERR(make_random_file(filename1,
                               min_lines, max_lines, var_lines, block_lines,
                               i % 3, subpool));
      SVN_ERR(make_random_file(filename2,
                               min_lines, max_lines, var_lines, block_lines,
                               i % 2, subpool));

      SVN_ERR(svn_stringbuf_from_file(&contents1, filename1, subpool));
      SVN_ERR(svn_stringbuf_from_file(&contents2, filename2, subpool));

      SVN_ERR(three_way_merge(filename1, filename2, filename1,
                              contents1->data, contents2->data,
                              contents1->data, contents2->data, NULL,
                              svn_diff_conflict_display_modified_latest,
                              subpool));
      SVN_ERR(three_way_merge(filename2, filename1, filename2,
                              contents2->data, contents1->data,
                              contents2->data, contents1->data, NULL,
                              svn_diff_conflict_display_modified_latest,
                              subpool));
      svn_pool_clear(subpool);
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* The "original" file has a number of distinct lines.  We generate two
   random modifications by selecting two subsets of the original lines and
   for each selected line either adding an additional line, replacing the
   line, or deleting the line.  The two subsets are chosen so that each
   selected line is distinct and no two selected lines are adjacent. This
   means the two sets of changes should merge without conflict.  */
static svn_error_t *
random_three_way_merge(const char **msg,
                       svn_boolean_t msg_only,
                       svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

  *msg = apr_psprintf(pool, "random 3-way merge (seed:%u)", seed_val());
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 20; ++i)
    {
      const char *filename1 = "original";
      const char *filename2 = "modified1";
      const char *filename3 = "modified2";
      const char *filename4 = "combined";
      svn_stringbuf_t *original, *modified1, *modified2, *combined;
      int num_lines = 100, num_src = 10, num_dst = 10;
      svn_boolean_t *lines = apr_pcalloc(subpool, sizeof(*lines) * num_lines);
      struct random_mod *src_lines = apr_palloc(subpool,
                                                sizeof(*src_lines) * num_src);
      struct random_mod *dst_lines = apr_palloc(subpool,
                                                sizeof(*dst_lines) * num_dst);
      struct random_mod *mrg_lines = apr_palloc(subpool,
                                                (sizeof(*mrg_lines)
                                                 * (num_src + num_dst)));

      select_lines(src_lines, num_src, lines, num_lines);
      select_lines(dst_lines, num_dst, lines, num_lines);
      memcpy(mrg_lines, src_lines, sizeof(*mrg_lines) * num_src);
      memcpy(mrg_lines + num_src, dst_lines, sizeof(*mrg_lines) * num_dst);

      SVN_ERR(make_random_merge_file(filename1, num_lines, NULL, 0, pool));
      SVN_ERR(make_random_merge_file(filename2, num_lines, src_lines, num_src,
                                     pool));
      SVN_ERR(make_random_merge_file(filename3, num_lines, dst_lines, num_dst,
                                     pool));
      SVN_ERR(make_random_merge_file(filename4, num_lines, mrg_lines,
                                     num_src + num_dst, pool));

      SVN_ERR(svn_stringbuf_from_file(&original, filename1, pool));
      SVN_ERR(svn_stringbuf_from_file(&modified1, filename2, pool));
      SVN_ERR(svn_stringbuf_from_file(&modified2, filename3, pool));
      SVN_ERR(svn_stringbuf_from_file(&combined, filename4, pool));

      SVN_ERR(three_way_merge(filename1, filename2, filename3,
                              original->data, modified1->data,
                              modified2->data, combined->data, NULL,
                              svn_diff_conflict_display_modified_latest,
                              subpool));
      SVN_ERR(three_way_merge(filename1, filename3, filename2,
                              original->data, modified2->data,
                              modified1->data, combined->data, NULL,
                              svn_diff_conflict_display_modified_latest,
                              subpool));

      SVN_ERR(svn_io_remove_file(filename4, pool));

      svn_pool_clear(subpool);
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* This is similar to random_three_way_merge above, except this time half
   of the original-to-modified1 changes are already present in modified2
   (or, equivalently, half the original-to-modified2 changes are already
   present in modified1).  Since the overlapping changes match exactly the
   merge should work without a conflict. */
static svn_error_t *
merge_with_part_already_present(const char **msg,
                                svn_boolean_t msg_only,
                                svn_test_opts_t *opts,
                                apr_pool_t *pool)
{
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);

  *msg = apr_psprintf(pool, "merge with part already present (seed:%u)",
                      seed_val());
  if (msg_only)
    return SVN_NO_ERROR;

  for (i = 0; i < 20; ++i)
    {
      const char *filename1 = "pap-original";
      const char *filename2 = "pap-modified1";
      const char *filename3 = "pap-modified2";
      const char *filename4 = "pap-combined";
      svn_stringbuf_t *original, *modified1, *modified2, *combined;
      int num_lines = 200, num_src = 20, num_dst = 20;
      svn_boolean_t *lines = apr_pcalloc(subpool, sizeof(*lines) * num_lines);
      struct random_mod *src_lines = apr_palloc(subpool,
                                                sizeof(*src_lines) * num_src);
      struct random_mod *dst_lines = apr_palloc(subpool,
                                                sizeof(*dst_lines) * num_dst);
      struct random_mod *mrg_lines = apr_palloc(subpool,
                                                (sizeof(*mrg_lines)
                                                 * (num_src + num_dst / 2)));

      select_lines(src_lines, num_src, lines, num_lines);
      /* Select half the destination changes at random */
      select_lines(dst_lines, num_dst / 2, lines, num_lines);
      /* Copy the other half from the source changes */
      memcpy(dst_lines + num_dst / 2, src_lines,
             sizeof(*dst_lines) * (num_dst - num_dst / 2));
      memcpy(mrg_lines, src_lines, sizeof(*mrg_lines) * num_src);
      memcpy(mrg_lines + num_src, dst_lines,
             sizeof(*mrg_lines) * num_dst / 2);

      SVN_ERR(make_random_merge_file(filename1, num_lines, NULL, 0, pool));
      SVN_ERR(make_random_merge_file(filename2, num_lines, src_lines, num_src,
                                     pool));
      SVN_ERR(make_random_merge_file(filename3, num_lines, dst_lines, num_dst,
                                     pool));
      SVN_ERR(make_random_merge_file(filename4, num_lines, mrg_lines,
                                     num_src + num_dst / 2, pool));

      SVN_ERR(svn_stringbuf_from_file(&original, filename1, pool));
      SVN_ERR(svn_stringbuf_from_file(&modified1, filename2, pool));
      SVN_ERR(svn_stringbuf_from_file(&modified2, filename3, pool));
      SVN_ERR(svn_stringbuf_from_file(&combined, filename4, pool));

      SVN_ERR(three_way_merge(filename1, filename2, filename3,
                              original->data, modified1->data,
                              modified2->data, combined->data, NULL,
                              svn_diff_conflict_display_modified_latest,
                              subpool));
      SVN_ERR(three_way_merge(filename1, filename3, filename2,
                              original->data, modified2->data,
                              modified1->data, combined->data, NULL,
                              svn_diff_conflict_display_modified_latest,
                              subpool));

      SVN_ERR(svn_io_remove_file(filename4, pool));

      svn_pool_clear(subpool);
    }
  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Merge is more "aggressive" about resolving conflicts than traditional
 * patch or diff3.  Some people consider this behaviour to be a bug, see
 * http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=35014
 */
static svn_error_t *
merge_adjacent_changes(const char **msg,
                       svn_boolean_t msg_only,
                       svn_test_opts_t *opts,
                       apr_pool_t *pool)
{
  *msg = "3-way merge, adjacent changes";
  if (msg_only)
    return SVN_NO_ERROR;

  SVN_ERR(three_way_merge("adj1", "adj2", "adj3",

                          "foo\n"
                          "bar\n"
                          "baz\n",

                          "foo\n"
                          "new_bar\n"
                          "baz\n",

                          "zig\n"
                          "foo\n"
                          "bar\n"
                          "new_baz\n",

                          "zig\n"
                          "foo\n"
                          "new_bar\n"
                          "new_baz\n",

                          NULL,
                          svn_diff_conflict_display_modified_latest,
                          pool));

  return SVN_NO_ERROR;
}



/* ========================================================================== */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS(dump_core),
    SVN_TEST_PASS(test_two_way_unified),
    SVN_TEST_PASS(test_two_way_unified_suspect),
    SVN_TEST_PASS(test_three_way_merge_no_overlap),
    SVN_TEST_PASS(test_three_way_merge_with_overlap),
    SVN_TEST_PASS(test_three_way_merge_with_conflict),
    SVN_TEST_PASS(random_trivial_merge),
    SVN_TEST_PASS(random_three_way_merge),
    SVN_TEST_PASS(merge_with_part_already_present),
    SVN_TEST_PASS(merge_adjacent_changes),
    SVN_TEST_PASS(test_three_way_merge_conflict_styles),
    SVN_TEST_NULL
  };
