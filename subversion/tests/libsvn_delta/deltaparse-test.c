/* deltaparse-test.c -- simple demo of using SVN's XML parser interface. 
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

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_general.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_test.h"   /* For svn_test_get_editor() */

/* libsvn_test.la requires this symbol */ 
struct svn_test_descriptor_t test_funcs[] = {SVN_TEST_NULL };

#define BASE_PATH "/root"


int
main (int argc, char *argv[])
{
  apr_pool_t *globalpool;
  const svn_delta_edit_fns_t *editor;
  svn_error_t *err;
  apr_file_t *file = NULL;
  apr_status_t status;
  void *edit_baton;
  svn_stream_t *out_stream;


  /* Process args */
  if (argc != 2)
    {
      printf 
        ("Usage: %s [filename], where [filename] contains an XML tree-delta\n",
         argv[0]);
      exit (1);
    }

  /* Init global memory pool */
  apr_initialize ();
  globalpool = svn_pool_create (NULL);

  /* Open a file full of XML, create "source baton" (the filehandle)
     that my_read_func() will slurp XML from. */
  status = apr_file_open (&file, argv[1], APR_READ,
                          APR_OS_DEFAULT, globalpool);
  if (status)
    {
      printf ("Error opening %s\n.", argv[1]);
      exit (1);
    }

  /* Set up a stream to print to stdout. */
  out_stream = svn_stream_from_stdio (stdout, globalpool);

  /* Grab the "test" editor and baton */
  err = svn_test_get_editor (&editor, 
                             &edit_baton,
                             svn_stringbuf_create ("DELTAPARSE-TEST", 
                                                globalpool),
                             out_stream, 
                             3, 
                             TRUE,
                             svn_stringbuf_create (BASE_PATH, globalpool),
                             globalpool);
  
  /* Fire up the XML parser */
  err = svn_delta_xml_auto_parse (svn_stream_from_aprfile (file, globalpool),
                                  editor,
                                  edit_baton,
                                  BASE_PATH,
                                  37,    /* random base revision */
                                  globalpool);

  apr_file_close (file);
  
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      svn_pool_destroy (globalpool);
      exit (err->apr_err);
    }

  svn_pool_destroy (globalpool);

  exit (0);
}
