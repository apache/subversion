/*
 * random-test.c:  Test delta generation and application using random data.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */


#include <stdio.h>
#include <assert.h>
#include "apr_general.h"
#include "apr_getopt.h"
#include "svn_delta.h"
#include "svn_error.h"

#define DEFAULT_MAXLEN (100 * 1024)

/* Generate a temporary file containing random data.  */
static FILE *
generate_random_file (int maxlen)
{
  int len, i;
  FILE *fp;

  fp = tmpfile ();
  assert (fp != NULL);
  len = rand () % maxlen;
  for (i = 0; i < len; i++)
    putc (rand () % 256, fp);
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

/* NOTE: Does no error-checking.  */
static svn_error_t *
read_from_file (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  FILE *fp = baton;

  if (!fp || feof (fp) || ferror (fp))
    *len = 0;
  else
    *len = fread (buffer, 1, *len, fp);
  return SVN_NO_ERROR;
}

/* NOTE: Does no error-checking.  */
static svn_error_t *
write_to_file (void *baton, const char *data, apr_size_t *len,
               apr_pool_t *pool)
{
  FILE *fp = baton;

  *len = fwrite (data, 1, *len, fp);
  return SVN_NO_ERROR;
}

int
main (int argc, char **argv)
{
  FILE *source, *source_copy, *target, *target_regen;
  apr_getopt_t *opt;
  apr_pool_t *pool;
  apr_status_t status;
  char optch;
  const char *optarg;
  unsigned int seed;
  int seed_set = 0, maxlen = DEFAULT_MAXLEN, c1, c2;
  svn_txdelta_stream_t *stream;
  svn_txdelta_window_t *window;
  svn_txdelta_applicator_t *appl;
  svn_error_t *err;

  apr_initialize();

  /* Read options.  */
  pool = svn_pool_create (NULL, NULL);
  apr_initopt (&opt, NULL, argc, argv);
  while ((status = apr_getopt (opt, "s:l:", &optch, &optarg)) == APR_SUCCESS)
    {
      switch (optch)
        {
        case 's':
          seed = atoi (optarg);
          seed_set = 1;
          break;
        case 'l':
          maxlen = atoi (optarg);
          break;
        }
    }
  apr_destroy_pool (pool);
  if (status != APR_EOF)
    {
      fprintf (stderr, "Usage: %s [-s seed]\n", argv[0]);
      return 1;
    }

  if (!seed_set)
    {
      seed = (unsigned int) apr_now ();
      printf ("Using seed %d\n", seed);
      srand (seed);
    }

  /* Generate source and target files for the delta and its application.  */
  source = generate_random_file (maxlen);
  target = generate_random_file (maxlen);
  source_copy = copy_tempfile (source);
  rewind (source);
  target_regen = tmpfile ();

  /* Create and simultaneously apply a delta between the source and target.  */
  pool = svn_pool_create (NULL, NULL);
  err = svn_txdelta (&stream,
                     read_from_file, source,
                     read_from_file, target,
                     pool);
  if (err == SVN_NO_ERROR)
    err = svn_txdelta_applicator_create (&appl, read_from_file, source_copy,
                                         write_to_file, target_regen, pool);
  while (err == SVN_NO_ERROR)
    {
      err = svn_txdelta_next_window (&window, stream);
      if (window == NULL)
        break;
      err = svn_txdelta_apply_window (window, appl);
      svn_txdelta_free_window (window);
    }
  if (err != SVN_NO_ERROR)
    {
      svn_handle_error (err, stderr);
      exit (1);
    }
  svn_txdelta_free (stream);
  svn_txdelta_applicator_free (appl);
  apr_destroy_pool (pool);

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
          printf ("Regenerated files differ; test failed.\n");
          exit (1);
        }
    }
  printf ("Test succeeded.\n");
  exit (0);
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
