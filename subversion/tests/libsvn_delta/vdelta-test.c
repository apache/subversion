/* vdelta-test.c -- test driver for text deltas
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
#include <apr_lib.h>

#include "svn_delta.h"
#include "svn_error.h"
#include "svn_pools.h"

static apr_off_t
print_delta_window (int quiet, svn_txdelta_window_t *window, FILE *stream)
{
  int i;
  apr_off_t len = 0, tmp;

  /* Try to estimate the size of the delta. */
  for (i = 0; i < window->num_ops; ++i)
    {
      apr_size_t const offset = window->ops[i].offset;
      apr_size_t const length = window->ops[i].length;
      if (window->ops[i].action_code == svn_txdelta_new)
        {
          len += 1;             /* opcode */
          len += (length > 255 ? 2 : 1);
          len += length;
        }
      else
        {
          len += 1;             /* opcode */
          len += (offset > 255 ? 2 : 1);
          len += (length > 255 ? 2 : 1);
        }
    }

  if (quiet)
    return len;
  
  fprintf (stream, "(WINDOW %" APR_OFF_T_FMT, len);
  for (i = 0; i < window->num_ops; ++i)
    {
      apr_size_t const offset = window->ops[i].offset;
      apr_size_t const length = window->ops[i].length;
      switch (window->ops[i].action_code)
        {
        case svn_txdelta_source:
          fprintf (stream, "\n  (SOURCE %" APR_SIZE_T_FMT
                   " %" APR_SIZE_T_FMT ")", offset, length);
          break;
        case svn_txdelta_target:
          fprintf (stream, "\n  (TARGET %" APR_SIZE_T_FMT
                   " %" APR_SIZE_T_FMT ")", offset, length);
          break;
        case svn_txdelta_new:
          fprintf (stream, "\n  (INSERT %" APR_SIZE_T_FMT " \"", length);
          for (tmp = offset; tmp < offset + length; ++tmp)
            {
              int const dat = window->new_data->data[tmp];
              if (apr_iscntrl (dat) || !apr_isascii(dat))
                fprintf (stream, "\\%3.3o", dat & 0xff);
              else if (dat == '\\')
                fputs ("\\\\", stream);
              else
                putc (dat, stream);
            }
          fputs ("\")", stream);
          break;
        default:
          fputs ("\n  (BAD-OP)", stream);
        }
    }
  fputs (")\n", stream);
  return len;
}

int
main (int argc, char **argv)
{
  FILE *source_file = NULL;
  FILE *target_file = NULL;
  svn_txdelta_stream_t *stream;
  svn_txdelta_window_t *window;
  apr_pool_t *wpool;

  int count = 0;
  int quiet = 0;
  apr_off_t len = 0;

  if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'q')
    {
      quiet = 1;
      --argc; ++argv;
    }

  if (argc == 2)
    {
      target_file = fopen (argv[1], "rb");
    }
  else if (argc == 3)
    {
      source_file = fopen (argv[1], "rb");
      target_file = fopen (argv[2], "rb");
    }
  else
    {
      fprintf (stderr,
               "Usage: vdelta-test [-q] <target>\n"
               "   or: vdelta-test [-q] <source> <target>\n");
      exit (1);
    }

  apr_initialize();
  wpool = svn_pool_create (NULL);
  svn_txdelta (&stream,
               svn_stream_from_stdio (source_file, wpool),
               svn_stream_from_stdio (target_file, wpool),
               wpool);

  /* ### urm. we should have a pool here! */
  do {
    svn_txdelta_next_window (&window, stream, wpool);
    if (window != NULL)
      {
        len += print_delta_window (quiet, window, stdout);
        svn_pool_clear (wpool);
        ++count;
      }
  } while (window != NULL);
  svn_pool_destroy (wpool);
  fprintf (stdout, "(LENGTH %" APR_OFF_T_FMT " +%d)\n", len, count);

  if (source_file)
    fclose (source_file);
  fclose (target_file);

  apr_terminate();
  exit (0);
}



/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
