/* vdelta-test.c -- test driver for text deltas
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
 * software developed by CollabNet (http://www.Collab.Net/)."
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include "svn_delta.h"
#include "svn_error.h"

static svn_error_t*
read_from_file (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  FILE *stream = baton;
  if (!stream || feof (stream) || ferror (stream))
    *len = 0;
  else
    *len = fread (buffer, 1, *len, stream);
  return SVN_NO_ERROR;
}


static apr_off_t
print_delta_window (int quiet, svn_txdelta_window_t *window, FILE *stream)
{
  int i;
  apr_off_t len = 0, tmp;

  /* Try to estimate the size of the delta. */
  for (i = 0; i < window->num_ops; ++i)
    {
      apr_off_t const offset = window->ops[i].offset;
      apr_off_t const length = window->ops[i].length;
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
  
  fprintf (stream, "(WINDOW %ld", (long) len);
  for (i = 0; i < window->num_ops; ++i)
    {
      apr_off_t const offset = window->ops[i].offset;
      apr_off_t const length = window->ops[i].length;
      switch (window->ops[i].action_code)
        {
        case svn_txdelta_source:
          fprintf (stream, "\n  (SOURCE %ld %ld)",
                   (long) offset, (long) length);
          break;
        case svn_txdelta_target:
          fprintf (stream, "\n  (TARGET %ld %ld)",
                   (long) offset, (long) length);
          break;
        case svn_txdelta_new:
          fprintf (stream, "\n  (INSERT %ld \"", (long) length);
          for (tmp = offset; tmp < offset + length; ++tmp)
            {
              int const dat = window->new->data[tmp];
              if (iscntrl (dat) || !isascii(dat))
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

extern apr_size_t svn_txdelta__window_size;

int
main (int argc, char **argv)
{
  FILE *source_file = NULL;
  FILE *target_file = NULL;
  svn_txdelta_stream_t *stream;
  svn_txdelta_window_t *window;

  int count = 0;
  int quiet = 0;
  apr_off_t len = 0;

  if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 's')
    {
      char *endptr;
      svn_txdelta__window_size = strtol (&argv[1][2], &endptr, 10);
      --argc; ++argv;
    }

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
               "Usage: vdelta-test [-q] [-s<window size>] <target>\n"
               "   or: vdelta-test [-q] [-s<window size>] <source> <target>\n");
      exit (1);
    }

  apr_initialize();
  svn_txdelta (&stream,
               read_from_file, source_file,
               read_from_file, target_file,
               NULL);

  do {
    svn_txdelta_next_window (&window, stream);
    if (window != NULL)
      {
        len += print_delta_window (quiet, window, stdout);
        svn_txdelta_free_window (window);
        ++count;
      }
  } while (window != NULL);
  fprintf (stdout, "(LENGTH %ld +%d)\n", (long) len, count);

  svn_txdelta_free (stream);
  if (source_file)
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
