
/* Test driver for text deltas */

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
print_delta_window (svn_txdelta_window_t *window, FILE *stream)
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
                fprintf (stream, "\\%3.3o", dat);
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
  apr_off_t len = 0;

  if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 's')
    {
      char *endptr;
      svn_txdelta__window_size = strtol (&argv[1][2], &endptr, 10);
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
               "Usage: vdelta-test [-s<window size>] <target>\n"
               "   or: vdelta-test [-s<window size>] <source> <target>\n");
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
        len += print_delta_window (window, stdout);
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
