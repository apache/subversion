/* update-editor.c --- a 'pipe' editor that intercepts dir_delta()'s
 *                     drive of the wc update editor.
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


#include <string.h>

#include "apr_pools.h"
#include "apr_file_io.h"

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "ra_local.h"



/*** Editor batons. ***/

struct edit_baton
{
  /* Private pool for allocating my own batons and doing path telescoping. */
  apr_pool_t *pool;

  /* The active RA session;  important, because it contains the open fs. */
  svn_ra_local__session_baton_t *session;

  /* Location in fs where where the edit will begin. */
  svn_stringbuf_t *base_path;

  /* A cached root object for the revision we're updating to (will be
     set by set_target_revision) */
  svn_fs_root_t *root;

};


/* NOTE: There are no custom dir or file batons defined here; instead,
   the svn_pipe_dir_baton and svn_pipe_file_baton have (void *) fields
   that will simply point to a telescoping svn_stringbuf_t path.  */



/*** Helpers ***/

/* Stolen from ra_dav:fetch.c.  :-)  */
typedef svn_error_t * (*prop_setter_t) (void *baton,
                                        svn_stringbuf_t *name,
                                        svn_stringbuf_t *value);

/* Fetch any 'entry props' for ROOT/PATH.  Then depending on the value
   of IS_DIR, push these properties to REAL_EDITOR (using REAL_BATON)
   via change_file_prop() or change_dir_prop().  */
static svn_error_t *
send_entry_props (svn_fs_root_t *root,
                  const svn_string_t *path,
                  const svn_delta_edit_fns_t *real_editor,
                  void *real_baton,
                  svn_boolean_t is_dir,
                  apr_pool_t *pool)
{
  svn_revnum_t committed_rev;
  svn_string_t *committed_date, *last_author;
  svn_stringbuf_t *name, *value;
  prop_setter_t pset_func;
  char *revision_str = NULL;
  apr_pool_t *subpool = svn_pool_create (pool);

  if (is_dir)
    pset_func = real_editor->change_dir_prop;
  else
    pset_func = real_editor->change_file_prop;

  /* At this time, there are exactly three pieces of fs-specific
     information we want to fetch and send via propsets.  This
     list might grow, however. */
  SVN_ERR (svn_repos_get_committed_info (&committed_rev,
                                         &committed_date,
                                         &last_author,
                                         root, path, subpool));

  /* A root/path will always have a "created rev" field. */
  revision_str = apr_psprintf (subpool, "%ld", committed_rev);
  name = svn_stringbuf_create (SVN_PROP_ENTRY_COMMITTED_REV, subpool);
  value = svn_stringbuf_create (revision_str, subpool);
  SVN_ERR ((*pset_func) (real_baton, name, value));

  /* Give the update editor either real or NULL values for the date
     and author props. */
  name = svn_stringbuf_create (SVN_PROP_ENTRY_COMMITTED_DATE, subpool);  
  if (committed_date)
    value = svn_stringbuf_create_from_string (committed_date, subpool);
  else
    value = NULL;
  SVN_ERR ((*pset_func) (real_baton, name, value));

  name = svn_stringbuf_create (SVN_PROP_ENTRY_LAST_AUTHOR, subpool);
  if (last_author)
    value = svn_stringbuf_create_from_string (last_author, subpool);
  else
    value = NULL;
  SVN_ERR ((*pset_func) (real_baton, name, value));

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}



/*** Custom editor functions ***/


static svn_error_t *
set_target_revision (void *edit_baton, svn_revnum_t target_revision)
{
  struct svn_pipe_edit_baton *eb = edit_baton;
  struct edit_baton *my_eb = (struct edit_baton *) eb->my_baton;

  /* Call the real update editor. */
  SVN_ERR ((* (eb->real_editor->set_target_revision)) (eb->real_edit_baton,
                                                       target_revision));
  
  /* Make our own edit_baton's root object from the target revision. */
  SVN_ERR (svn_fs_revision_root (&(my_eb->root),
                                 my_eb->session->fs, target_revision,
                                 my_eb->pool));
           
  return SVN_NO_ERROR;
}



static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           void **root_baton)
{
  struct svn_pipe_edit_baton *eb = edit_baton;
  struct edit_baton *my_eb = (struct edit_baton *) eb->my_baton;
  struct svn_pipe_dir_baton *d = apr_pcalloc (eb->pool, sizeof (*d));

  d->edit_baton = eb;
  d->parent_dir_baton = NULL;

  /* Call the "real" open_root. */
  SVN_ERR ((* (eb->real_editor->open_root)) (eb->real_edit_baton,
                                             base_revision,
                                             &(d->real_dir_baton)));


  /* set the pipe_dir_baton's void pointer to a path. */
  d->my_baton = svn_string_create_from_buf (my_eb->base_path, my_eb->pool);

  /* fetch & send entry props for this path. */
  SVN_ERR (send_entry_props (my_eb->root,
                             d->my_baton, /* path */
                             eb->real_editor, d->real_dir_baton,
                             TRUE, /* is_dir */
                             my_eb->pool));

  *root_baton = d;
  
  return SVN_NO_ERROR;

}



static svn_error_t *
open_directory (svn_stringbuf_t *name,
                void *parent_baton,
                svn_revnum_t base_revision,
                void **child_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_dir_baton *child =
    apr_pcalloc (d->edit_baton->pool, sizeof (*child));
  struct edit_baton *my_eb = (struct edit_baton *) d->edit_baton->my_baton;

  /* ### can toss when svn_path has more svn_string_t ops. */
  svn_stringbuf_t *pathbuf;

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = d;

  /* Call the "real" open_directory. */
  SVN_ERR ((* (d->edit_baton->real_editor->open_directory))
           (name, d->real_dir_baton, base_revision, &(child->real_dir_baton)));

  /* set the pipe_dir_baton's void pointer to a path. */
  pathbuf =
    svn_stringbuf_create_from_string (child->parent_dir_baton->my_baton,
                                      my_eb->pool);
  svn_path_add_component (pathbuf, name);
  
  child->my_baton = svn_string_create_from_buf (pathbuf, my_eb->pool);

  /* fetch & send entry props for this path. */
  SVN_ERR (send_entry_props (my_eb->root,
                             child->my_baton, /* path */
                             d->edit_baton->real_editor, child->real_dir_baton,
                             TRUE, /* is_dir */
                             my_eb->pool));

  *child_baton = child;

  return SVN_NO_ERROR;
}



static svn_error_t *
add_directory (svn_stringbuf_t *name,
               void *parent_baton,
               svn_stringbuf_t *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               void **child_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_dir_baton *child =
    apr_pcalloc (d->edit_baton->pool, sizeof (*child));
  struct edit_baton *my_eb = (struct edit_baton *) d->edit_baton->my_baton;

  /* ### can toss when svn_path has more svn_string_t ops. */
  svn_stringbuf_t *pathbuf;

  child->edit_baton = d->edit_baton;
  child->parent_dir_baton = d;

  /* Call the "real" add_directory. */
  SVN_ERR ((* (d->edit_baton->real_editor->add_directory))
           (name, d->real_dir_baton, copyfrom_path, copyfrom_revision,
            &(child->real_dir_baton)));

  /* set the pipe_dir_baton's void pointer to a path. */
  pathbuf =
    svn_stringbuf_create_from_string (child->parent_dir_baton->my_baton,
                                      my_eb->pool);
  svn_path_add_component (pathbuf, name);
  
  child->my_baton = svn_string_create_from_buf (pathbuf, my_eb->pool);

  /* fetch & send entry props for this path. */
  SVN_ERR (send_entry_props (my_eb->root,
                             child->my_baton, /* path */
                             d->edit_baton->real_editor, child->real_dir_baton,
                             TRUE, /* is_dir */
                             my_eb->pool));

  *child_baton = child;

  return SVN_NO_ERROR;
}




static svn_error_t *
add_file (svn_stringbuf_t *name,
          void *parent_baton,
          svn_stringbuf_t *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          void **file_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_file_baton *fb
    = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));
  struct edit_baton *my_eb = (struct edit_baton *) d->edit_baton->my_baton;

  /* ### can toss when svn_path has more svn_string_t ops. */
  svn_stringbuf_t *pathbuf;

  fb->dir_baton = d;

  /* Call the "real" add_file. */
  SVN_ERR ((* (d->edit_baton->real_editor->add_file))
           (name, d->real_dir_baton, copyfrom_path, 
            copyfrom_revision, &(fb->real_file_baton)));

  /* set the pipe_file_baton's void pointer to a path. */
  pathbuf = svn_stringbuf_create_from_string (fb->dir_baton->my_baton,
                                              my_eb->pool);
  svn_path_add_component (pathbuf, name);
  fb->my_baton = svn_string_create_from_buf (pathbuf, my_eb->pool);

  /* fetch & send entry props for this path. */
  SVN_ERR (send_entry_props (my_eb->root,
                             fb->my_baton, /* path */
                             d->edit_baton->real_editor, fb->real_file_baton,
                             FALSE, /* is_dir */
                             my_eb->pool));

  *file_baton = fb;
  return SVN_NO_ERROR;
}




static svn_error_t *
open_file (svn_stringbuf_t *name,
           void *parent_baton,
           svn_revnum_t base_revision,
           void **file_baton)
{
  struct svn_pipe_dir_baton *d = parent_baton;
  struct svn_pipe_file_baton *fb
    = apr_pcalloc (d->edit_baton->pool, sizeof (*fb));
  struct edit_baton *my_eb = (struct edit_baton *) d->edit_baton->my_baton;

  /* ### can toss when svn_path has more svn_string_t ops. */
  svn_stringbuf_t *pathbuf;

  fb->dir_baton = d;

  /* Call the "real" open_file. */
  SVN_ERR ((* (d->edit_baton->real_editor->open_file))
           (name, d->real_dir_baton, base_revision, &(fb->real_file_baton)));

  /* set the pipe_file_baton's void pointer to a path. */
  pathbuf = svn_stringbuf_create_from_string (fb->dir_baton->my_baton,
                                              my_eb->pool);
  svn_path_add_component (pathbuf, name);
  fb->my_baton = svn_string_create_from_buf (pathbuf, my_eb->pool);

  /* fetch & send entry props for this path. */
  SVN_ERR (send_entry_props (my_eb->root,
                             fb->my_baton, /* path */
                             d->edit_baton->real_editor, fb->real_file_baton,
                             FALSE, /* is_dir */
                             my_eb->pool));

  *file_baton = fb;
  return SVN_NO_ERROR;
}




/*** Public interface. ***/

svn_error_t *
svn_ra_local__get_update_pipe_editor (svn_delta_edit_fns_t **editor,
                                      struct svn_pipe_edit_baton **edit_baton,
                                      const svn_delta_edit_fns_t *update_editor,
                                      void *update_edit_baton,
                                      svn_ra_local__session_baton_t *session,
                                      svn_stringbuf_t *base_path,
                                      apr_pool_t *pool)
{
  svn_delta_edit_fns_t *e;
  struct svn_pipe_edit_baton *eb;
  struct edit_baton *my_eb;

  /* Create a 'pipe' editor that wraps around the original update editor: */
  svn_delta_old_default_pipe_editor (&e, &eb,
                                     update_editor, update_edit_baton, pool);

  /* The default pipe editor just makes direct calls to the
     update-editor;  but we want to swap in 6 of our own functions
     which will send extra entry-props. */
  e->set_target_revision = set_target_revision;
  e->open_root           = open_root;
  e->open_directory      = open_directory;
  e->open_file           = open_file;
  e->add_directory       = add_directory;
  e->add_file            = add_file;

  /* Set up our -private- edit baton. */
  my_eb = apr_pcalloc (pool, sizeof(*my_eb));
  my_eb->pool = pool;
  my_eb->base_path = base_path;
  my_eb->session = session;

  /* Insert our private edit baton into the public one. */
  eb->my_baton = my_eb;

  /* Return the pipe editor. */
  *edit_baton = eb;
  *editor = e;  
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
