/* 
   A simple demo of how to use Subversion's XML parser interface. 
*/


#include <stdio.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "apr_pools.h"
#include "apr_file_io.h"



/* ACK!  A GLOBAL VARIABLE!  What kind of program is this?  Must be
   some lame test program or something.  :)  Yeah, whatever. */

apr_pool_t *globalpool;


/* A dummy routine designed to consume windows of vcdiff data, (of
   type svn_delta_handler_t).  This will be called by the vcdiff
   parser everytime it has a window ready to go. */
svn_error_t *
my_vcdiff_windoweater (svn_delta_window_t *window, void *baton)
{
  printf ("Windoweater: yum, got me a window of vcdiff data!\n");
  
  /* TODO:  delve into the vcdiff window and print the data. */

  /* This deallocates the whole subpool created to hold the window. */
  svn_free_delta_window (window);

  return SVN_NO_ERROR;
}



/* A bunch of dummy callback routines.  */

svn_error_t *
test_delete (svn_string_t *filename, void *walk_baton, void *parent_baton)
{
  printf ("DELETE event:  delete filename '%s'\n", 
          svn_string_2cstring (filename, globalpool));

  return SVN_NO_ERROR;         
}

svn_error_t *
test_add_directory (svn_string_t *name,
                    void *walk_baton, void *parent_baton,
                    svn_string_t *base_path,
                    long int base_version,
                    svn_pdelta_t *pdelta,
                    void **child_baton)
{
  printf ("ADD_DIR event:  name '%s', ancestor '%s' version %d\n",
          svn_string_2cstring (name, globalpool),
          svn_string_2cstring (base_path, globalpool), base_version);
  
  return SVN_NO_ERROR;
}


svn_error_t *
test_replace_directory (svn_string_t *name,
                        void *walk_baton, void *parent_baton,
                        svn_string_t *base_path,
                        long int base_version,
                        svn_pdelta_t *pdelta,
                        void **child_baton)
{
  printf ("REPLACE_DIR event:  name '%s', ancestor '%s' version %d\n",
          svn_string_2cstring (name, globalpool),
          svn_string_2cstring (base_path, globalpool), base_version);
  
  return SVN_NO_ERROR;
}


svn_error_t *
test_finish_directory (void *baton)
{
  printf ("FINISH_DIR event.\n");

  return SVN_NO_ERROR;    
}


svn_error_t *
test_finish_file (void *baton)
{
  printf ("FINISH_FILE event.\n");

  return SVN_NO_ERROR;    
}


svn_error_t *
test_add_file (svn_string_t *name,
               void *walk_baton, void *parent_baton,
               svn_string_t *base_path,
               long int base_version,
               svn_pdelta_t *pdelta,
               svn_delta_handler_t **handler,
               void **handler_baton)
{
  printf ("ADD_FILE event:  name '%s', ancestor '%s' version %d\n",
          svn_string_2cstring (name, globalpool),
          svn_string_2cstring (base_path, globalpool), base_version);
  
  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = NULL;

  return SVN_NO_ERROR;
}



svn_error_t *
test_replace_file (svn_string_t *name,
                   void *walk_baton, void *parent_baton,
                   svn_string_t *base_path,
                   long int base_version,
                   svn_pdelta_t *pdelta,
                   svn_delta_handler_t **handler,
                   void **handler_baton)
{
  printf ("REPLACE_FILE event:  name '%s', ancestor '%s' version %d\n",
          svn_string_2cstring (name, globalpool),
          svn_string_2cstring (base_path, globalpool), base_version);
  
  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = NULL;

  return SVN_NO_ERROR;
}




/* An official subversion "read" routine, comforming to POSIX standards. 
   This one reads our XML filehandle, passed in as our baton.  */
svn_error_t *
my_read_func (void *baton, char *buffer, apr_off_t *len)
{
  /* Recover our filehandle */
  apr_file_t *xmlfile = (apr_file_t *) baton;

  /* TODO: read *len bytes from the file into buffer, and set *len to
     the number actually read.  Return 0 if we got an EOF.  */

  

}





int main()
{
  svn_walk_t my_walker;
  svn_error_t *err;
  apr_file_t *source_baton = NULL;
  void *foo_baton = NULL;
  void *bar_baton = NULL;

  /* Init global memory pool */
  apr_initialize ();
  apr_create_pool (&globalpool, NULL);


  /* Open a file full of XML, create "source baton" (the filehandle)
     that my_read_func() will slurp XML from. */
  apr_open (&source_baton, "foo.delta", APR_READ, APR_OS_DEFAULT, globalpool);


  /* Fill out a walk structure, with our own routines inside it. */
  my_walker.delete             = test_delete;
  my_walker.add_directory      = test_add_directory;
  my_walker.replace_directory  = test_replace_directory;
  my_walker.finish_direcory    = test_finish_directory;
  my_walker.finish_file        = test_finish_file;
  my_walker.add_file           = test_add_file;
  my_walker.replace_file       = test_replace_file;
    

  /* Fire up the XML parser */
  err = svn_delta_parse (my_read_func, source_baton, /* read from here */
                         &my_walker,                 /* call these callbacks */
                         foo_baton, bar_baton,       /* with these objects */
                         globalpool);

  apr_close (source_baton);

  if (err)
    {
      svn_handle_error (err, stderr);
      apr_destroy_pool (globalpool);
      exit (err->apr_err);
    }

  apr_destroy_pool (globalpool);
  exit (0);
}
