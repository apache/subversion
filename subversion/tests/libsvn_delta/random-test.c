/*
 * random-test.c:  Test delta generation and application using random data.
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


#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "apr_general.h"
#include "apr_getopt.h"
#include "svn_delta.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "../svn_tests.h"

#include "../../libsvn_delta/delta.h"
#include "delta-window-test.h"


#define DEFAULT_ITERATIONS 30
#define DEFAULT_MAXLEN (100 * 1024)
#define DEFAULT_DUMP_FILES 0
#define DEFAULT_PRINT_WINDOWS 0
#define SEEDS 50
#define MAXSEQ 100


/* Initialize parameters for the random tests. */
extern int test_argc;
extern const char **test_argv;

static void init_params (apr_uint32_t *seed,
                         apr_uint32_t *maxlen, int *iterations,
                         int *dump_files, int *print_windows,
                         const char **random_bytes,
                         apr_uint32_t *bytes_range,
                         apr_pool_t *pool)
{
  apr_getopt_t *opt;
  char optch;
  const char *opt_arg;
  apr_status_t status;

  *seed = (apr_uint32_t) apr_time_now();
  *maxlen = DEFAULT_MAXLEN;
  *iterations = DEFAULT_ITERATIONS;
  *dump_files = DEFAULT_DUMP_FILES;
  *print_windows = DEFAULT_PRINT_WINDOWS;
  *random_bytes = NULL;
  *bytes_range = 256;

  apr_getopt_init (&opt, pool, test_argc, test_argv);
  while (APR_SUCCESS
         == (status = apr_getopt (opt, "s:l:n:r:FW", &optch, &opt_arg)))
    {
      switch (optch)
        {
        case 's':
          *seed = atol (opt_arg);
          break;
        case 'l':
          *maxlen = atoi (opt_arg);
          break;
        case 'n':
          *iterations = atoi (opt_arg);
          break;
        case 'r':
          *random_bytes = opt_arg + 1;
          *bytes_range = strlen (*random_bytes);
          break;
        case 'F':
          *dump_files = !*dump_files;
          break;
        case 'W':
          *print_windows = !*print_windows;
          break;
        }
    }
}


/* Generate a temporary file containing sort-of random data.  Diffs
   between files of random data tend to be pretty boring, so we try to
   make sure there are a bunch of common substrings between two runs
   of this function with the same seedbase.  */
static FILE *
generate_random_file (apr_uint32_t maxlen,
                      apr_uint32_t subseed_base,
                      apr_uint32_t *seed,
                      const char *random_bytes,
                      apr_uint32_t bytes_range,
                      int dump_files)
{
  apr_uint32_t len, seqlen;
  FILE *fp;
  unsigned long r;

  fp = tmpfile ();
  assert (fp != NULL);
  len = svn_test_rand (seed) % maxlen; /* We might go over this by a bit.  */
  while (len > 0)
    {
      /* Generate a pseudo-random sequence of up to MAXSEQ bytes,
         where the seed is in the range [seedbase..seedbase+MAXSEQ-1].
         (Use our own pseudo-random number generator here to avoid
         clobbering the seed of the libc random number generator.)  */
      seqlen = svn_test_rand (seed) % MAXSEQ;
      if (seqlen > len) seqlen = len;
      len -= seqlen;
      r = subseed_base + svn_test_rand (seed) % SEEDS;
      while (seqlen-- > 0)
        {
          const int ch = (random_bytes
                          ? random_bytes[r % bytes_range]
                          : r % bytes_range);
          putc (ch, fp);
          r = r * 1103515245 + 12345;
        }
    }
  rewind (fp);

  if (dump_files)
    {
      int ch;
      fputs ("--------\n", stdout);
      while (EOF != (ch = getc (fp)))
        putc (ch, stdout);
      putc ('\n', stdout);
      rewind (fp);
    }

  return fp;
}

/* Compare two open files. The file positions may change. */
static svn_error_t *
compare_files (FILE *f1, FILE *f2, int dump_files, apr_pool_t *pool)
{
  int c1, c2;
  apr_off_t pos = 0;

  rewind (f1);
  rewind (f2);

  if (dump_files)
    {
      int ch;
      fputs ("--------\n", stdout);
      while (EOF != (ch = getc (f2)))
        putc (ch, stdout);
      putc ('\n', stdout);
      rewind (f2);
    }

  for (;;)
    {
      c1 = getc (f1);
      c2 = getc (f2);
      ++pos;
      if (c1 == EOF && c2 == EOF)
        break;
      if (c1 != c2)
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                  "mismatch at position %"APR_OFF_T_FMT,
                                  pos);
    }
  return SVN_NO_ERROR;
}


static FILE *
copy_tempfile (FILE *fp)
{
  FILE *newfp;
  int c;

  newfp = tmpfile ();
  assert (newfp != NULL);
  rewind(fp);
  while ((c = getc (fp)) != EOF)
    putc (c, newfp);
  rewind(fp);
  rewind (newfp);
  return newfp;
}



static svn_error_t *
random_test (const char **msg,
             svn_boolean_t msg_only,
             apr_pool_t *pool)
{
  static char msg_buff[256];

  apr_uint32_t seed, bytes_range, maxlen;
  int i, iterations, dump_files, print_windows;
  const char *random_bytes;

  /* Initialize parameters and print out the seed in case we dump core
     or something. */
  init_params(&seed, &maxlen, &iterations, &dump_files, &print_windows,
              &random_bytes, &bytes_range, pool);
  sprintf(msg_buff, "random delta test, seed = %lu", (unsigned long) seed);
  *msg = msg_buff;

  if (msg_only)
    return SVN_NO_ERROR;
  else
    printf("SEED: %s\n", msg_buff);

  for (i = 0; i < iterations; i++)
    {
      /* Generate source and target for the delta and its application.  */
      apr_uint32_t subseed_base = svn_test_rand (&seed);
      FILE *source = generate_random_file (maxlen, subseed_base, &seed,
                                           random_bytes, bytes_range,
                                           dump_files);
      FILE *target = generate_random_file (maxlen, subseed_base, &seed,
                                           random_bytes, bytes_range,
                                           dump_files);
      FILE *source_copy = copy_tempfile (source);
      FILE *target_regen = tmpfile ();

      svn_txdelta_stream_t *txdelta_stream;
      svn_txdelta_window_handler_t handler;
      svn_stream_t *stream;
      void *handler_baton;

      /* Set up a four-stage pipeline: create a delta, convert it to
         svndiff format, parse it back into delta format, and apply it
         to a copy of the source file to see if we get the same target
         back.  */
      apr_pool_t *delta_pool = svn_pool_create (pool);

      /* Make stage 4: apply the text delta.  */
      svn_txdelta_apply (svn_stream_from_stdio (source_copy, delta_pool),
                         svn_stream_from_stdio (target_regen, delta_pool),
                         delta_pool, &handler, &handler_baton);

      /* Make stage 3: reparse the text delta.  */
      stream = svn_txdelta_parse_svndiff (handler, handler_baton, TRUE,
                                          delta_pool);

      /* Make stage 2: encode the text delta in svndiff format.  */
      svn_txdelta_to_svndiff (stream, delta_pool, &handler, &handler_baton);

      /* Make stage 1: create the text delta.  */
      svn_txdelta (&txdelta_stream,
                   svn_stream_from_stdio (source, delta_pool),
                   svn_stream_from_stdio (target, delta_pool),
                   delta_pool);

      SVN_ERR (svn_txdelta_send_txstream (txdelta_stream,
                                          handler,
                                          handler_baton,
                                          delta_pool));

      svn_pool_destroy (delta_pool);

      SVN_ERR (compare_files (target, target_regen, dump_files, pool));

      fclose(source);
      fclose(target);
      fclose(source_copy);
      fclose(target_regen);
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
do_random_combine_test (const char **msg,
                        svn_boolean_t msg_only,
                        apr_pool_t *pool,
                        apr_uint32_t *last_seed)
{
  static char msg_buff[256];

  apr_uint32_t seed, bytes_range, maxlen;
  int i, iterations, dump_files, print_windows;
  const char *random_bytes;

  /* Initialize parameters and print out the seed in case we dump core
     or something. */
  init_params(&seed, &maxlen, &iterations, &dump_files, &print_windows,
              &random_bytes, &bytes_range, pool);
  sprintf(msg_buff,
          "random combine delta test, seed = %lu", (unsigned long) seed);
  *msg = msg_buff;

  if (msg_only)
    return SVN_NO_ERROR;
  else
    printf("SEED: %s\n", msg_buff);

  for (i = 0; i < iterations; i++)
    {
      /* Generate source and target for the delta and its application.  */
      apr_uint32_t subseed_base = svn_test_rand ((*last_seed = seed, &seed));
      FILE *source = generate_random_file (maxlen, subseed_base, &seed,
                                           random_bytes, bytes_range,
                                           dump_files);
      FILE *middle = generate_random_file (maxlen, subseed_base, &seed,
                                           random_bytes, bytes_range,
                                           dump_files);
      FILE *target = generate_random_file (maxlen, subseed_base, &seed,
                                           random_bytes, bytes_range,
                                           dump_files);
      FILE *source_copy = copy_tempfile (source);
      FILE *middle_copy = copy_tempfile (middle);
      FILE *target_regen = tmpfile ();

      svn_txdelta_stream_t *txdelta_stream_A;
      svn_txdelta_stream_t *txdelta_stream_B;
      svn_txdelta_window_handler_t handler;
      svn_stream_t *stream;
      void *handler_baton;

      /* Set up a four-stage pipeline: create two deltas, combine them
         and convert the result to svndiff format, parse that back
         into delta format, and apply it to a copy of the source file
         to see if we get the same target back.  */
      apr_pool_t *delta_pool = svn_pool_create (pool);

      /* Make stage 4: apply the text delta.  */
      svn_txdelta_apply (svn_stream_from_stdio (source_copy, delta_pool),
                         svn_stream_from_stdio (target_regen, delta_pool),
                         delta_pool, &handler, &handler_baton);

      /* Make stage 3: reparse the text delta.  */
      stream = svn_txdelta_parse_svndiff (handler, handler_baton, TRUE,
                                          delta_pool);

      /* Make stage 2: encode the text delta in svndiff format.  */
      svn_txdelta_to_svndiff (stream, delta_pool, &handler, &handler_baton);

      /* Make stage 1: create the text deltas.  */

      svn_txdelta (&txdelta_stream_A,
                   svn_stream_from_stdio (source, delta_pool),
                   svn_stream_from_stdio (middle, delta_pool),
                   delta_pool);

      svn_txdelta (&txdelta_stream_B,
                   svn_stream_from_stdio (middle_copy, delta_pool),
                   svn_stream_from_stdio (target, delta_pool),
                   delta_pool);

      {
        svn_txdelta_window_t *window_A;
        svn_txdelta_window_t *window_B;
        svn_txdelta_window_t *composite;
        apr_pool_t *wpool = svn_pool_create (delta_pool);
        apr_off_t sview_offset = 0;

        do
          {
            SVN_ERR (svn_txdelta_next_window (&window_A, txdelta_stream_A,
                                              wpool));
            if (print_windows)
              delta_window_print (window_A, "A ", stdout);
            SVN_ERR (svn_txdelta_next_window (&window_B, txdelta_stream_B,
                                              wpool));
            if (print_windows)
              delta_window_print (window_B, "B ", stdout);
            composite = svn_txdelta__compose_windows (window_A, window_B,
                                                      &sview_offset, wpool);
            if (print_windows)
              delta_window_print (composite, "AB", stdout);

            SVN_ERR (handler (composite, handler_baton));
            svn_pool_clear (wpool);
          }
        while (composite != NULL);
        svn_pool_destroy (wpool);
      }

      svn_pool_destroy (delta_pool);

      SVN_ERR (compare_files (target, target_regen, dump_files, pool));

      fclose(source);
      fclose(middle);
      fclose(target);
      fclose(source_copy);
      fclose(middle_copy);
      fclose(target_regen);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
random_combine_test (const char **msg,
                     svn_boolean_t msg_only,
                     apr_pool_t *pool)
{
  apr_uint32_t seed;
  svn_error_t *err = do_random_combine_test (msg, msg_only, pool, &seed);
  if (!msg_only)
    printf("SEED: Last seen = %lu\n", (unsigned long) seed);
  return err;
}


/* Change to 1 to enable the unit test for the delta combiner's range index: */
#if 0
#include "range-index-test.h"
#endif



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  random_test,
  random_combine_test,
#ifdef SVN_RANGE_INDEX_TEST_H
  random_range_index_test,
#endif
  0
};



/*
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
