/*
 * repos_diff.c -- The diff editor for comparing two repository versions
 *
 * ====================================================================
 * Copyright (c) 2000-2008 CollabNet.  All rights reserved.
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

/* This code uses an editor driven by a tree delta between two
 * repository revisions (REV1 and REV2). For each file encountered in
 * the delta the editor constructs two temporary files, one for each
 * revision. This necessitates a separate request for the REV1 version
 * of the file when the delta shows the file being modified or
 * deleted. Files that are added by the delta do not require a
 * separate request, the REV1 version is empty and the delta is
 * sufficient to construct the REV2 version. When both versions of
 * each file have been created the diff callback is invoked to display
 * the difference between the two files.  */

#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_props.h"

#include "client.h"

#define SAVEPOINT_RELEASED -1

/* Overall crawler editor baton.  */
struct edit_baton {
  /* TARGET is a working-copy directory which corresponds to the base
     URL open in RA_SESSION below. */
  const char *target;

  /* ADM_ACCESS is an access baton that includes the TARGET directory */
  svn_wc_adm_access_t *adm_access;

  /* The callback and calback argument that implement the file comparison
     function */
  const svn_wc_diff_callbacks3_t *diff_callbacks;
  void *diff_cmd_baton;

  /* DRY_RUN is TRUE if this is a dry-run diff, false otherwise. */
  svn_boolean_t dry_run;

  /* RA_SESSION is the open session for making requests to the RA layer */
  svn_ra_session_t *ra_session;

  /* The rev1 from the '-r Rev1:Rev2' command line option */
  svn_revnum_t revision;

  /* The rev2 from the '-r Rev1:Rev2' option, specifically set by
     set_target_revision(). */
  svn_revnum_t target_revision;

  /* The path to a temporary empty file used for add/delete
     differences.  The path is cached here so that it can be reused,
     since all empty files are the same. */
  const char *empty_file;

  /* Empty hash used for adds. */
  apr_hash_t *empty_hash;

  /* Hash used to check replaced paths. Key is path relative CWD,
   * Value is *kind_action_state_t.
   * All allocations are from edit_baton's pool. */
  apr_hash_t *deleted_paths;

  /* If the func is non-null, send notifications of actions. */
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;

  /* Hold the temporary svnpatch file. */
  apr_file_t *svnpatch_file;

  /* The stream attached to the @c svnpatch_file. */
  svn_stream_t *svnpatch_stream;

  /* @c edit_baton's @c savepoint is set to 0 when initialized.  When
   * @c savepoint is equal to 0 at the time we write the close-edit
   * command, no changes svnpatch wants happened and svnpatch_file is
   * completely erased. Conversely, when @c savepoint equals to
   * SAVEPOINT_RELEASED, there are changes of svnpatch-interest and we
   * need the open-root/close-edit pair commands. */
  apr_off_t savepoint;

  /* Diff editor baton */
  svn_delta_editor_t *diff_editor;

  /* A token holder, helps to build the svnpatch. */
  int next_token;

  apr_pool_t *pool;
};

typedef struct kind_action_state_t
{
  svn_node_kind_t kind;
  svn_wc_notify_action_t action;
  svn_wc_notify_state_t state;
} kind_action_state_t;

/* Directory level baton.
 */
struct dir_baton {
  /* Gets set if the directory is added rather than replaced/unchanged. */
  svn_boolean_t added;

  /* The path of the directory within the repository */
  const char *path;

  /* The path of the directory in the wc, relative to cwd */
  const char *wcpath;

  /* The baton for the parent directory, or null if this is the root of the
     hierarchy to be compared. */
  struct dir_baton *dir_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* A cache of any property changes (svn_prop_t) received for this dir. */
  apr_array_header_t *propchanges;

  /* The pristine-property list attached to this directory. */
  apr_hash_t *pristine_props;

  /* Set when dealing with svnpatch diff. */
  const char *token;

  /* @c savepoint is set to the position of svnpatch_file's cursor at
   * the time before writing the open-dir command to the svnpatch_file.
   * See svnpatch_savepoint(). */
  apr_off_t savepoint;

  /* The pool passed in by add_dir, open_dir, or open_root.
     Also, the pool this dir baton is allocated in. */
  apr_pool_t *pool;
};

/* File level baton.
 */
struct file_baton {
  /* Gets set if the file is added rather than replaced. */
  svn_boolean_t added;

  /* The path of the file within the repository */
  const char *path;

  /* The path of the file in the wc, relative to cwd */
  const char *wcpath;

  /* The path and APR file handle to the temporary file that contains the
     first repository version.  Also, the pristine-property list of
     this file. */
  const char *path_start_revision;
  apr_file_t *file_start_revision;
  apr_hash_t *pristine_props;

  /* The path and APR file handle to the temporary file that contains the
     second repository version.  These fields are set when processing
     textdelta and file deletion, and will be NULL if there's no
     textual difference between the two revisions. */
  const char *path_end_revision;
  apr_file_t *file_end_revision;

  /* APPLY_HANDLER/APPLY_BATON represent the delta application baton. */
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  /* The overall crawler editor baton. */
  struct edit_baton *edit_baton;

  /* The directory that contains the file. */
  struct dir_baton *dir_baton;

  /* A cache of any property changes (svn_prop_t) received for this file. */
  apr_array_header_t *propchanges;

  /* Set when dealing with svnpatch diff. */
  const char *token;

  /* @c savepoint is set to the position of svnpatch_file's cursor at
   * the time before writing the open-file command to the svnpatch_file.
   * See svnpatch_savepoint(). */
  apr_off_t savepoint;

  /* The pool passed in by add_file or open_file.
     Also, the pool this file_baton is allocated in. */
  apr_pool_t *pool;
};

static const char *
make_token(char type,
           struct edit_baton *eb,
           apr_pool_t *pool)
{
  return apr_psprintf(pool, "%c%d", type, eb->next_token++);
}

/* Create a new directory baton for PATH in POOL.  ADDED is set if
 * this directory is being added rather than replaced. PARENT_BATON is
 * the baton of the parent directory (or NULL if this is the root of
 * the comparison hierarchy). The directory and its parent may or may
 * not exist in the working copy.  EDIT_BATON is the overall crawler
 * editor baton.
 */
static struct dir_baton *
make_dir_baton(const char *path,
               struct dir_baton *parent_baton,
               struct edit_baton *edit_baton,
               svn_boolean_t added,
               const char *token,
               apr_pool_t *pool)
{
  struct dir_baton *dir_baton = apr_pcalloc(pool, sizeof(*dir_baton));

  dir_baton->dir_baton = parent_baton;
  dir_baton->edit_baton = edit_baton;
  dir_baton->added = added;
  dir_baton->pool = pool;
  dir_baton->path = apr_pstrdup(pool, path);
  dir_baton->wcpath = svn_path_join(edit_baton->target, path, pool);
  dir_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  dir_baton->token = token;

  return dir_baton;
}

/* Create a new file baton for PATH in POOL, which is a child of
 * directory PARENT_PATH. ADDED is set if this file is being added
 * rather than replaced.  EDIT_BATON is a pointer to the global edit
 * baton.
 */
static struct file_baton *
make_file_baton(const char *path,
                svn_boolean_t added,
                void *edit_baton,
                struct dir_baton *parent_baton,
                const char *token,
                apr_pool_t *pool)
{
  struct file_baton *file_baton = apr_pcalloc(pool, sizeof(*file_baton));
  struct edit_baton *eb = edit_baton;

  file_baton->edit_baton = edit_baton;
  file_baton->added = added;
  file_baton->pool = pool;
  file_baton->path = apr_pstrdup(pool, path);
  file_baton->wcpath = svn_path_join(eb->target, path, pool);
  file_baton->propchanges  = apr_array_make(pool, 1, sizeof(svn_prop_t));
  file_baton->token = token;
  file_baton->dir_baton = parent_baton;

  return file_baton;
}


/* Here is a trio of functions to play with svnpatch generation at will.
 * In a repos-repos diff, there are cases when, while being driven and
 * dumping Editor Commands, we need to rollback because we realize we've
 * been misled.  The main reason why we need this trick is because
 * svnpatch doesn't want to hear about textual-modifications, which
 * Unidiff holds.  Suppose a changeset to be a simple textual
 * modification to A/D/H/psi.  In an svnpatch context, we don't know
 * this is a change we don't want until we face with psi, i.e. until
 * we've actually opened/consumed the whole stack of directories plus
 * the file and thus blindly dumped the associated commands wave.
 * Additionally, we're able to decide whether or not it was useless to
 * open an entry at the time of close.  So here we are with a mechanism
 * to set savepoints when opening, and to rollback when closing.  By
 * convention, we decide we need a rollback if the savepoint is still
 * there (positive value).  Thus we also have another function to
 * release a savepoint (i.e. set it to a negative value,
 * SAVEPOINT_RELEASED, that is) when we face a change we want (e.g. a
 * change-file-prop) so that no rollback is performed when closing.
 *
 * This is implemented with the help of seek and truncate functions
 * since we're dumping Editor Commands to a temporary file.  In this
 * semantic, it's easy to think of a savepoint as an offset, and a
 * rollback as a truncate back to the offset.
 */

/* Create a savepoint.  Basically, @a *savepoint is set to the current
 * cursor position of @a eb->svnpatch_file. */
static svn_error_t *
svnpatch_savepoint(struct edit_baton *eb, apr_off_t *savepoint)
{
  apr_off_t current_position = 0;

  /* No flush-to-disk here as apr_file_seek() is APR_BUFFERED-safe. */
  SVN_ERR(svn_io_file_seek(eb->svnpatch_file, APR_CUR,
                           &current_position, eb->pool));
  *savepoint = current_position;
  return SVN_NO_ERROR;
}

/* Release a savepoint.  Set @a baton's and ascendant directories'
 * savepoints to SAVEPOINT_RELEASED.  This can't be 0 since edit_baton's
 * savepoint is set to 0, nor positive integers as offsets hold down
 * this range. */
static void
svnpatch_release_savepoint(void *baton,
                           svn_node_kind_t kind)
{
  struct dir_baton *pb;

  if (kind == svn_node_dir)
    {
      struct dir_baton *b = baton;

      b->savepoint = SAVEPOINT_RELEASED;
      b->edit_baton->savepoint = SAVEPOINT_RELEASED;
      pb = b->dir_baton;
    }
  else
    {
      struct file_baton *b = baton;

      b->savepoint = SAVEPOINT_RELEASED;
      b->edit_baton->savepoint = SAVEPOINT_RELEASED;
      pb = b->dir_baton;
    }

  while (pb)
    {
      pb->savepoint = SAVEPOINT_RELEASED;
      pb = pb->dir_baton;
    }
}

/* Rollback to a savepoint.  This truncates @a eb->svnpatch_file back to
 * @a savepoint offset. */
static svn_error_t *
svnpatch_rollback(struct edit_baton *eb, apr_off_t savepoint)
{
  apr_status_t status;

  SVN_ERR(svn_io_file_flush_to_disk(eb->svnpatch_file, eb->pool));
  status = apr_file_trunc(eb->svnpatch_file, savepoint);
  if (status != APR_SUCCESS)
    return svn_error_create
            (status, NULL,
             "There was an error when truncating the file.");

  eb->next_token--; /* This isn't perfectionism, is it? */
  return SVN_NO_ERROR;
}

/* svnpatch-specific editor functions follow */
static svn_error_t *
svnpatch_close_directory(void *dir_baton,
                         apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;
  struct edit_baton *eb = b->edit_baton;

  if (b->savepoint == SAVEPOINT_RELEASED || b->added)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "close-dir", "c", b->token));
  else
    svnpatch_rollback(eb, b->savepoint);

  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_change_dir_prop(void *dir_baton,
                         const char *name,
                         const svn_string_t *value,
                         apr_pool_t *pool)
{
  struct dir_baton *pb = dir_baton;
  struct edit_baton *eb = pb->edit_baton;

  svnpatch_release_savepoint(pb, svn_node_dir);
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "change-dir-prop", "cc(?s)", pb->token,
                           name, value));

  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_close_file(void *file_baton,
                    const char *text_checksum,
                    apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;

  if (b->savepoint == SAVEPOINT_RELEASED || b->added)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "close-file", "c(?c)",
                             b->token, text_checksum));
  else
    svnpatch_rollback(eb, b->savepoint);

  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_change_file_prop(void *file_baton,
                          const char *name,
                          const svn_string_t *value,
                          apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;

  svnpatch_release_savepoint(b, svn_node_file);
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "change-file-prop", "cc(?s)",
                           b->token, name, value));

  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_delete_entry(const char *path,
                      svn_revnum_t base_revision,
                      void *parent_baton,
                      apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  svnpatch_release_savepoint(pb, svn_node_dir);
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "delete-entry", "cc",
                           path, pb->token));

  return SVN_NO_ERROR;
}

/* Used in svnpatch_apply_textdelta() to set up the diff_stream. */
static svn_error_t *
svndiff_write_handler(void *baton,
                      const char *data,
                      apr_size_t *len)
{
  struct file_baton *f = baton;
  struct edit_baton *eb = f->edit_baton;
  svn_string_t str;

  str.data = data;
  str.len = *len;
  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "textdelta-chunk", "cs", f->token, &str));
  return SVN_NO_ERROR;
}

/* Used in svnpatch_apply_textdelta() to set up the diff_stream. */
static svn_error_t *
svndiff_close_handler(void *baton)
{
  struct file_baton *f = baton;
  struct edit_baton *eb = f->edit_baton;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "textdelta-end", "c", f->token));
  return SVN_NO_ERROR;
  
}

static svn_error_t *
svnpatch_apply_textdelta(void *file_baton,
                         const char *base_checksum,
                         apr_pool_t *pool,
                         svn_txdelta_window_handler_t *handler,
                         void **handler_baton)
{
  struct file_baton *f = file_baton;
  struct edit_baton *eb = f->edit_baton;
  svn_stream_t *diff_stream;

  SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                           "apply-textdelta", "c(?c)", f->token,
                           base_checksum));
  diff_stream = svn_stream_create(f, pool);
  svn_stream_set_write(diff_stream, svndiff_write_handler);
  svn_stream_set_close(diff_stream, svndiff_close_handler);
  svn_txdelta_to_svndiff2(handler, handler_baton, diff_stream, 1, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
svnpatch_close_edit(void *edit_baton,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  
  if (eb->savepoint == SAVEPOINT_RELEASED)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "close-edit", ""));
  else
    svnpatch_rollback(eb, 0); /* Empty svnpatch */

  return SVN_NO_ERROR;
}

/* Drive @a editor against @a path's content modifications. */
static svn_error_t *
transmit_svndiff(const char *path,
                 const svn_delta_editor_t *editor,
                 void *file_baton,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;
  svn_txdelta_window_handler_t handler;
  svn_txdelta_stream_t *txdelta_stream;
  apr_file_t *file;
  svn_stream_t *base_stream;
  svn_stream_t *local_stream;
  void *wh_baton;

  /* Initialize window_handler/baton to produce svndiff from txdelta
   * windows. */
  SVN_ERR(eb->diff_editor->apply_textdelta
          (fb, NULL, pool, &handler, &wh_baton));

  base_stream = svn_stream_empty(pool);

  SVN_ERR(svn_io_file_open(&file, path, APR_READ | APR_BUFFERED,
                           APR_OS_DEFAULT, pool));
  local_stream = svn_stream_from_aprfile2(file, FALSE, pool);

  svn_txdelta(&txdelta_stream, base_stream, local_stream, pool);
  SVN_ERR(svn_txdelta_send_txstream(txdelta_stream, handler,
                                    wh_baton, pool));
  return SVN_NO_ERROR;
}


/* Helper function: return up to two svn:mime-type values buried
 * within a file baton.  Set *MIMETYPE1 to the value within the file's
 * pristine properties, or NULL if not available.  Set *MIMETYPE2 to
 * the value within the "new" file's propchanges, or NULL if not
 * available.
 */
static void
get_file_mime_types(const char **mimetype1,
                    const char **mimetype2,
                    struct file_baton *b)
{
  /* Defaults */
  *mimetype1 = NULL;
  *mimetype2 = NULL;

  if (b->pristine_props)
    {
      svn_string_t *pristine_val;
      pristine_val = apr_hash_get(b->pristine_props, SVN_PROP_MIME_TYPE,
                                  strlen(SVN_PROP_MIME_TYPE));
      if (pristine_val)
        *mimetype1 = pristine_val->data;
    }

  if (b->propchanges)
    {
      int i;
      svn_prop_t *propchange;

      for (i = 0; i < b->propchanges->nelts; i++)
        {
          propchange = &APR_ARRAY_IDX(b->propchanges, i, svn_prop_t);
          if (strcmp(propchange->name, SVN_PROP_MIME_TYPE) == 0)
            {
              if (propchange->value)
                *mimetype2 = propchange->value->data;
              break;
            }
        }
    }
}


/* Get the repository version of a file. This makes an RA request to
 * retrieve the file contents. A pool cleanup handler is installed to
 * delete this file.
 */
static svn_error_t *
get_file_from_ra(struct file_baton *b, svn_revnum_t revision)
{
  apr_file_t *file;
  svn_stream_t *fstream;
  const char *temp_dir;

  SVN_ERR(svn_io_temp_dir(&temp_dir, b->pool));
  SVN_ERR(svn_io_open_unique_file2(&file, &(b->path_start_revision),
                                   svn_path_join(temp_dir, "tmp", b->pool),
                                   "", svn_io_file_del_on_pool_cleanup,
                                   b->pool));

  fstream = svn_stream_from_aprfile2(file, TRUE, b->pool);
  SVN_ERR(svn_ra_get_file(b->edit_baton->ra_session,
                          b->path,
                          revision,
                          fstream, NULL,
                          &(b->pristine_props),
                          b->pool));
  return svn_io_file_close(file, b->pool);
}

/* Get the props attached to a directory in the repository at BASE_REVISION. */
static svn_error_t *
get_dirprops_from_ra(struct dir_baton *b, svn_revnum_t base_revision)
{
  return svn_ra_get_dir2(b->edit_baton->ra_session,
                         NULL, NULL, &(b->pristine_props),
                         b->path,
                         base_revision,
                         0,
                         b->pool);
}


/* Create an empty file, the path to the file is returned in
   EMPTY_FILE_PATH.  If ADM_ACCESS is not NULL and a lock is held,
   create the file in the adm tmp/ area, otherwise use a system temp
   directory.

   If FILE is non-NULL, an open file is returned in *FILE. */
static svn_error_t *
create_empty_file(apr_file_t **file,
                  const char **empty_file_path,
                  svn_wc_adm_access_t *adm_access,
                  svn_io_file_del_t delete_when,
                  apr_pool_t *pool)
{
  if (adm_access && svn_wc_adm_locked(adm_access))
    return svn_wc_create_tmp_file2(file, empty_file_path,
                                   svn_wc_adm_access_path(adm_access),
                                   delete_when, pool);
  else
    {
      const char *temp_dir;

      SVN_ERR(svn_io_temp_dir(&temp_dir, pool));
      return svn_io_open_unique_file2(file, empty_file_path,
                                      svn_path_join(temp_dir, "tmp", pool),
                                      "", delete_when, pool);
    }
}

/* Return in *PATH_ACCESS the access baton for the directory PATH by
   searching the access baton set of ADM_ACCESS.  If ADM_ACCESS is NULL
   then *PATH_ACCESS will be NULL.  If LENIENT is TRUE then failure to find
   an access baton will not return an error but will set *PATH_ACCESS to
   NULL instead. */
static svn_error_t *
get_path_access(svn_wc_adm_access_t **path_access,
                svn_wc_adm_access_t *adm_access,
                const char *path,
                svn_boolean_t lenient,
                apr_pool_t *pool)
{
  if (! adm_access)
    *path_access = NULL;
  else
    {
      svn_error_t *err = svn_wc_adm_retrieve(path_access, adm_access, path,
                                             pool);
      if (err)
        {
          if (! lenient)
            return err;
          svn_error_clear(err);
          *path_access = NULL;
        }
    }

  return SVN_NO_ERROR;
}

/* Like get_path_access except the returned access baton, in
   *PARENT_ACCESS, is for the parent of PATH rather than for PATH
   itself. */
static svn_error_t *
get_parent_access(svn_wc_adm_access_t **parent_access,
                  svn_wc_adm_access_t *adm_access,
                  const char *path,
                  svn_boolean_t lenient,
                  apr_pool_t *pool)
{
  if (! adm_access)
    *parent_access = NULL;  /* Avoid messing around with paths */
  else
    {
      const char *parent_path = svn_path_dirname(path, pool);
      SVN_ERR(get_path_access(parent_access, adm_access, parent_path,
                              lenient, pool));
    }
  return SVN_NO_ERROR;
}

/* Get the empty file associated with the edit baton. This is cached so
 * that it can be reused, all empty files are the same.
 */
static svn_error_t *
get_empty_file(struct edit_baton *eb,
               const char **empty_file_path)
{
  /* Create the file if it does not exist */
  /* Note that we tried to use /dev/null in r17220, but
     that won't work on Windows: it's impossible to stat NUL */
  if (!eb->empty_file)
    SVN_ERR(create_empty_file(NULL, &(eb->empty_file), eb->adm_access,
                              svn_io_file_del_on_pool_cleanup, eb->pool));


  *empty_file_path = eb->empty_file;

  return SVN_NO_ERROR;
}

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  eb->target_revision = target_revision;
  return SVN_NO_ERROR;
}

/* An editor function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  const char *token = make_token('d', eb, pool);
  struct dir_baton *b = make_dir_baton("", NULL, eb, FALSE, token, pool);

  /* Override the wcpath in our baton. */
  b->wcpath = apr_pstrdup(pool, eb->target);

  SVN_ERR(get_dirprops_from_ra(b, base_revision));

  if (eb->svnpatch_stream)
    SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                             "open-root", "c", token));

  *root_baton = b;
  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
  svn_wc_notify_action_t action = svn_wc_notify_skip;

  /* We need to know if this is a directory or a file */
  SVN_ERR(svn_ra_check_path(eb->ra_session, path, eb->revision, &kind, pool));
  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath,
                          TRUE, pool));
  if ((! eb->adm_access) || adm_access)
    {
      switch (kind)
        {
        case svn_node_file:
          {
            const char *mimetype1, *mimetype2;
            struct file_baton *b;

            /* Compare a file being deleted against an empty file */
            b = make_file_baton(path, FALSE, eb, pb, NULL, pool);
            SVN_ERR(get_file_from_ra(b, eb->revision));
            SVN_ERR(get_empty_file(b->edit_baton, &(b->path_end_revision)));

            get_file_mime_types(&mimetype1, &mimetype2, b);

            SVN_ERR(eb->diff_callbacks->file_deleted
                    (adm_access, &state, b->wcpath,
                     b->path_start_revision,
                     b->path_end_revision,
                     mimetype1, mimetype2,
                     b->pristine_props,
                     b->edit_baton->diff_cmd_baton));

            break;
          }
        case svn_node_dir:
          {
            SVN_ERR(eb->diff_callbacks->dir_deleted
                    (adm_access, &state,
                     svn_path_join(eb->target, path, pool),
                     eb->diff_cmd_baton));
            break;
          }
        default:
          break;
        }

      if ((state != svn_wc_notify_state_missing)
          && (state != svn_wc_notify_state_obstructed))
        {
          action = svn_wc_notify_update_delete;
          if (eb->dry_run)
            {
              /* Remember what we _would've_ deleted (issue #2584). */
              const char *wcpath = svn_path_join(eb->target, path, pb->pool);
              apr_hash_set(svn_client__dry_run_deletions(eb->diff_cmd_baton),
                           wcpath, APR_HASH_KEY_STRING, wcpath);

              /* ### TODO: if (kind == svn_node_dir), record all
                 ### children as deleted to avoid collisions from
                 ### subsequent edits. */
            }
        }
    }

  if (eb->notify_func)
    {
      const char* deleted_path;
      kind_action_state_t *kas = apr_palloc(eb->pool, sizeof(*kas));
      deleted_path = svn_path_join(eb->target, path, eb->pool);
      kas->kind = kind;
      kas->action = action;
      kas->state = state;
      apr_hash_set(eb->deleted_paths, deleted_path, APR_HASH_KEY_STRING, kas);
    }

  if (eb->svnpatch_stream)
    SVN_ERR(eb->diff_editor->delete_entry
            (path, SVN_INVALID_REVNUM, pb, pool));

  return SVN_NO_ERROR;
}

/* An editor function.  */
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
  struct dir_baton *b;
  svn_wc_adm_access_t *adm_access;
  svn_wc_notify_state_t state;
  svn_wc_notify_action_t action;
  const char *token = make_token('d', eb, pool);

  /* ### TODO: support copyfrom? */

  b = make_dir_baton(path, pb, eb, TRUE, token, pool);
  b->pristine_props = eb->empty_hash;
  *child_baton = b;

  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath, TRUE,
                          pool));

  SVN_ERR(eb->diff_callbacks->dir_added
          (adm_access, &state, b->wcpath, eb->target_revision,
           copyfrom_path, copyfrom_revision,
           eb->diff_cmd_baton));

  if ((state == svn_wc_notify_state_missing)
      || (state == svn_wc_notify_state_obstructed))
    action = svn_wc_notify_skip;
  else
    action = svn_wc_notify_update_add;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify;
      svn_boolean_t is_replace = FALSE;
      kind_action_state_t *kas = apr_hash_get(eb->deleted_paths, b->wcpath,
                                              APR_HASH_KEY_STRING);
      if (kas)
        {
          svn_wc_notify_action_t new_action;
          if (kas->action == svn_wc_notify_update_delete
              && action == svn_wc_notify_update_add)
            {
              is_replace = TRUE;
              new_action = svn_wc_notify_update_replace;
            }
          else
            new_action = kas->action;
          notify  = svn_wc_create_notify(b->wcpath, new_action, pool);
          notify->kind = kas->kind;
          notify->content_state = notify->prop_state = kas->state;
          notify->lock_state = svn_wc_notify_lock_state_inapplicable;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
          apr_hash_set(eb->deleted_paths, b->wcpath,
                       APR_HASH_KEY_STRING, NULL);
        }

      if (!is_replace)
        {
          notify = svn_wc_create_notify(b->wcpath, action, pool);
          notify->kind = svn_node_dir;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }
    }

  if (eb->svnpatch_stream)
    {
      /* Calling svnpatch_savepoint() would be pure overhead since we
       * know we want this directory be included in svnpatch.  So let's
       * release parents' savepoints instead, which means no rollback
       * can/will be attempted on this path -- Editor Commands to
       * traverse from root to this directory. */
      svnpatch_release_savepoint(pb, svn_node_dir);
      SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                               "add-dir", "ccc(?c)", path, pb->token,
                               token, copyfrom_path));
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
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
  const char *token = make_token('d', eb, pool);
  svn_wc_adm_access_t *adm_access;

  b = make_dir_baton(path, pb, pb->edit_baton, FALSE, token, pool);
  *child_baton = b;

  SVN_ERR(get_dirprops_from_ra(b, base_revision));

  SVN_ERR(get_path_access(&adm_access, eb->adm_access, pb->wcpath, TRUE,
                          pool));

  SVN_ERR(eb->diff_callbacks->dir_opened
          (adm_access, b->wcpath, base_revision,
           b->edit_baton->diff_cmd_baton));

  if (eb->svnpatch_stream)
    {
      SVN_ERR(svnpatch_savepoint(eb, &b->savepoint));
      SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                               "open-dir", "ccc", path, pb->token, token));
    }

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *b;
  const char *token = make_token('c', eb, pool);

  /* ### TODO: support copyfrom? */

  b = make_file_baton(path, TRUE, pb->edit_baton, pb, token, pool);
  *file_baton = b;

  if (eb->svnpatch_stream)
    {
      /* Release right away, we want this path all the way up to root as
       * this is an addition, which we always include in svnpatch. */
      svnpatch_release_savepoint(b, svn_node_file);
      SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                               "add-file", "ccc(?c)", path, pb->token,
                               token, copyfrom_path));
    }

  SVN_ERR(get_empty_file(b->edit_baton, &(b->path_start_revision)));
  b->pristine_props = pb->edit_baton->empty_hash;

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *b;
  struct edit_baton *eb = pb->edit_baton;
  const char *token = make_token('c', eb, pool);

  b = make_file_baton(path, FALSE, pb->edit_baton, pb, token, pool);
  *file_baton = b;

  SVN_ERR(get_file_from_ra(b, base_revision));

  if (eb->svnpatch_stream)
    {
      SVN_ERR(svnpatch_savepoint(eb, &b->savepoint));
      SVN_ERR(svn_wc_write_cmd(eb->svnpatch_stream, eb->pool,
                               "open-file", "ccc", path,
                               pb->token, token));
    }

  return SVN_NO_ERROR;
}

/* Do the work of applying the text delta.  */
static svn_error_t *
window_handler(svn_txdelta_window_t *window,
               void *window_baton)
{
  struct file_baton *b = window_baton;

  SVN_ERR(b->apply_handler(window, b->apply_baton));

  if (!window)
    {
      SVN_ERR(svn_io_file_close(b->file_start_revision, b->pool));
      SVN_ERR(svn_io_file_close(b->file_end_revision, b->pool));
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *b = file_baton;
  svn_wc_adm_access_t *adm_access;

  /* Open the file to be used as the base for second revision */
  SVN_ERR(svn_io_file_open(&(b->file_start_revision),
                           b->path_start_revision,
                           APR_READ, APR_OS_DEFAULT, b->pool));

  /* Open the file that will become the second revision after applying the
     text delta, it starts empty */
  if (b->edit_baton->adm_access)
    {
      svn_error_t *err;

      err = svn_wc_adm_probe_retrieve(&adm_access, b->edit_baton->adm_access,
                                      b->wcpath, pool);
      if (err)
        {
          svn_error_clear(err);
          adm_access = NULL;
        }
    }
  else
    adm_access = NULL;
  SVN_ERR(create_empty_file(&(b->file_end_revision),
                            &(b->path_end_revision), adm_access,
                            svn_io_file_del_on_pool_cleanup, b->pool));

  svn_txdelta_apply(svn_stream_from_aprfile2(b->file_start_revision, TRUE,
                                             b->pool),
                    svn_stream_from_aprfile2(b->file_end_revision, TRUE,
                                             b->pool),
                    NULL, b->path, b->pool,
                    &(b->apply_handler), &(b->apply_baton));

  *handler = window_handler;
  *handler_baton = file_baton;

  return SVN_NO_ERROR;
}

/* An editor function.  When the file is closed we have a temporary
 * file containing a pristine version of the repository file. This can
 * be compared against the working copy.
 *
 * ### Ignore TEXT_CHECKSUM for now.  Someday we can use it to verify
 * ### the integrity of the file being diffed.  Done efficiently, this
 * ### would probably involve calculating the checksum as the data is
 * ### received, storing the final checksum in the file_baton, and
 * ### comparing against it here.
 */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_adm_access_t *adm_access;
  svn_error_t *err;
  svn_wc_notify_action_t action;
  svn_wc_notify_state_t
    content_state = svn_wc_notify_state_unknown,
    prop_state = svn_wc_notify_state_unknown;
  svn_boolean_t binary_file = FALSE;

  err = get_parent_access(&adm_access, eb->adm_access,
                          b->wcpath, eb->dry_run, b->pool);

  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* ### maybe try to stat the local b->wcpath? */
      /* If the file path doesn't exist, then send a 'skipped' notification. */
      if (eb->notify_func)
        {
          svn_wc_notify_t *notify = svn_wc_create_notify(b->wcpath,
                                                         svn_wc_notify_skip,
                                                         pool);
          notify->kind = svn_node_file;
          notify->content_state = svn_wc_notify_state_missing;
          notify->prop_state = prop_state;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }

      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  if (b->path_end_revision || b->propchanges->nelts > 0)
    {
      const char *mimetype1, *mimetype2;
      get_file_mime_types(&mimetype1, &mimetype2, b);
      if (( mimetype1 && svn_mime_type_is_binary(mimetype1))
          || (mimetype2 && svn_mime_type_is_binary(mimetype2)))
        binary_file = TRUE;

      if (b->added)
        SVN_ERR(eb->diff_callbacks->file_added
                (adm_access, &content_state, &prop_state,
                 b->wcpath,
                 b->path_end_revision ? b->path_start_revision : NULL,
                 b->path_end_revision,
                 0,
                 b->edit_baton->target_revision,
                 mimetype1, mimetype2,
                 NULL, SVN_INVALID_REVNUM, /* XXX make use of new 1.6 API */
                 b->propchanges, b->pristine_props,
                 b->edit_baton->diff_cmd_baton));
      else
        SVN_ERR(eb->diff_callbacks->file_changed
                (adm_access, &content_state, &prop_state,
                 b->wcpath,
                 b->path_end_revision ? b->path_start_revision : NULL,
                 b->path_end_revision,
                 b->edit_baton->revision,
                 b->edit_baton->target_revision,
                 mimetype1, mimetype2,
                 b->propchanges, b->pristine_props,
                 b->edit_baton->diff_cmd_baton));

      if (binary_file && eb->svnpatch_stream)
        SVN_ERR(transmit_svndiff(b->path_end_revision,
                                 eb->diff_editor, b, pool));
    }


  if ((content_state == svn_wc_notify_state_missing)
      || (content_state == svn_wc_notify_state_obstructed))
    action = svn_wc_notify_skip;
  else if (b->added)
    action = svn_wc_notify_update_add;
  else
    action = svn_wc_notify_update_update;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify;
      svn_boolean_t is_replace = FALSE;
      kind_action_state_t *kas = apr_hash_get(eb->deleted_paths, b->wcpath,
                                              APR_HASH_KEY_STRING);
      if (kas)
        {
          svn_wc_notify_action_t new_action;
          if (kas->action == svn_wc_notify_update_delete
              && action == svn_wc_notify_update_add)
            {
              is_replace = TRUE;
              new_action = svn_wc_notify_update_replace;
            }
          else
            new_action = kas->action;
          notify  = svn_wc_create_notify(b->wcpath, new_action, pool);
          notify->kind = kas->kind;
          notify->content_state = notify->prop_state = kas->state;
          notify->lock_state = svn_wc_notify_lock_state_inapplicable;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
          apr_hash_set(eb->deleted_paths, b->wcpath,
                       APR_HASH_KEY_STRING, NULL);
        }

      if (!is_replace)
        {
          notify = svn_wc_create_notify(b->wcpath, action, pool);
          notify->kind = svn_node_file;
          notify->content_state = content_state;
          notify->prop_state = prop_state;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }
    }

  if (eb->svnpatch_stream)
    {
      /* In case this file is an addition, we've already released
       * savepoints when writing add-file command. */
      if (binary_file && !b->added)
          svnpatch_release_savepoint(b, svn_node_file);

      SVN_ERR(eb->diff_editor->close_file
              (b, binary_file ? text_checksum : NULL, b->pool));
    }

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *b = dir_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_wc_notify_state_t content_state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  svn_error_t *err;
  svn_wc_adm_access_t *adm_access;

  if (eb->dry_run)
    svn_hash__clear(svn_client__dry_run_deletions(eb->diff_cmd_baton));

  err = get_path_access(&adm_access, eb->adm_access, b->wcpath,
                        eb->dry_run, b->pool);

  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* ### maybe try to stat the local b->wcpath? */
      /* If the path doesn't exist, then send a 'skipped' notification. */
      if (eb->notify_func)
        {
          svn_wc_notify_t *notify
            = svn_wc_create_notify(b->wcpath, svn_wc_notify_skip, pool);
          notify->kind = svn_node_dir;
          notify->content_state = notify->prop_state
            = svn_wc_notify_state_missing;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
        }
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;

  /* Don't do the props_changed stuff if this is a dry_run and we don't
     have an access baton, since in that case the directory will already
     have been recognised as added, in which case they cannot conflict. */
  if ((b->propchanges->nelts > 0) && (! eb->dry_run || adm_access))
    SVN_ERR(eb->diff_callbacks->dir_props_changed
            (adm_access, &prop_state,
             b->wcpath,
             b->propchanges, b->pristine_props,
             b->edit_baton->diff_cmd_baton));

  SVN_ERR(eb->diff_callbacks->dir_closed
          (adm_access, &content_state,
           b->wcpath, b->edit_baton->diff_cmd_baton));

  /* ### Don't notify added directories as they triggered notification
     in add_directory.  Does this mean that directory notification
     isn't getting all the information? */
  if (!b->added && eb->notify_func)
    {
      svn_wc_notify_t *notify;
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(NULL, eb->deleted_paths); hi;
           hi = apr_hash_next(hi))
        {
          const void *deleted_path;
          kind_action_state_t *kas;
          apr_hash_this(hi, &deleted_path, NULL, (void *)&kas);
          notify  = svn_wc_create_notify(deleted_path, kas->action, pool);
          notify->kind = kas->kind;
          notify->content_state = notify->prop_state = kas->state;
          notify->lock_state = svn_wc_notify_lock_state_inapplicable;
          (*eb->notify_func)(eb->notify_baton, notify, pool);
          apr_hash_set(eb->deleted_paths, deleted_path,
                       APR_HASH_KEY_STRING, NULL);
        }

      notify = svn_wc_create_notify(b->wcpath,
                                    svn_wc_notify_update_update, pool);
      notify->kind = svn_node_dir;

      /* In case of a tree conflict during merge, the diff callback
       * sets content_state appropriately. So copy the state into the
       * notify_t to make sure conflicts get displayed. */
      notify->content_state = content_state;
      
      notify->prop_state = prop_state;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  if (eb->svnpatch_stream)
    eb->diff_editor->close_directory(b, pool);

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *b = file_baton;
  struct edit_baton *eb = b->edit_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push(b->propchanges);
  propchange->name = apr_pstrdup(b->pool, name);
  propchange->value = value ? svn_string_dup(value, b->pool) : NULL;

  /* Restrict to Regular Properties. */
  if (eb->svnpatch_stream
      && svn_property_kind(NULL, name) == svn_prop_regular_kind)
    SVN_ERR(eb->diff_editor->change_file_prop
            (b, name, value, pool));

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  svn_prop_t *propchange;

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = value ? svn_string_dup(value, db->pool) : NULL;

  /* Restrict to Regular Properties. */
  if (eb->svnpatch_stream
      && svn_property_kind(NULL, name) == svn_prop_regular_kind)
    SVN_ERR(eb->diff_editor->change_dir_prop
            (dir_baton, name, value, pool));

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  svn_pool_destroy(eb->pool);

  if (eb->svnpatch_stream)
      SVN_ERR(eb->diff_editor->close_edit(eb, pool));

  return SVN_NO_ERROR;
}

/* An editor function.  */
static svn_error_t *
absent_directory(const char *path,
                 void *parent_baton,
                 apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(svn_path_join(pb->wcpath,
                                             svn_path_basename(path, pool),
                                             pool),
                               svn_wc_notify_skip, pool);
      notify->kind = svn_node_dir;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_missing;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* An editor function.  */
static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(svn_path_join(pb->wcpath,
                                             svn_path_basename(path, pool),
                                             pool),
                               svn_wc_notify_skip, pool);
      notify->kind = svn_node_file;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_missing;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}

/* Create an svnpatch-specific editor.  As svnpatch is revisionless, no
 * need for @c set_target_revision callback.  A few other functions are
 * missing too as we replace them with inline code in the real editor's
 * functions (see svn_client__get_diff_editor). 
 * Note: although we're creating a true delta editor, we're indeed not
 * using @a *editor as it ought to be, in a sense that we will not drive
 * it as expected.  We're just using the delta editor interface that
 * turns out to fit very well with the use we're making here.  A small
 * drive is being performed by transmit_svndiff() against binary files
 * to generate svndiffs and txdelta Editor Commands though. */
static void
get_svnpatch_diff_editor(svn_delta_editor_t **editor,
                         apr_pool_t *pool)
{
  svn_delta_editor_t *diff_editor;

  diff_editor = svn_delta_default_editor(pool);

  diff_editor->delete_entry = svnpatch_delete_entry;
  diff_editor->close_directory = svnpatch_close_directory;
  diff_editor->apply_textdelta = svnpatch_apply_textdelta;
  diff_editor->change_file_prop = svnpatch_change_file_prop;
  diff_editor->change_dir_prop = svnpatch_change_dir_prop;
  diff_editor->close_file = svnpatch_close_file;
  diff_editor->close_edit = svnpatch_close_edit;

  *editor = diff_editor;
}

/* Create a repository diff editor and baton.  */
svn_error_t *
svn_client__get_diff_editor(const char *target,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_diff_callbacks3_t *diff_callbacks,
                            void *diff_cmd_baton,
                            svn_depth_t depth,
                            svn_boolean_t dry_run,
                            svn_ra_session_t *ra_session,
                            svn_revnum_t revision,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            const svn_delta_editor_t **editor,
                            void **edit_baton,
                            apr_file_t *svnpatch_file,
                            apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(subpool);
  struct edit_baton *eb = apr_palloc(subpool, sizeof(*eb));

  eb->target = target;
  eb->adm_access = adm_access;
  eb->diff_callbacks = diff_callbacks;
  eb->diff_cmd_baton = diff_cmd_baton;
  eb->dry_run = dry_run;
  eb->ra_session = ra_session;
  eb->revision = revision;
  eb->empty_file = NULL;
  eb->empty_hash = apr_hash_make(subpool);
  eb->deleted_paths = apr_hash_make(subpool);
  eb->pool = subpool;
  eb->notify_func = notify_func;
  eb->notify_baton = notify_baton;
  eb->next_token = 0;
  eb->savepoint = 0;
  eb->diff_editor = NULL;
  eb->svnpatch_stream = NULL;
  eb->svnpatch_file = svnpatch_file;

  if (eb->svnpatch_file)
    {
      eb->svnpatch_stream =
        svn_stream_from_aprfile2(eb->svnpatch_file,
                                 FALSE, eb->pool);
      get_svnpatch_diff_editor(&eb->diff_editor, eb->pool);
    }

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->close_file = close_file;
  tree_editor->close_directory = close_directory;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_edit = close_edit;
  tree_editor->absent_directory = absent_directory;
  tree_editor->absent_file = absent_file;

  return svn_delta_get_cancellation_editor(cancel_func,
                                           cancel_baton,
                                           tree_editor,
                                           eb,
                                           editor,
                                           edit_baton,
                                           pool);

  /* We don't destroy subpool, as it's managed by the edit baton. */
}
