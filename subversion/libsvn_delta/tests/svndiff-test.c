
/* Test driver for text deltas */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <apr_general.h>
#include "svn_delta.h"
#include "svn_error.h"


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
  FILE *source_file;
  FILE *target_file;
  svn_txdelta_stream_t *stream;
  svn_txdelta_window_t *window;
  svn_txdelta_window_handler_t *handler;
  void *baton;

  source_file = fopen (argv[1], "rb");
  target_file = fopen (argv[2], "rb");

  apr_initialize();
  svn_txdelta (&stream, read_from_file, source_file,
	       read_from_file, target_file, NULL);

  svn_txdelta_to_svndiff (write_to_file, stdout, NULL, &handler, &baton);
  do {
    svn_txdelta_next_window (&window, stream);
    handler (window, baton);
    svn_txdelta_free_window (window);
  } while (window != NULL);

  svn_txdelta_free (stream);
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
