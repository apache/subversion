/* svndiff-test.c -- test driver for text deltas
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include "svn_base64.h"
#include "svn_quoprint.h"
#include "svn_delta.h"
#include "svn_error.h"


int
main (int argc, char **argv)
{
  FILE *source_file;
  FILE *target_file;
  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_t *window;
  svn_txdelta_window_handler_t svndiff_handler;
  svn_stream_t *encoder;
  void *svndiff_baton;

  source_file = fopen (argv[1], "rb");
  target_file = fopen (argv[2], "rb");

  apr_initialize();
  svn_txdelta (&txdelta_stream, svn_stream_from_stdio (source_file, NULL),
	       svn_stream_from_stdio (target_file, NULL), NULL);

#ifdef QUOPRINT_SVNDIFFS
  encoder = svn_quoprint_encode (svn_stream_from_stdio (stdout, NULL), NULL);
#else
  encoder = svn_base64_encode (svn_stream_from_stdio (stdout, NULL), NULL);
#endif
  svn_txdelta_to_svndiff (encoder, NULL, &svndiff_handler, &svndiff_baton);
  do {
    svn_txdelta_next_window (&window, txdelta_stream);
    svndiff_handler (window, svndiff_baton);
    svn_txdelta_free_window (window);
  } while (window != NULL);

  svn_txdelta_free (txdelta_stream);
  fclose (source_file);
  fclose (target_file);

  apr_terminate();
  exit (0);
}



/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
