/*
 * md5args.c: Command-line argument verifier.
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
#include <apr_general.h>
#include <apr_md5.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_string.h"

/*** Main. ***/

static void
print_usage (const char *progname)
{
  printf ("%s - Argument verification tool\n"
          "\n"
          "USAGE: %s MD5CHECKSUM ARG1 [ARG2 ... ARGn]\n"
          "\n"
          "MD5CHECKSUM is string of hexpairs (using capitals for A - F)\n"
          "representing an MD5 checksum\n"
          "\n"
          "This program returns 0 if the concatenation of ARG1, ARG2,\n"
          "... ARGn (with a single space character ' ' between them)\n"
          "results in a string whose MD5 checksum is equivalent to the\n"
          "MD5CHECKSUM passed as the first argument to this program.\n",
          progname,
          progname);
  return;
}


int
main (int argc, const char * const *argv)
{
  int i;
  apr_pool_t *pool;
  svn_stringbuf_t *string, *digest_str;
  unsigned char digest[MD5_DIGESTSIZE];

  if (apr_initialize () != APR_SUCCESS)
    {
      printf ("apr_initialize() failed.\n");
      exit (1);
    }

  /* set up the global pool */
  pool = svn_pool_create (NULL);

  if (argc == 1)
    {
      /* Nothing to do...not an error. */
      exit (0);
    }

  if (argc == 2)
    {
      print_usage (argv[0]);
      exit (-1);
    }

  /* Create our expected digest. */
  if (strlen (argv[1]) != (MD5_DIGESTSIZE * 2))
    {
      printf ("md5 checksum has unexpected length.\n");
      exit (-2);
    }
  
  /* Build the string of space-separated arguments. */
  string = svn_stringbuf_create ("", pool);
  for (i = 2; i < (argc - 1); i++)
    {
      svn_stringbuf_appendcstr (string, argv[i]);
      svn_stringbuf_appendcstr (string, " ");
    }
  svn_stringbuf_appendcstr (string, argv[argc - 1]);

  printf ("args=%s\n", string->data);

  /* Now, run the MD5 digest calculation on that string. */
  apr_md5 (digest, string->data, string->len);
  digest_str = svn_stringbuf_create ("", pool);
  for (i = 0; i < MD5_DIGESTSIZE; i++)
    {
      svn_stringbuf_t *tmp_str = 
        svn_stringbuf_createf (pool, "%02X", digest[i]);
      svn_stringbuf_appendstr (digest_str, tmp_str);
    }

  /* Exit with an error if the two digests don't match. */
  if (strcmp (argv[1], digest_str->data))
    {
      printf ("md5 checksum failure.\n");
      exit (1);
    }

  /* Clean up APR */
  svn_pool_destroy (pool);
  apr_terminate();

  exit (0);
}
