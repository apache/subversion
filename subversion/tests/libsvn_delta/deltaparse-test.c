/* deltaparse-test.c -- simple demo of using SVN's XML parser interface. 
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


#include <stdio.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_test.h"   /* For svn_test_get_editor() */




int
main (int argc, char *argv[])
{
  apr_pool_t *globalpool;
  const svn_delta_edit_fns_t *editor;
  svn_error_t *err;
  apr_file_t *file = NULL;
  apr_status_t status;
  void *root_dir_baton;

  svn_revnum_t base_revision;
  svn_string_t *base_path;


  /* Process args */
  if (argc != 2)
    {
      printf 
        ("\nUsage: %s [filename], where [filename] contains an XML tree-delta",
         argv[0]);
      exit (1);
    }

  /* Init global memory pool */
  apr_initialize ();
  globalpool = svn_pool_create (NULL);

  /* Open a file full of XML, create "source baton" (the filehandle)
     that my_read_func() will slurp XML from. */
  status = apr_open (&file, argv[1], APR_READ, APR_OS_DEFAULT, globalpool);
  if (status)
    {
      printf ("Error opening %s\n.", argv[1]);
      exit (1);
    }
    

  /* Set context variables for evaluating a tree-delta */
  base_revision = 37;
  base_path = svn_string_create ("/root", globalpool);
  
  /* Grab the "test" editor and baton */
  err = svn_test_get_editor (&editor, &root_dir_baton,
                             base_path, base_revision, globalpool);
  
  /* Fire up the XML parser */
  err = svn_delta_xml_auto_parse (svn_stream_from_aprfile (file, globalpool),
                                  editor,
                                  root_dir_baton,
                                  base_path,
                                  base_revision,
                                  globalpool);

  apr_close (file);
  
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      apr_destroy_pool (globalpool);
      exit (err->apr_err);
    }

  apr_destroy_pool (globalpool);
  exit (0);
}
