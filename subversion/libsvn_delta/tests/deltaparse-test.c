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
   type svn_text_delta_window_handler_t).  This will be called by the
   vcdiff parser everytime it has a window ready to go. */
svn_error_t *
my_vcdiff_windoweater (svn_delta_window_t *window, void *baton)
{
  int i;

  /* Delve into the vcdiff window and print the data. */
  for (i = 0; i < window->num_ops; i++)
    {
      switch (window->ops[i].action_code)
        {
        case svn_delta_new:
          {
            char *startaddr = (window->new->data +
                                (window->ops[i].offset));
            svn_string_t *str = 
              svn_string_ncreate (startaddr,
                                  (window->ops[i].length),
                                  globalpool);

            printf ("--- new datawindow -- : [%s]\n", str->data);
          }
        case svn_delta_source:
          {
          }
        case svn_delta_target:
        default:
          {
          }
        }
              
    }

  /* This deallocates the whole subpool created to hold the window.
     We're done with the window, so we should clean up. */
  svn_free_delta_window (window);

  return SVN_NO_ERROR;
}



/* A bunch of dummy callback routines.  */

svn_error_t *
test_delete (svn_string_t *filename, void *walk_baton, void *parent_baton)
{
  printf ("DELETE event:  delete filename '%s'\n", filename->data);
  return SVN_NO_ERROR;         
}

svn_error_t *
test_add_directory (svn_string_t *name,
                    void *walk_baton, void *parent_baton,
                    svn_string_t *base_path,
                    long int base_version,
                    void **child_baton)
{
  printf ("ADD_DIR event:  name '%s', ancestor '%s' version %d\n",
          name->data, base_path->data, base_version);
  
  return SVN_NO_ERROR;
}


svn_error_t *
test_replace_directory (svn_string_t *name,
                        void *walk_baton, void *parent_baton,
                        svn_string_t *base_path,
                        long int base_version,
                        void **child_baton)
{
  printf ("REPLACE_DIR event:  name '%s', ancestor '%s' version %d\n",
          name->data, base_path->data, base_version);
  
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
test_begin_textdelta (void *walk_baton, void *parent_baton,
                      svn_text_delta_window_handler_t **handler,
                      void **handler_baton)
{
  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
test_add_file (svn_string_t *name,
               void *walk_baton, void *parent_baton,
               svn_string_t *base_path,
               long int base_version)
{
  printf ("ADD_FILE event:  name '%s', ancestor '%s' version %d\n",
          name->data, base_path->data, base_version);
  
  return SVN_NO_ERROR;
}



svn_error_t *
test_replace_file (svn_string_t *name,
                   void *walk_baton, void *parent_baton,
                   svn_string_t *base_path,
                   long int base_version)
{
  printf ("REPLACE_FILE event:  name '%s', ancestor '%s' version %d\n",
          name->data, base_path->data, base_version);
  
  return SVN_NO_ERROR;
}




/* An official subversion "read" routine, comforming to POSIX standards. 
   This one reads our XML filehandle, passed in as our baton.  */
svn_error_t *
my_read_func (void *baton, char *buffer, apr_off_t *len, apr_pool_t *pool)
{
  svn_error_t *err;
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

  if (stat && (stat != APR_EOF)) 
    return
      svn_create_error (stat, 0, "my_read_func: error reading xmlfile",
                        NULL, pool);
  
  return SVN_NO_ERROR;  
}


int main()
{
  svn_delta_walk_t my_walker;
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
  my_walker.finish_directory   = test_finish_directory;
  my_walker.finish_file        = test_finish_file;
  my_walker.add_file           = test_add_file;
  my_walker.replace_file       = test_replace_file;
  my_walker.begin_textdelta    = test_begin_textdelta;
  my_walker.begin_propdelta    = NULL;
  my_walker.finish_textdelta   = NULL;
  my_walker.finish_propdelta   = NULL;

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
