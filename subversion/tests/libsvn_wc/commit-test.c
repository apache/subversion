/* commit-test.c -- simple test of the working copy "crawler"
 *
 * Crawler walks a working copy and prints a virtual `commit' to stdout.
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

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_xml.h"
#include "svn_delta.h"
#include "svn_wc.h"
#include "svn_test.h"

/* libsvn_test.la requires this symbol */
svn_error_t *(*test_funcs[])(char **msg, apr_pool_t *p) = { 0, 0 };

int
main (int argc, char *argv[])
{
  svn_error_t *err;
  apr_status_t status;
  apr_pool_t *globalpool;
  apr_file_t *stdout_handle;
  svn_stream_t *out_stream;

  const svn_delta_edit_fns_t *my_editor;
  void *my_edit_baton;

  svn_string_t *rootdir;

  svn_boolean_t use_xml = FALSE;

  /* Process command-line args */
  if (argc < 2)
    {
      printf 
        ("\nUsage: %s [dir] [-x]:  crawls working copy [dir]\n",
         argv[0]);
      printf ("Prints human-readable `commit', or XML if -x is used.\n");
      exit (1);
    }

  /* Init global memory pool */
  apr_initialize ();
  globalpool = svn_pool_create (NULL);

  rootdir = svn_string_create (argv[1], globalpool);

  if (argc > 2)
    if (! strcmp (argv[2], "-x"))
      use_xml = TRUE;
      
  /* Get an editor */

  if (use_xml)  /* xml output */
    {
      /* Open a stdout filehandle */
      status = apr_file_open (&stdout_handle, "-", APR_WRITE,
                         APR_OS_DEFAULT, globalpool);
      
      err = svn_delta_get_xml_editor (svn_stream_from_aprfile (stdout_handle,
							       globalpool),
                                      &my_editor, &my_edit_baton,
                                      globalpool);
      if (err)
        {
          svn_handle_error (err, stderr, 0);
          svn_pool_destroy (globalpool);
          exit (1);
        }
    }

  else  /* human-readable output */
    {
      /* A stream to print to stdout. */
      out_stream = svn_stream_from_stdio (stdout, globalpool);

      err = svn_test_get_editor (&my_editor, &my_edit_baton,
                                 out_stream, 3, rootdir, globalpool);
      if (err)
        {
          svn_handle_error (err, stderr, 0);
          svn_pool_destroy (globalpool);
          exit (1);
        }
    }

  /* Call the commit-crawler with the editor. */
  err = svn_wc_crawl_local_mods (rootdir,
                                 my_editor, my_edit_baton,
                                 globalpool);
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      svn_pool_destroy (globalpool);
      exit (1);
    }

  svn_pool_destroy (globalpool);
  exit (0);
}
