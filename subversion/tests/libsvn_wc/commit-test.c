/* 
   A simple test of the working copy "crawler". 

   Crawler walks a working copy and prints a virtual `commit' to stdout.
*/


#include <stdio.h>
#include "apr_pools.h"
#include "apr_file_io.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_delta.h"
#include "svn_wc.h"
#include "svn_test.h"



int
main (int argc, char *argv[])
{
  svn_error_t *err;
  apr_pool_t *globalpool;

  svn_delta_edit_fns_t *my_editor;
  void *my_edit_baton;

  svn_string_t *rootdir;

  /* Process command-line args */
  if (argc != 2)
    {
      printf 
        ("\nUsage: %s [dir]:  crawls [dir], printing `commit' to stdout.\n\n",
         argv[0]);
      exit (1);
    }

  /* Init global memory pool */
  apr_initialize ();
  globalpool = svn_pool_create (NULL);

  rootdir = svn_string_create (argv[1], globalpool);
      
  /* Get the generic dumb editor */
  err = svn_test_get_editor (&my_editor, &my_edit_baton,
                             rootdir, 59, globalpool);
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      apr_destroy_pool (globalpool);
      exit (1);
    }


  /* Call the crawler */
  err = svn_wc_crawl_local_mods (rootdir, my_editor, my_edit_baton,
                                 globalpool);
  if (err)
    {
      svn_handle_error (err, stderr, 0);
      apr_destroy_pool (globalpool);
      exit (1);
    }

  /* Close the edit */
  err = my_editor->close_edit (my_edit_baton);

  if (err)
    {
      svn_handle_error (err, stderr, 0);
      apr_destroy_pool (globalpool);
      exit (1);
    }

  apr_destroy_pool (globalpool);
  exit (0);
}
