/* svndiff-test.c -- test driver for text deltas
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include "svn_base64.h"
#include "svn_quoprint.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_error.h"


int
main (int argc, char **argv)
{
  FILE *source_file;
  FILE *target_file;
  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_handler_t svndiff_handler;
  svn_stream_t *encoder;
  void *svndiff_baton;
  apr_pool_t *pool = svn_pool_create (NULL);

  if (argc < 3)
    {
      printf ("usage: %s source target\n", argv[0]);
      exit (0);
    }

  source_file = fopen (argv[1], "rb");
  target_file = fopen (argv[2], "rb");

  apr_initialize();
  svn_txdelta (&txdelta_stream, svn_stream_from_stdio (source_file, pool),
	       svn_stream_from_stdio (target_file, pool), pool);

#ifdef QUOPRINT_SVNDIFFS
  encoder = svn_quoprint_encode (svn_stream_from_stdio (stdout, pool), pool);
#else
  encoder = svn_base64_encode (svn_stream_from_stdio (stdout, pool), pool);
#endif
  svn_txdelta_to_svndiff (encoder, pool, &svndiff_handler, &svndiff_baton);
  svn_txdelta_send_txstream (txdelta_stream,
                             svndiff_handler,
                             svndiff_baton,
                             pool);

  fclose (source_file);
  fclose (target_file);
  svn_pool_destroy (pool);
  apr_terminate();
  exit (0);
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
