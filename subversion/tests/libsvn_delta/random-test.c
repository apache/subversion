/*
 * random-test.c:  Test delta generation and application using random data.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
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


int
main (int argc, const char * const *argv)
{
  FILE *source, *source_copy, *target, *target_regen;
  apr_getopt_t *opt;
  apr_pool_t *pool;
  apr_status_t status;
  char optch;
  const char *opt_arg, *progname;
  unsigned long seed, seed_save, subseed_base;
  int seed_set = 0, maxlen = DEFAULT_MAXLEN, iterations = DEFAULT_ITERATIONS;
  int c1, c2, i;
  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_t *window;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;
  svn_stream_t *stream;
  svn_error_t *err = SVN_NO_ERROR;

  progname = strrchr (argv[0], '/');
  progname = (progname == NULL) ? argv[0] : progname + 1;

  apr_initialize();

  /* Read options.  */
  pool = svn_pool_create (NULL);
  apr_getopt_init (&opt, pool, argc, argv);
  while ((status = apr_getopt (opt, "s:l:n:", &optch, &opt_arg))
         == APR_SUCCESS)
    {
      switch (optch)
        {
        case 's':
          seed = atoi (opt_arg);
          seed_set = 1;
          break;
        case 'l':
          maxlen = atoi (opt_arg);
          break;
        case 'n':
          iterations = atoi (opt_arg);
          break;
        }
    }
  svn_pool_destroy (pool);
  if (!APR_STATUS_IS_EOF(status))
    {
      fprintf (stderr, "Usage: %s [-s seed]\n", argv[0]);
      return 1;
    }

  /* Pick a seed if one wasn't given, and save it.  Print it out in
   * case we dump core or something.  */
  if (!seed_set)
    seed = (unsigned int) apr_time_now ();
  seed_save = seed;
  printf("%s using seed %lu\n", progname, seed_save);

  for (i = 0; i < iterations; i++)
    {
      /* Generate source and target for the delta and its application.  */
      subseed_base = myrand (&seed);
      source = generate_random_file (maxlen, subseed_base, &seed);
      target = generate_random_file (maxlen, subseed_base, &seed);
      source_copy = copy_tempfile (source);
      rewind (source);
      target_regen = tmpfile ();

      /* Set up a four-stage pipeline: create a delta, convert it to
         svndiff format, parse it back into delta format, and apply it
         to a copy of the source file to see if we get the same target
         back.  */
      pool = svn_pool_create (NULL);

      /* Make stage 4: apply the text delta.  */
      svn_txdelta_apply (svn_stream_from_stdio (source_copy, pool),
                         svn_stream_from_stdio (target_regen, pool),
                         pool, &handler, &handler_baton);

      /* Make stage 3: reparse the text delta.  */
      stream = svn_txdelta_parse_svndiff (handler, handler_baton, TRUE, pool);

      /* Make stage 2: encode the text delta in svndiff format.  */
      svn_txdelta_to_svndiff (stream, pool, &handler, &handler_baton);

      /* Make stage 1: create the text delta.  */
      svn_txdelta (&txdelta_stream, svn_stream_from_stdio (source, pool),
                   svn_stream_from_stdio (target, pool), pool);

      while (err == SVN_NO_ERROR)
        {
          err = svn_txdelta_next_window (&window, txdelta_stream);
          if (err == SVN_NO_ERROR)
            err = handler (window, handler_baton);
          if (window == NULL)
            break;
          svn_txdelta_free_window (window);
        }
      if (err != SVN_NO_ERROR)
        {
          printf ("FAIL: %s 1: random delta test error, seed %lu\n",
                  progname, seed_save);
          exit (1);
        }
      svn_txdelta_free (txdelta_stream);
      svn_pool_destroy (pool);

      /* Compare the two files.  */
      rewind (target);
      rewind (target_regen);
      while (1)
        {
          c1 = getc (target);
          c2 = getc (target_regen);
          if (c1 == EOF && c2 == EOF)
            break;
          if (c1 != c2)
            {
              printf ("FAIL: %s 1: random delta test mismatch, seed %lu\n",
                      progname, seed_save);
              exit (1);
            }
        }
      fclose(source);
      fclose(target);
      fclose(source_copy);
      fclose(target_regen);
    }
  printf ("PASS: %s 1: random delta testing, seed %lu\n", progname, seed_save);
  exit (0);
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
