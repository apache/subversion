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

#define DEFAULT_ITERATIONS 30
#define DEFAULT_MAXLEN (100 * 1024)
#define SEEDS 50
#define MAXSEQ 100


/* Initialize parameters for the random tests. */
extern int test_argc;
extern const char **test_argv;

static void init_params (unsigned long *seed,
                         int *maxlen, int *iterations,
                         apr_pool_t *pool)
{
  apr_getopt_t *opt;
  char optch;
  const char *opt_arg;
  apr_status_t status;

  *seed = (unsigned long) apr_time_now();
  *maxlen = DEFAULT_MAXLEN;
  *iterations = DEFAULT_ITERATIONS;

  apr_getopt_init (&opt, pool, test_argc, test_argv);
  while (APR_SUCCESS
         == (status = apr_getopt (opt, "s:l:n:", &optch, &opt_arg)))
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
        }
    }
}



static unsigned long
myrand (unsigned long *seed)
{
  *seed = (*seed * 1103515245 + 12345) & 0xffffffff;
  return *seed;
}


/* Generate a temporary file containing sort-of random data.  Diffs
   between files of random data tend to be pretty boring, so we try to
   make sure there are a bunch of common substrings between two runs
   of this function with the same seedbase.  */
static FILE *
generate_random_file (int maxlen, unsigned long subseed_base,
                      unsigned long *seed)
{
  int len, seqlen;
  FILE *fp;
  unsigned long r;

  fp = tmpfile ();
  assert (fp != NULL);
  len = myrand (seed) % maxlen;       /* We might go over this by a bit.  */
  while (len > 0)
    {
      /* Generate a pseudo-random sequence of up to MAXSEQ bytes,
         where the seed is in the range [seedbase..seedbase+MAXSEQ-1].
         (Use our own pseudo-random number generator here to avoid
         clobbering the seed of the libc random number generator.)  */
      seqlen = myrand (seed) % MAXSEQ;
      len -= seqlen;
      r = subseed_base + myrand (seed) % SEEDS;
      while (seqlen-- > 0)
        { 
          putc (r % 256, fp);
          r = r * 1103515245 + 12345;
        }
    }
  rewind (fp);
  return fp;
}

/* Compare two open files. The file positions may change. */
static svn_error_t *
compare_files (FILE *f1, FILE *f2, apr_pool_t *pool)
{
  int c1, c2;
  apr_off_t pos = 0;

  rewind (f1);
  rewind (f2);
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
  while ((c = getc (fp)) != EOF)
    putc (c, newfp);
  rewind (newfp);
  return newfp;
}



static svn_error_t *
random_test (const char **msg,
             svn_boolean_t msg_only,
             apr_pool_t *pool)
{
  static char msg_buff[256];

  unsigned long seed;
  int i, maxlen, iterations;

  /* Initialize parameters and print out the seed in case we dump core
     or something. */
  init_params(&seed, &maxlen, &iterations, pool);
  sprintf(msg_buff, "random delta test, seed = %lu", seed);
  *msg = msg_buff;

  if (msg_only)
    return SVN_NO_ERROR;
  else
    printf("SEED: %s\n", msg_buff);

  for (i = 0; i < iterations; i++)
    {
      /* Generate source and target for the delta and its application.  */
      unsigned long subseed_base = myrand (&seed);
      FILE *source = generate_random_file (maxlen, subseed_base, &seed);
      FILE *target = generate_random_file (maxlen, subseed_base, &seed);
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
      rewind (source);

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

      SVN_ERR (compare_files (target, target_regen, pool));

      fclose(source);
      fclose(target);
      fclose(source_copy);
      fclose(target_regen);
    }

  return SVN_NO_ERROR;
}



#include "../../libsvn_delta/compose_delta.c"


static range_index_node_t *prev_node, *prev_prev_node;
static apr_off_t
walk_range_index (range_index_node_t *node, const char **msg)
{
  apr_off_t ret;

  if (node == NULL)
    return 0;

  ret = walk_range_index (node->left, msg);
  if (ret > 0)
    return ret;

  if (prev_node != NULL
      && node->target_offset > 0
      && (prev_node->offset >= node->offset
          || (prev_node->limit >= node->limit)))
    {
      ret = node->target_offset;
      node->target_offset = -node->target_offset;
      *msg = "Oops, the previous node ate me.";
      return ret;
    }
  if (prev_prev_node != NULL
      && prev_node->target_offset > 0
      && prev_prev_node->limit >= node->offset)
    {
      ret = prev_node->target_offset;
      prev_node->target_offset = -prev_node->target_offset;
      *msg = "Arrgh, my neighbours are conspiring against me.";
      return ret;
    }
  prev_prev_node = prev_node;
  prev_node = node;

  return walk_range_index (node->right, msg);
}

static void
print_range_index (range_index_node_t *node, const char *msg, apr_off_t ndx)
{
  if (node == NULL)
    return;

  print_range_index (node->left, msg, ndx);
  if (-node->target_offset == ndx)
    {
      printf ("   * Node: [%3"APR_OFF_T_FMT
              ", %3"APR_OFF_T_FMT
              ") = %-5"APR_OFF_T_FMT"%s\n",
              node->offset, node->limit, -node->target_offset, msg);
    }
  else
    {
      printf ("     Node: [%3"APR_OFF_T_FMT
              ", %3"APR_OFF_T_FMT
              ") = %"APR_OFF_T_FMT"\n",
              node->offset, node->limit,
              (node->target_offset < 0
               ? -node->target_offset : node->target_offset));
    }
  print_range_index (node->right, msg, ndx);
}


static svn_error_t *
random_range_index_test (const char **msg,
                         svn_boolean_t msg_only,
                         apr_pool_t *pool)
{
  static char msg_buff[256];

  unsigned long seed;
  int i, maxlen, iterations;
  range_index_t *ndx;

  /* Initialize parameters and print out the seed in case we dump core
     or something. */
  init_params(&seed, &maxlen, &iterations, pool);
  sprintf(msg_buff, "random range index test, seed = %lu", seed);
  *msg = msg_buff;

  /* ### This test is expected to fail randomly at the moment, so don't
     enable it by default. --xbc */
  if (msg_only /*REMOVE*/|| 1)
    return SVN_NO_ERROR;
  else
    printf("SEED: %s\n", msg_buff);

  ndx = create_range_index (pool);
  for (i = 1; i <= iterations; ++i)
    {
      apr_off_t offset = myrand (&seed) % 47;
      apr_off_t limit = offset + myrand (&seed) % 16 + 1;
      apr_off_t ret;
      const char *msg;

      printf ("%3d: Inserting [%3"APR_OFF_T_FMT", %3"APR_OFF_T_FMT") ...... ",
              i, offset, limit);
      insert_range (offset, limit, i, ndx);
      prev_prev_node = prev_node = NULL;
      ret = walk_range_index (ndx->tree, &msg);
      if (ret == 0)
        {
          printf (" OK\n");
        }
      else
        {
          printf (" Ooops!\n");
          print_range_index (ndx->tree, msg, ret);
          return svn_error_create (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                   "insert_range");
        }
    }
  return SVN_NO_ERROR;
}



/* The test table.  */

svn_error_t * (*test_funcs[]) (const char **msg,
                               svn_boolean_t msg_only,
                               apr_pool_t *pool) = {
  0,
  random_test,
  random_range_index_test,
  0
};



/*
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
