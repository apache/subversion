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




/* An official subversion "read" routine, comforming to POSIX standards. 
   This one reads our XML filehandle, passed in as our baton.  */
static svn_error_t *
my_read_func (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  apr_status_t stat;

  /* Recover our filehandle */
  apr_file_t *xmlfile = (apr_file_t *) baton;

  stat = apr_full_read (xmlfile, buffer,
                        (apr_size_t) *len,
                        (apr_size_t *) len);
  
  /* We want to return general I/O errors, but we explicitly ignore
     the APR_EOF error.  Why?  Because the caller of this routine
     doesn't want to know about that error.  It uses (*len == 0) as a
     test to know when the reading is finished.  Notice that
     apr_full_read()'s documentation says that it's possible for
     APR_EOF to be returned and bytes *still* be read into buffer!
     Therfore, if apr_full_read() does this, the caller will call this
     routine one more time, and *len should then be set to 0 for sure. */

  if (stat && !APR_STATUS_IS_EOF(stat)) 
    return
      svn_error_create (stat, 0, NULL, pool,
                        "my_read_func: error reading xmlfile");
  
  return SVN_NO_ERROR;  
}


int
main (int argc, char *argv[])
{
  apr_pool_t *globalpool;
  const svn_delta_edit_fns_t *editor;
  svn_error_t *err;
  apr_file_t *source_baton = NULL;
  apr_status_t status;
  void *edit_baton;

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
  status = apr_open (&source_baton, argv[1],
                     APR_READ, APR_OS_DEFAULT, globalpool);
  if (status)
    {
      printf ("Error opening %s\n.", argv[1]);
      exit (1);
    }
    

  /* Set context variables for evaluating a tree-delta */
  base_revision = 37;
  base_path = svn_string_create ("/root", globalpool);
  
  /* Grab the "test" editor and baton */
  err = svn_test_get_editor (&editor, &edit_baton,
                             base_path, base_revision, globalpool);
  
  /* Fire up the XML parser */
  err = svn_delta_xml_auto_parse (my_read_func, source_baton, 
                                  editor,
                                  edit_baton,
                                  base_path,
                                  base_revision,
                                  globalpool);

  apr_close (source_baton);
  
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      apr_destroy_pool (globalpool);
      exit (err->apr_err);
    }

  apr_destroy_pool (globalpool);
  exit (0);
}
