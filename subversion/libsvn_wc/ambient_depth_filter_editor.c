/*
 * ambient_depth_filter_editor.c -- provide a svn_delta_editor_t which wraps
 *                                  another editor and provides
 *                                  *ambient* depth-based filtering
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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

#include "svn_delta.h"
#include "svn_wc.h"
#include "svn_path.h"
#include "wc.h"

/*
     Notes on the general depth-filtering strategy.
     ==============================================

     When a depth-aware (>= 1.5) client pulls an update from a
     non-depth-aware server, the server may send back too much data,
     because it doesn't hear what the client tells it about the
     "requested depth" of the update (the "foo" in "--depth=foo"), nor
     about the "ambient depth" of each working copy directory.

     For example, suppose a 1.5 client does this against a 1.4 server:

       $ svn co --depth=empty -rSOME_OLD_REV http://url/repos/blah/ wc
       $ cd wc
       $ svn up

     In the initial checkout, the requested depth is 'empty', so the
     depth-filtering editor (see libsvn_delta/depth_filter_editor.c)
     that wraps the main update editor transparently filters out all
     the unwanted calls.

     In the 'svn up', the requested depth is unspecified, meaning that
     the ambient depth(s) of the working copy should be preserved.
     Since there's only one directory, and its depth is 'empty',
     clearly we should filter out or render no-ops all editor calls
     after open_root(), except maybe for change_dir_prop() on the
     top-level directory.  (Note that the server will have stuff to
     send down, because we checked out at an old revision in the first
     place, to set up this scenario.)

     The depth-filtering editor won't help us here.  It only filters
     based on the requested depth, it never looks in the working copy
     to get ambient depths.  So the update editor itself will have to
     filter out the unwanted calls -- or better yet, it will have to
     be wrapped in a filtering editor that does the job.

     This is that filtering editor.

     Most of the work is done at the moment of baton construction.
     When a file or dir is opened, we create its baton with the
     appropriate ambient depth, either taking the depth directly from
     the corresponding working copy object (if available), or from its
     parent baton.  In the latter case, we don't just copy the parent
     baton's depth, but rather use it to choose the correct depth for
     this child.  The usual depth demotion rules apply, with the
     additional stipulation that as soon as we find a subtree is not
     present at all, due to being omitted for depth reasons, we set the
     ambiently_excluded flag in its baton, which signals that
     all descendant batons should be ignored.
     (In fact, we may just re-use the parent baton, since none of the
     other fields will be used anyway.)

     See issues #2842 and #2897 for more.
*/


/*** Batons, and the Toys That Create Them ***/

struct edit_baton
{
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;
  const char *anchor;
  const char *target;
  svn_wc_adm_access_t *adm_access;
};

struct file_baton
{
  svn_boolean_t ambiently_excluded;
  struct edit_baton *edit_baton;
  void *wrapped_baton;
};

struct dir_baton
{
  svn_boolean_t ambiently_excluded;
  svn_depth_t ambient_depth;
  struct edit_baton *edit_baton;
  const char *path;
  void *wrapped_baton;
};

static svn_error_t *
make_dir_baton(struct dir_baton **d_p,
               const char *path,
               struct edit_baton *eb,
               struct dir_baton *pb,
               apr_pool_t *pool)
{
  struct dir_baton *d;

  SVN_ERR_ASSERT(path || (! pb));

  if (pb && pb->ambiently_excluded)
    {
      /* Just re-use the parent baton, since the only field that
         matters is ambiently_excluded. */
      *d_p = pb;
      return SVN_NO_ERROR;
    }

  /* Okay, no easy out, so allocate and initialize a dir baton. */
  d = apr_pcalloc(pool, sizeof(*d));

  d->path = apr_pstrdup(pool, eb->anchor);
  if (path)
    d->path = svn_path_join(d->path, path, pool);

  /* The svn_depth_unknown means that: 1) pb is the anchor; 2) there
     is an non-null target, for which we are preparing the baton.
     This enables explicitly pull in the target. */
  if (pb && pb->ambient_depth != svn_depth_unknown)
    {
      const svn_wc_entry_t *entry;
      svn_boolean_t exclude;

      SVN_ERR(svn_wc_entry(&entry, d->path, eb->adm_access, TRUE, pool));
      if (pb->ambient_depth == svn_depth_empty
          || pb->ambient_depth == svn_depth_files)
        {
          /* This is not a depth upgrade, and the parent directory is
             depth==empty or depth==files.  So if the parent doesn't
             already have an entry for the new dir, then the parent
             doesn't want the new dir at all, thus we should initialize
             it with ambiently_excluded=TRUE. */
          exclude = (entry == NULL);
        }
      else
        {
          /* If the parent expect all children by default, only exclude
             it whenever it is explicitly marked as exclude. */
          exclude = (entry && (entry->depth == svn_depth_exclude));
        }
      if (exclude)
        {
          d->ambiently_excluded = TRUE;
          *d_p = d;
          return SVN_NO_ERROR;
        }
    }

  d->edit_baton = eb;
  /* We'll initialize this differently in add_directory and
     open_directory. */
  d->ambient_depth = svn_depth_unknown;

  *d_p = d;
  return SVN_NO_ERROR;
}

static svn_error_t *
make_file_baton(struct file_baton **f_p,
                struct dir_baton *pb,
                const char *path,
                apr_pool_t *pool)
{
  struct file_baton *f = apr_pcalloc(pool, sizeof(*f));

  SVN_ERR_ASSERT(path);

  if (pb->ambiently_excluded)
    {
      f->ambiently_excluded = TRUE;
      *f_p = f;
      return SVN_NO_ERROR;
    }

  if (pb->ambient_depth == svn_depth_empty)
    {
      /* This is not a depth upgrade, and the parent directory is
         depth==empty.  So if the parent doesn't
         already have an entry for the file, then the parent
         doesn't want to hear about the file at all. */
      const svn_wc_entry_t *entry;

      SVN_ERR(svn_wc_entry(&entry,
                           svn_path_join(pb->edit_baton->anchor, path, pool),
                           pb->edit_baton->adm_access, FALSE, pool));
      if (! entry)
        {
          f->ambiently_excluded = TRUE;
          *f_p = f;
          return SVN_NO_ERROR;
        }
    }

  f->edit_baton = pb->edit_baton;

  *f_p = f;
  return SVN_NO_ERROR;
}


/*** Editor Functions ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  /* Nothing depth-y to filter here. */
 return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                target_revision, pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *b;

  SVN_ERR(make_dir_baton(&b, NULL, eb, NULL, pool));
  *root_baton = b;

  if (b->ambiently_excluded)
    return SVN_NO_ERROR;

  if (! *eb->target)
    {
      /* For an update with a NULL target, this is equivalent to open_dir(): */
      const svn_wc_entry_t *entry;

      /* Read the depth from the entry. */
      SVN_ERR(svn_wc_entry(&entry, b->path, eb->adm_access,
                           FALSE, pool));
      if (entry)
        b->ambient_depth = entry->depth;
    }

  return eb->wrapped_editor->open_root(eb->wrapped_edit_baton, base_revision,
                                       pool, &b->wrapped_baton);
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  if (pb->ambiently_excluded)
    return SVN_NO_ERROR;

  if (pb->ambient_depth < svn_depth_immediates)
    {
      /* If the entry we want to delete doesn't exist, that's OK.
         It's probably an old server that doesn't understand
         depths. */
      const svn_wc_entry_t *entry;
      const char *full_path = svn_path_join(eb->anchor, path,
                                            pool);

      SVN_ERR(svn_wc_entry(&entry, full_path,
                           eb->adm_access, FALSE, pool));
      if (! entry)
        return SVN_NO_ERROR;
    }

  return eb->wrapped_editor->delete_entry(path, base_revision,
                                          pb->wrapped_baton, pool);
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *b = NULL;

  SVN_ERR(make_dir_baton(&b, path, eb, pb, pool));
  *child_baton = b;

  if (b->ambiently_excluded)
    return SVN_NO_ERROR;

  /* It's not excluded, so what should we treat the ambient depth as
     being? */
  if (strcmp(eb->target, path) == 0)
    {
      /* The target of the edit is being added, so make it
         infinity. */
      b->ambient_depth = svn_depth_infinity;
    }
  else if (pb->ambient_depth == svn_depth_immediates)
    {
      b->ambient_depth = svn_depth_empty;
    }
  else
    {
      /* There may be a requested depth < svn_depth_infinity, but
         that's okay, libsvn_delta/depth_filter_editor.c will filter
         further calls out for us anyway, and the update_editor will
         do the right thing when it creates the directory. */
      b->ambient_depth = svn_depth_infinity;
    }

  return eb->wrapped_editor->add_directory(path, pb->wrapped_baton,
                                           copyfrom_path,
                                           copyfrom_revision,
                                           pool, &b->wrapped_baton);
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *b;
  const svn_wc_entry_t *entry;

  SVN_ERR(make_dir_baton(&b, path, eb, pb, pool));
  *child_baton = b;

  if (b->ambiently_excluded)
    return SVN_NO_ERROR;

  SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_baton,
                                             base_revision, pool,
                                             &b->wrapped_baton));
  /* Note that for the update editor, the open_directory above will
     flush the logs of pb's directory, which might be important for
     this svn_wc_entry call. */
  SVN_ERR(svn_wc_entry(&entry, b->path, eb->adm_access, FALSE, pool));
  if (entry)
    b->ambient_depth = entry->depth;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *b = NULL;

  SVN_ERR(make_file_baton(&b, pb, path, pool));
  *child_baton = b;

  if (b->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->add_file(path, pb->wrapped_baton,
                                      copyfrom_path, copyfrom_revision,
                                      pool, &b->wrapped_baton);
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *b;

  SVN_ERR(make_file_baton(&b, pb, path, pool));
  *child_baton = b;
  if (b->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->open_file(path, pb->wrapped_baton,
                                       base_revision, pool,
                                       &b->wrapped_baton);
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  /* For filtered files, we just consume the textdelta. */
  if (fb->ambiently_excluded)
    {
      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;
      return SVN_NO_ERROR;
    }

  return eb->wrapped_editor->apply_textdelta(fb->wrapped_baton,
                                             base_checksum, pool,
                                             handler, handler_baton);;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  if (fb->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->close_file(fb->wrapped_baton,
                                        text_checksum, pool);
}

static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  if (pb->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->absent_file(path, pb->wrapped_baton, pool);
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  if (db->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->close_directory(db->wrapped_baton, pool);
}

static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  /* Don't report absent items in filtered directories. */
  if (pb->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->absent_directory(path, pb->wrapped_baton, pool);
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  if (fb->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->change_file_prop(fb->wrapped_baton,
                                              name, value, pool);
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  if (db->ambiently_excluded)
    return SVN_NO_ERROR;

  return eb->wrapped_editor->change_dir_prop(db->wrapped_baton,
                                             name, value, pool);
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

svn_error_t *
svn_wc__ambient_depth_filter_editor(const svn_delta_editor_t **editor,
                                    void **edit_baton,
                                    const svn_delta_editor_t *wrapped_editor,
                                    void *wrapped_edit_baton,
                                    const char *anchor,
                                    const char *target,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool)
{
  svn_delta_editor_t *depth_filter_editor;
  struct edit_baton *eb;

  depth_filter_editor = svn_delta_default_editor(pool);
  depth_filter_editor->set_target_revision = set_target_revision;
  depth_filter_editor->open_root = open_root;
  depth_filter_editor->delete_entry = delete_entry;
  depth_filter_editor->add_directory = add_directory;
  depth_filter_editor->open_directory = open_directory;
  depth_filter_editor->change_dir_prop = change_dir_prop;
  depth_filter_editor->close_directory = close_directory;
  depth_filter_editor->absent_directory = absent_directory;
  depth_filter_editor->add_file = add_file;
  depth_filter_editor->open_file = open_file;
  depth_filter_editor->apply_textdelta = apply_textdelta;
  depth_filter_editor->change_file_prop = change_file_prop;
  depth_filter_editor->close_file = close_file;
  depth_filter_editor->absent_file = absent_file;
  depth_filter_editor->close_edit = close_edit;

  eb = apr_palloc(pool, sizeof(*eb));
  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->anchor = anchor;
  eb->target = target;
  eb->adm_access = adm_access;

  *editor = depth_filter_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}
