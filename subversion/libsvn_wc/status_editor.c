/*
 * status_editor.c :  editor that implement a 'dry run' update
 *                    and tweaks status structures accordingly.
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



#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_private_config.h"

#include "wc.h"



struct edit_baton
{
  /* For status, the "destination" of the edit  and whether to honor
     any paths that are 'below'.  */
  const char *path;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t descend;

  /* The youngest revision in the repository.  This is a reference
     because this editor returns youngest rev to the driver directly,
     as well as in each statushash entry. */
  svn_revnum_t *youngest_revision;

  /* The hash of status structures we're editing. */
  apr_hash_t *statushash;

  /* The pool that will be used to add new structures to the hash,
     presumably the same one it's already been using. */
  apr_pool_t *hashpool;

  /* The pool which the editor uses for the whole tree-walk.*/
  apr_pool_t *pool;
};





/*** Helper ***/


/* Look up the key PATH in EDIT_BATON->STATUSHASH.

   If the value doesn't yet exist, create a new status struct using
   EDIT_BATON->HASHPOOL.

   Set the status structure's "network" fields to REPOS_TEXT_STATUS,
   REPOS_PROP_STATUS.  If either of these fields is 0, it will be
   ignored.  */
static svn_error_t *
tweak_statushash (void *edit_baton,
                  const char *path,
                  svn_boolean_t is_dir,
                  enum svn_wc_status_kind repos_text_status,
                  enum svn_wc_status_kind repos_prop_status)
{
  svn_wc_status_t *statstruct;
  struct edit_baton *eb = (struct edit_baton *) edit_baton;
  apr_hash_t *statushash = eb->statushash;
  apr_pool_t *pool = eb->hashpool;

  /* If you want temporary debugging info... */
  /* {
     apr_hash_index_t *hi;
     char buf[200];
     
     printf("---Tweaking statushash:  editing path `%s'\n", path);
     
     for (hi = apr_hash_first (pool, statushash); 
     hi; 
     hi = apr_hash_next (hi))
     {
     const void *key;
     void *val;
     apr_ssize_t klen;
         
     apr_hash_this (hi, &key, &klen, &val);
     snprintf(buf, klen+1, (const char *)key);
     printf("    %s\n", buf);
     }
     fflush(stdout);
     }
  */
  
  /* Is PATH already a hash-key? */
  statstruct = (svn_wc_status_t *) apr_hash_get (statushash, path,
                                                 APR_HASH_KEY_STRING);
  /* If not, make it so. */
  if (! statstruct)
    {
      /* Need a path with the same lifetime as the hash */
      const char *path_dup = apr_pstrdup (pool, path);
      svn_wc_adm_access_t *adm_access;
      if (repos_text_status == svn_wc_status_added)
        adm_access = NULL;
      else if (is_dir)
        SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access, path, pool));
      else
        SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access,
                                      svn_path_dirname (path, pool),
                                      pool));

      /* Use the public API to get a statstruct: */
      SVN_ERR (svn_wc_status (&statstruct, path, adm_access, pool));

      /* Put the path/struct into the hash. */
      apr_hash_set (statushash, path_dup, APR_HASH_KEY_STRING, statstruct);
    }

  /* Tweak the structure's repos fields. */
  if (repos_text_status)
    statstruct->repos_text_status = repos_text_status;
  if (repos_prop_status)
    statstruct->repos_prop_status = repos_prop_status;
  
  return SVN_NO_ERROR;
}




/*** batons ***/

struct dir_baton
{
  /* The path to this directory. */
  const char *path;

  /* Basename of this directory. */
  const char *name;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this directory's parent, or NULL if this is the root
     directory. */
  struct dir_baton *parent_baton;

  /* 'svn status' shouldn't print status lines for things that are
     added;  we're only interest in asking if objects that the user
     *already* has are up-to-date or not.  Thus if this flag is set,
     the next two will be ignored.  :-)  */
  svn_boolean_t added;

  /* Gets set iff there's a change to this directory's properties, to
     guide us when syncing adm files later. */
  svn_boolean_t prop_changed;

  /* This means (in terms of 'svn status') that some child was deleted
     or added to the directory */
  svn_boolean_t text_changed;

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;
};



/* Create a new dir_baton for subdir PATH. */
static struct dir_baton *
make_dir_baton (const char *path,
                struct edit_baton *edit_baton,
                struct dir_baton *parent_baton,
                apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d = apr_pcalloc (pool, sizeof (*d));
  const char *full_path; 

  /* Don't do this.  Just do NOT do this to me. */
  if (pb && (! path))
    abort();

  /* Construct the full path of this directory. */
  if (pb)
    full_path = svn_path_join (eb->path, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->path);

  /* Finish populating the baton members. */
  d->path         = full_path;
  d->name         = path ? (svn_path_basename (path, pool)) : NULL;
  d->edit_baton   = edit_baton;
  d->parent_baton = parent_baton;
  d->pool         = pool;

  return d;
}


struct file_baton
{
  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this file's parent directory. */
  struct dir_baton *dir_baton;

  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Name of this file (its entry in the directory). */
  const char *name;

  /* Path to this file, either abs or relative to the change-root. */
  const char *path;

  /* 'svn status' shouldn't print status lines for things that are
     added;  we're only interest in asking if objects that the user
     *already* has are up-to-date or not.  Thus if this flag is set,
     the next two will be ignored.  :-)  */
  svn_boolean_t added;

  /* This gets set if the file underwent a text change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t text_changed;

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;

};


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton (struct dir_baton *parent_dir_baton, 
                 const char *path,
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_dir_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *f = apr_pcalloc (pool, sizeof (*f));
  const char *full_path;
 
  /* Construct the full path of this directory. */
  if (pb)
    full_path = svn_path_join (eb->path, path, pool);
  else
    full_path = apr_pstrdup (pool, eb->path);

  /* Finish populating the baton members. */
  f->path       = full_path;
  f->name       = svn_path_basename (path, pool);
  f->pool       = pool;
  f->dir_baton  = pb;
  f->edit_baton = eb;

  return f;
}


/*----------------------------------------------------------------------*/

/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

static svn_error_t *
set_target_revision (void *edit_baton, 
                     svn_revnum_t target_revision,
                     apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  *(eb->youngest_revision) = target_revision;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  *dir_baton = make_dir_baton (NULL, eb, NULL, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry (const char *path,
              svn_revnum_t revision,
              void *parent_baton,
              apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;
  apr_hash_t *entries;
  const char *name = svn_path_basename (path, pool);
  const char *full_path = svn_path_join (eb->path, path, pool);
  const char *dir_path;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;

  /* Note:  when something is deleted, it's okay to tweak the
     statushash immediately.  No need to wait until close_file or
     close_dir, because there's no risk of having to honor the 'added'
     flag.  We already know this item exists in the working copy. */

  /* Read the parent's entries file.  If the deleted thing is not
     versioned in this working copy, it was probably deleted via this
     working copy.  No need to report such a thing. */
  /* ### use svn_wc_entry() instead? */
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind == svn_node_dir)
    dir_path = full_path;
  else
    dir_path = svn_path_dirname (full_path, pool);
  SVN_ERR (svn_wc_adm_retrieve (&adm_access, eb->adm_access, dir_path, pool));
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, FALSE, pool));
  if (apr_hash_get (entries, name, APR_HASH_KEY_STRING))
    SVN_ERR (tweak_statushash (db->edit_baton,
                               full_path, kind == svn_node_dir,
                               svn_wc_status_deleted, 0));

  /* Mark the parent dir regardless -- it lost an entry. */
  SVN_ERR (tweak_statushash (db->edit_baton,
                             db->path, kind == svn_node_dir,
                             svn_wc_status_modified, 0));

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *new_db;

  new_db = make_dir_baton (path, pb->edit_baton, pb, pool);

  /* Make this dir as added. */
  new_db->added = TRUE;

  /* Mark the parent as changed;  it gained an entry. */
  pb->text_changed = TRUE;

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory (const char *path,
                void *parent_baton,
                svn_revnum_t base_revision,
                apr_pool_t *pool,
                void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  *child_baton = make_dir_baton (path, pb->edit_baton, pb, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop (void *dir_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  if (svn_wc_is_normal_prop (name))    
    db->prop_changed = TRUE;
  return SVN_NO_ERROR;
}



static svn_error_t *
close_directory (void *dir_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;

  /* If nothing has changed, return. */
  if (! (db->added || db->prop_changed || db->text_changed))
    return SVN_NO_ERROR;

  /* If this directory was added, add the directory to the status hash. */
  if (db->added)
    SVN_ERR (tweak_statushash (db->edit_baton,
                               db->path, TRUE,
                               svn_wc_status_added,
                               db->prop_changed ? svn_wc_status_added : 0));

  /* Else, mark the existing directory in the statushash. */
  else
    SVN_ERR (tweak_statushash (db->edit_baton,
                               db->path, TRUE,
                               db->text_changed ? svn_wc_status_modified : 0,
                               db->prop_changed ? svn_wc_status_modified : 0));
  
  return SVN_NO_ERROR;
}



static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton (pb, path, pool);

  /* Mark parent dir as changed */  
  pb->text_changed = TRUE;

  /* Make this file as added. */
  new_fb->added = TRUE;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file (const char *path,
           void *parent_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton (pb, path, pool);

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
apply_textdelta (void *file_baton, 
                 apr_pool_t *pool,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  
  /* Mark file as having textual mods. */
  fb->text_changed = TRUE;

  /* Send back a NULL window handler -- we don't need the actual diffs. */
  *handler_baton = NULL;
  *handler = NULL;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  if (svn_wc_is_normal_prop (name))
    fb->prop_changed = TRUE;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_file (void *file_baton,
            apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  /* If nothing has changed, return. */
  if (! (fb->added || fb->prop_changed || fb->text_changed))
    return SVN_NO_ERROR;

  /* If this is a new file, add it to the statushash. */
  if (fb->added)
    SVN_ERR (tweak_statushash (fb->edit_baton,
                               fb->path, FALSE,
                               svn_wc_status_added, 
                               fb->prop_changed ? svn_wc_status_added : 0));
  /* Else, mark the existing file in the statushash. */
  else
    SVN_ERR (tweak_statushash (fb->edit_baton,
                               fb->path, FALSE,
                               fb->text_changed ? svn_wc_status_modified : 0,
                               fb->prop_changed ? svn_wc_status_modified : 0));

  return SVN_NO_ERROR;
}


static svn_error_t *
close_edit (void *edit_baton,
            apr_pool_t *pool)
{
  /* The edit is over, free its pool. */
  svn_pool_destroy (((struct edit_baton *) edit_baton)->pool);
  return SVN_NO_ERROR;
}



/*** Returning editors. ***/


/*** Public API ***/

svn_error_t *
svn_wc_get_status_editor (const svn_delta_editor_t **editor,
                          void **edit_baton,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t descend,
                          apr_hash_t *statushash,
                          svn_revnum_t *youngest,
                          apr_pool_t *pool)
{
  struct edit_baton *eb;
  const char *anchor, *target, *tempbuf;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor (pool);

  /* Construct an edit baton. */
  eb = apr_palloc (subpool, sizeof (*eb));
  eb->pool              = subpool;
  eb->hashpool          = pool;
  eb->statushash        = statushash;
  eb->descend           = descend;
  eb->youngest_revision = youngest;
  eb->adm_access        = adm_access;

  /* Anchor target analysis, to make this editor able to match
     hash-keys already in the hash.  (svn_wc_statuses is ignorant of
     anchor/target issues.) */
  SVN_ERR (svn_wc_get_actual_target (path, &anchor, &target, pool));
  if (target)
    tempbuf = svn_path_join (anchor, target, pool);
  else
    tempbuf = apr_pstrdup (pool, anchor);


  if (strcmp (path, tempbuf) != 0)
    eb->path = "";
  else
    eb->path = anchor;

  /* Construct an editor. */
  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  *edit_baton = eb;
  *editor = tree_editor;

  return SVN_NO_ERROR;
}
