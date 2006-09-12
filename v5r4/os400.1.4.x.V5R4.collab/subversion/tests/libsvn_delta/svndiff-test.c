/* svndiff-test.c -- test driver for text deltas
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
main(int argc, char **argv)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *source_file;
  apr_file_t *target_file;
  svn_stream_t *stdout_stream;
  svn_txdelta_stream_t *txdelta_stream;
  svn_txdelta_window_handler_t svndiff_handler;
  svn_stream_t *encoder;
  void *svndiff_baton;
  apr_pool_t *pool;
  int version = 0;

  if (argc < 3)
    {
      printf("usage: %s source target [version]\n", argv[0]);
      exit(0);
    }

  apr_initialize();
  pool = svn_pool_create(NULL);
  apr_err = apr_file_open(&source_file, argv[1], (APR_READ | APR_BINARY),
                          APR_OS_DEFAULT, pool);
  if (apr_err)
    {
      fprintf(stderr, "unable to open \"%s\" for reading\n", argv[1]);
      exit(1);
    }

  apr_err = apr_file_open(&target_file, argv[2], (APR_READ | APR_BINARY),
                          APR_OS_DEFAULT, pool);
  if (apr_err)
    {
      fprintf(stderr, "unable to open \"%s\" for reading\n", argv[2]);
      exit(1);
    }
  if (argc == 4)
    version = atoi(argv[3]);

  svn_txdelta(&txdelta_stream,
              svn_stream_from_aprfile(source_file, pool),
              svn_stream_from_aprfile(target_file, pool),
              pool);

  err = svn_stream_for_stdout(&stdout_stream, pool);
  if (err)
    svn_handle_error2(err, stdout, TRUE, "svndiff-test: ");

#ifdef QUOPRINT_SVNDIFFS
  encoder = svn_quoprint_encode(stdout_stream, pool);
#else
  encoder = svn_base64_encode(stdout_stream, pool);
#endif
  svn_txdelta_to_svndiff2(&svndiff_handler, &svndiff_baton, 
                          encoder, version, pool);
  err = svn_txdelta_send_txstream(txdelta_stream,
                                  svndiff_handler,
                                  svndiff_baton,
                                  pool);
  if (err)
    svn_handle_error2(err, stdout, TRUE, "svndiff-test: ");

  apr_file_close(source_file);
  apr_file_close(target_file);
  svn_pool_destroy(pool);
  apr_terminate();
  exit(0);
}
