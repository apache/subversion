/* 
   A simple demo of how to use Subversion's XML parser interface. 
*/


#include <stdio.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "apr_pools.h"
#include "apr_file_io.h"



/* ACK!  A GLOBAL VARIABLE!  What kind of program is this?  Must be
   some lame test program or something.  :)  Yeah, whatever. */

apr_pool_t *globalpool;


struct edit_baton
{
  int indentation; /* just an indentation count */
  svn_string_t *root_path;
  apr_pool_t *pool;
};


struct dir_baton
{
  svn_string_t *path;
  struct edit_baton *edit_baton;
};


struct file_baton
{
  svn_string_t *path;
  struct dir_baton *dir_baton;
};



/* For making formatting all purty. */
static void
print_spaces (int total)
{
  int i;

  for (i = 0; i < total; i++)
    printf(" ");
}

static void
inc_spaces (struct edit_baton *eb)
{
  eb->indentation += 4;
}

static void
dec_spaces (struct edit_baton *eb)
{
  eb->indentation -= 4;
}


/* A dummy routine designed to consume windows of vcdiff data, (of
   type svn_text_delta_window_handler_t).  This will be called by the
   vcdiff parser everytime it has a window ready to go. */
static svn_error_t *
my_vcdiff_windoweater (svn_txdelta_window_t *window, void *baton)
{
  struct file_baton *fb = (struct file_baton *) baton;
  int i;

  /* Delve into the vcdiff window and print the data. */
  for (i = 0; i < window->num_ops; i++)
    {
      switch (window->ops[i].action_code)
        {
        case svn_txdelta_new:
          {
            char *startaddr = (window->new->data +
                                (window->ops[i].offset));
            svn_string_t *str = 
              svn_string_ncreate (startaddr,
                                  (window->ops[i].length),
                                  globalpool);
            
            print_spaces (fb->dir_baton->edit_baton->indentation);
            printf ("-- got txdelta window -- : new text: [%s]\n", str->data);
          }
        case svn_txdelta_source:
          {
          }
        case svn_txdelta_target:
          {
          }
        default:
          {
          }
        }
              
    }


  return SVN_NO_ERROR;
}



/* A bunch of dummy callback routines.  */

static svn_error_t *
test_delete (svn_string_t *filename, void *parent_baton)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  char *Aname = filename->data ? filename->data : "(unknown)";

  print_spaces (d->edit_baton->indentation);

  printf ("DELETE file '%s'\n", Aname);
  return SVN_NO_ERROR;         
}


static svn_error_t *
test_replace_root (void *edit_baton,
                   void **root_baton)
{
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  struct dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->path = (svn_string_t *) svn_string_dup (eb->root_path, globalpool);
  d->edit_baton = eb;
  *root_baton = d;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_replace_dir (svn_string_t *name,
                    void *parent_baton,
                    svn_string_t *ancestor_path,
                    long int ancestor_version,
                    void **child_baton,
                    const char *pivot_string)
{
  struct dir_baton *pd = (struct dir_baton *) parent_baton;
  char *Aname = name ? name->data : "(unknown)";
  char *ancestor = ancestor_path ? ancestor_path->data : "(unknown)";
  struct dir_baton *d;

  inc_spaces (pd->edit_baton);
  print_spaces (pd->edit_baton->indentation);

  printf ("%s:  name '%s', ancestor '%s' version %ld\n",
          pivot_string, Aname, ancestor, ancestor_version);

  /* Set child_baton to a new dir baton. */
  d = apr_pcalloc (pd->edit_baton->pool, sizeof (*d));
  d->path = svn_string_dup (pd->path, pd->edit_baton->pool);
  svn_path_add_component (d->path,
                          svn_string_create (Aname, pd->edit_baton->pool),
                          SVN_PATH_LOCAL_STYLE,
                          pd->edit_baton->pool);
  d->edit_baton = pd->edit_baton;
  *child_baton = d;
  
  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_directory (svn_string_t *name,
                    void *parent_baton,
                    svn_string_t *ancestor_path,
                    long int ancestor_version,
                    void **child_baton)
{
  return add_or_replace_dir (name,
                             parent_baton,
                             ancestor_path,
                             ancestor_version,
                             child_baton,
                             "ADD_DIR");
}


static svn_error_t *
test_replace_directory (svn_string_t *name,
                        void *parent_baton,
                        svn_string_t *ancestor_path,
                        long int ancestor_version,
                        void **child_baton)
{
  return add_or_replace_dir (name,
                             parent_baton,
                             ancestor_path,
                             ancestor_version,
                             child_baton,
                             "REPLACE_DIR");
}


static svn_error_t *
test_close_directory (void *dir_baton)
{
  struct dir_baton *d = (struct dir_baton *) dir_baton;
  print_spaces (d->edit_baton->indentation);
  dec_spaces (d->edit_baton);

  if (d->path)
    printf ("CLOSE_DIR '%s'\n", d->path->data);
  else 
    printf ("CLOSE_DIR:  no name!!\n");

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_close_file (void *file_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  print_spaces (fb->dir_baton->edit_baton->indentation);
  dec_spaces (fb->dir_baton->edit_baton);

  if (file_baton)
    printf ("CLOSE_FILE '%s'\n", fb->path->data);
  else
    printf ("CLOSE_FILE:  no name!!\n");

  return SVN_NO_ERROR;    
}


static svn_error_t *
test_apply_textdelta (void *file_baton,
                      svn_txdelta_window_handler_t **handler,
                      void **handler_baton)
{
  struct file_baton *fb = (struct file_baton *) file_baton;

  char *Aname = fb->path ? fb->path->data : "(unknown)";

  print_spaces (fb->dir_baton->edit_baton->indentation);

  printf ("TEXT-DELTA on file '%s':\n", Aname);

  /* Set the value of HANDLER and HANDLER_BATON here */
  *handler        = my_vcdiff_windoweater;
  *handler_baton  = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
add_or_replace_file (svn_string_t *name,
                     void *parent_baton,
                     svn_string_t *ancestor_path,
                     long int ancestor_version,
                     void **file_baton,
                     const char *pivot_string)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  struct file_baton *fb;
  char *Aname = name ? name->data : "(unknown)";
  char *ancestor = ancestor_path ? ancestor_path->data : "(unknown)";

  inc_spaces (d->edit_baton);
  print_spaces (d->edit_baton->indentation);

  printf ("%s:  name '%s', ancestor '%s' version %ld\n",
          pivot_string, Aname, ancestor, ancestor_version);

  /* Put the filename in file_baton */
  fb = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));
  fb->dir_baton = parent_baton;
  fb->path = (svn_string_t *) svn_string_dup (name, globalpool);
  *file_baton = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
test_add_file (svn_string_t *name,
               void *parent_baton,
               svn_string_t *ancestor_path,
               long int ancestor_version,
               void **file_baton)
{
  return add_or_replace_file (name,
                              parent_baton,
                              ancestor_path,
                              ancestor_version,
                              file_baton,
                              "ADD_FILE");
}


static svn_error_t *
test_replace_file (svn_string_t *name,
                   void *parent_baton,
                   svn_string_t *ancestor_path,
                   long int ancestor_version,
                   void **file_baton)
{
  return add_or_replace_file (name,
                              parent_baton,
                              ancestor_path,
                              ancestor_version,
                              file_baton,
                              "REPLACE_FILE");
}


static svn_error_t *
test_change_file_prop (void *file_baton,
                       svn_string_t *name, svn_string_t *value)
{
  struct file_baton *fb = (struct file_baton *) file_baton;
  print_spaces (fb->dir_baton->edit_baton->indentation);

  printf ("PROPCHANGE on file '%s': ", fb->path->data);

  if (value == NULL)
    printf (" delete `%s'\n", (char *) name->data);

  else
    printf (" set `%s' to `%s'\n",
            (char *) name->data, (char *) value->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_change_dir_prop (void *parent_baton,
                      svn_string_t *name, svn_string_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  print_spaces (d->edit_baton->indentation);

  printf ("PROPCHANGE on directory '%s': ", d->path->data);

  if (value == NULL)
    printf (" delete `%s'\n", (char *) name->data);

  else
    printf (" set  `%s' to `%s'\n",
            (char *) name->data, (char *) value->data);

  return SVN_NO_ERROR;
}


static svn_error_t *
test_change_dirent_prop (void *parent_baton,
                         svn_string_t *entry,
                         svn_string_t *name, svn_string_t *value)
{
  struct dir_baton *d = (struct dir_baton *) parent_baton;
  print_spaces (d->edit_baton->indentation);

  printf ("PROPCHANGE on dir-entry '%s': ", (char *) entry->data);

  if (value == NULL)
    printf (" delete  `%s'\n", (char *) name->data);

  else
    printf (" set `%s' to `%s'\n",
            (char *) name->data, (char *) value->data);

  return SVN_NO_ERROR;
}









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

  if (stat && (stat != APR_EOF)) 
    return
      svn_error_create (stat, 0, NULL, pool,
                        "my_read_func: error reading xmlfile");
  
  return SVN_NO_ERROR;  
}


int
main (int argc, char *argv[])
{
  svn_delta_edit_fns_t my_editor;
  svn_error_t *err;
  apr_file_t *source_baton = NULL;
  apr_status_t status;
  struct edit_baton *my_edit_baton;

  svn_vernum_t base_version;
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
  globalpool = svn_pool_create (NULL, NULL);

  /* Open a file full of XML, create "source baton" (the filehandle)
     that my_read_func() will slurp XML from. */
  status = apr_open (&source_baton, argv[1],
                     APR_READ, APR_OS_DEFAULT, globalpool);
  if (status)
    {
      printf ("Error opening %s\n.", argv[1]);
      exit (1);
    }
    
  
  /* Fill out a editor structure, with our own routines inside it. */
  my_editor.delete             = test_delete;

  my_editor.replace_root       = test_replace_root;
  my_editor.add_directory      = test_add_directory;
  my_editor.replace_directory  = test_replace_directory;
  my_editor.close_directory    = test_close_directory;

  my_editor.add_file           = test_add_file;
  my_editor.replace_file       = test_replace_file;
  my_editor.close_file         = test_close_file;

  my_editor.apply_textdelta    = test_apply_textdelta;

  my_editor.change_file_prop   = test_change_file_prop;
  my_editor.change_dir_prop    = test_change_dir_prop;
  my_editor.change_dirent_prop = test_change_dirent_prop;

  my_editor.close_edit         = NULL;

  /* Set context variables for evaluating a tree-delta */
  base_version = 37;
  base_path = svn_string_create ("/root", globalpool);

  /* Set up the edit baton. */
  my_edit_baton = apr_pcalloc (globalpool, sizeof (*my_edit_baton));
  my_edit_baton->pool = globalpool;
  my_edit_baton->root_path = base_path;

  /* Fire up the XML parser */
  err = svn_delta_xml_auto_parse (my_read_func, source_baton, 
                                  &my_editor,
                                  base_path,
                                  base_version,
                                  my_edit_baton,
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
