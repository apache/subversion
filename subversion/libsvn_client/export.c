/*
 * export.c:  export a tree.
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_file_io.h>
#include <apr_md5.h>
#include "svn_types.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_subst.h"
#include "svn_time.h"
#include "svn_props.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* Add EXTERNALS_PROP_VAL for the export destination path PATH to
   TRAVERSAL_INFO.  */
static void
add_externals(apr_hash_t *externals,
              const char *path,
              const svn_string_t *externals_prop_val)
{
  apr_pool_t *pool = apr_hash_pool_get(externals);

  if (! externals_prop_val)
    return;

  apr_hash_set(externals,
               apr_pstrdup(pool, path),
               APR_HASH_KEY_STRING,
               apr_pstrmemdup(pool, externals_prop_val->data,
                              externals_prop_val->len));
}

/* Helper function that gets the eol style and optionally overrides the
   EOL marker for files marked as native with the EOL marker matching
   the string specified in requested_value which is of the same format
   as the svn:eol-style property values. */
static svn_error_t *
get_eol_style(svn_subst_eol_style_t *style,
              const char **eol,
              const char *value,
              const char *requested_value)
{
  svn_subst_eol_style_from_value(style, eol, value);
  if (requested_value && *style == svn_subst_eol_style_native)
    {
      svn_subst_eol_style_t requested_style;
      const char *requested_eol;

      svn_subst_eol_style_from_value(&requested_style, &requested_eol,
                                     requested_value);

      if (requested_style == svn_subst_eol_style_fixed)
        *eol = requested_eol;
      else
        return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                 _("'%s' is not a valid EOL value"),
                                 requested_value);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
copy_one_versioned_file(const char *from,
                        const char *to,
                        svn_wc_adm_access_t *adm_access,
                        const svn_opt_revision_t *revision,
                        const char *native_eol,
                        apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  apr_hash_t *kw = NULL;
  svn_subst_eol_style_t style;
  apr_hash_t *props;
  svn_string_t *eol_style, *keywords, *executable, *special;
  const char *eol = NULL;
  svn_boolean_t local_mod = FALSE;
  apr_time_t tm;
  svn_stream_t *source;
  svn_stream_t *dst_stream;
  const char *dst_tmp;
  svn_error_t *err;

  SVN_ERR(svn_wc_entry(&entry, from, adm_access, FALSE, pool));

  /* Only export 'added' files when the revision is WORKING.
     Otherwise, skip the 'added' files, since they didn't exist
     in the BASE revision and don't have an associated text-base.

     Don't export 'deleted' files and directories unless it's a
     revision other than WORKING.  These files and directories
     don't really exist in WORKING. */
  if ((revision->kind != svn_opt_revision_working &&
       entry->schedule == svn_wc_schedule_add) ||
      (revision->kind == svn_opt_revision_working &&
       entry->schedule == svn_wc_schedule_delete))
    return SVN_NO_ERROR;

  if (revision->kind != svn_opt_revision_working)
    {
      SVN_ERR(svn_wc_get_pristine_contents(&source, from, pool, pool));
      SVN_ERR(svn_wc_get_prop_diffs(NULL, &props, from, adm_access, pool));
    }
  else
    {
      svn_wc_status2_t *status;

      /* ### hmm. this isn't always a specialfile. this will simply open
         ### the file readonly if it is a regular file. */
      SVN_ERR(svn_subst_read_specialfile(&source, from, pool, pool));

      SVN_ERR(svn_wc_prop_list(&props, from, adm_access, pool));
      SVN_ERR(svn_wc_status2(&status, from, adm_access, pool));
      if (status->text_status != svn_wc_status_normal)
        local_mod = TRUE;
    }

  /* We can early-exit if we're creating a special file. */
  special = apr_hash_get(props, SVN_PROP_SPECIAL,
                         APR_HASH_KEY_STRING);
  if (special != NULL)
    {
      /* Create the destination as a special file, and copy the source
         details into the destination stream. */
      SVN_ERR(svn_subst_create_specialfile(&dst_stream, to, pool, pool));
      return svn_stream_copy3(source, dst_stream, NULL, NULL, pool);
    }


  eol_style = apr_hash_get(props, SVN_PROP_EOL_STYLE,
                           APR_HASH_KEY_STRING);
  keywords = apr_hash_get(props, SVN_PROP_KEYWORDS,
                          APR_HASH_KEY_STRING);
  executable = apr_hash_get(props, SVN_PROP_EXECUTABLE,
                            APR_HASH_KEY_STRING);

  if (eol_style)
    SVN_ERR(get_eol_style(&style, &eol, eol_style->data, native_eol));

  if (local_mod)
    {
      /* Use the modified time from the working copy of
         the file */
      SVN_ERR(svn_io_file_affected_time(&tm, from, pool));
    }
  else
    {
      tm = entry->cmt_date;
    }

  if (keywords)
    {
      const char *fmt;
      const char *author;

      if (local_mod)
        {
          /* For locally modified files, we'll append an 'M'
             to the revision number, and set the author to
             "(local)" since we can't always determine the
             current user's username */
          fmt = "%ldM";
          author = _("(local)");
        }
      else
        {
          fmt = "%ld";
          author = entry->cmt_author;
        }

      SVN_ERR(svn_subst_build_keywords2
              (&kw, keywords->data,
               apr_psprintf(pool, fmt, entry->cmt_rev),
               entry->url, tm, author, pool));
    }

  /* For atomicity, we translate to a tmp file and then rename the tmp file
     over the real destination. */
  SVN_ERR(svn_stream_open_unique(&dst_stream, &dst_tmp,
                                 svn_path_dirname(to, pool),
                                 svn_io_file_del_none, pool, pool));

  /* If some translation is needed, then wrap the output stream (this is
     more efficient than wrapping the input). */
  if (eol || (kw && (apr_hash_count(kw) > 0)))
    dst_stream = svn_subst_stream_translated(dst_stream,
                                             eol,
                                             FALSE /* repair */,
                                             kw,
                                             TRUE /* expand */,
                                             pool);

  /* ###: use cancel func/baton in place of NULL/NULL below. */
  err = svn_stream_copy3(source, dst_stream, NULL, NULL, pool);

  if (!err && executable)
    err = svn_io_set_file_executable(dst_tmp, TRUE, FALSE, pool);

  if (!err)
    err = svn_io_set_file_affected_time(tm, dst_tmp, pool);

  if (err)
    return svn_error_compose_create(err, svn_io_remove_file(dst_tmp, pool));

  /* Now that dst_tmp contains the translated data, do the atomic rename. */
  return svn_io_file_rename(dst_tmp, to, pool);
}

static svn_error_t *
copy_versioned_files(const char *from,
                     const char *to,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t force,
                     svn_boolean_t ignore_externals,
                     svn_depth_t depth,
                     const char *native_eol,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_error_t *err;
  apr_pool_t *iterpool;
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, from, FALSE,
                                 0, ctx->cancel_func, ctx->cancel_baton,
                                 pool));

  SVN_ERR(svn_wc__entry_versioned(&entry, from, adm_access, FALSE, pool));

  /* Only export 'added' files when the revision is WORKING.
     Otherwise, skip the 'added' files, since they didn't exist
     in the BASE revision and don't have an associated text-base.

     Don't export 'deleted' files and directories unless it's a
     revision other than WORKING.  These files and directories
     don't really exist in WORKING. */
  if ((revision->kind != svn_opt_revision_working &&
       entry->schedule == svn_wc_schedule_add) ||
      (revision->kind == svn_opt_revision_working &&
       entry->schedule == svn_wc_schedule_delete))
    return SVN_NO_ERROR;

  if (entry->kind == svn_node_dir)
    {
      /* Try to make the new directory.  If this fails because the
         directory already exists, check our FORCE flag to see if we
         care. */

      /* Skip retrieving the umask on windows. Apr does not implement setting
         filesystem privileges on Windows.
         Retrieving the file permissions with APR_FINFO_PROT | APR_FINFO_OWNER
         is documented to be 'incredibly expensive' */
#ifdef WIN32
      err = svn_io_dir_make(to, APR_OS_DEFAULT, pool);
#else
      apr_finfo_t finfo;
      SVN_ERR(svn_io_stat(&finfo, from, APR_FINFO_PROT, pool));
      err = svn_io_dir_make(to, finfo.protection, pool);
#endif
      if (err)
        {
          if (! APR_STATUS_IS_EEXIST(err->apr_err))
            return err;
          if (! force)
            SVN_ERR_W(err, _("Destination directory exists, and will not be "
                             "overwritten unless forced"));
          else
            svn_error_clear(err);
        }

      SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, pool));

      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
        {
          const char *item;
          const void *key;
          void *val;

          svn_pool_clear(iterpool);

          apr_hash_this(hi, &key, NULL, &val);

          item = key;
          entry = val;

          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          /* ### We could also invoke ctx->notify_func somewhere in
             ### here... Is it called for, though?  Not sure. */

          if (entry->kind == svn_node_dir)
            {
              if (strcmp(item, SVN_WC_ENTRY_THIS_DIR) == 0)
                {
                  ; /* skip this, it's the current directory that we're
                       handling now. */
                }
              else
                {
                  if (depth == svn_depth_infinity)
                    {
                      const char *new_from = svn_path_join(from, item,
                                                           iterpool);
                      const char *new_to = svn_path_join(to, item, iterpool);

                      SVN_ERR(copy_versioned_files(new_from, new_to,
                                                   revision, force,
                                                   ignore_externals, depth,
                                                   native_eol, ctx,
                                                   iterpool));
                    }
                }
            }
          else if (entry->kind == svn_node_file)
            {
              const char *new_from = svn_path_join(from, item, iterpool);
              const char *new_to = svn_path_join(to, item, iterpool);

              SVN_ERR(copy_one_versioned_file(new_from, new_to, adm_access,
                                              revision, native_eol,
                                              iterpool));
            }
        }

      /* Handle externals. */
      if (! ignore_externals && depth == svn_depth_infinity
          && entry->depth == svn_depth_infinity)
        {
          apr_array_header_t *ext_items;
          const svn_string_t *prop_val;

          SVN_ERR(svn_wc_prop_get(&prop_val, SVN_PROP_EXTERNALS,
                                  from, adm_access, pool));
          if (prop_val != NULL)
            {
              int i;

              SVN_ERR(svn_wc_parse_externals_description3(&ext_items, from,
                                                          prop_val->data,
                                                          FALSE, pool));
              for (i = 0; i < ext_items->nelts; ++i)
                {
                  svn_wc_external_item2_t *ext_item;
                  const char *new_from, *new_to;

                  svn_pool_clear(iterpool);

                  ext_item = APR_ARRAY_IDX(ext_items, i,
                                           svn_wc_external_item2_t *);
                  new_from = svn_path_join(from, ext_item->target_dir,
                                           iterpool);
                  new_to = svn_path_join(to, ext_item->target_dir,
                                         iterpool);

                   /* The target dir might have multiple components.  Guarantee
                      the path leading down to the last component. */
                  if (svn_path_component_count(ext_item->target_dir) > 1)
                    {
                      const char *parent = svn_path_dirname(new_to, iterpool);
                      SVN_ERR(svn_io_make_dir_recursively(parent, iterpool));
                    }

                  SVN_ERR(copy_versioned_files(new_from, new_to,
                                               revision, force, FALSE,
                                               svn_depth_infinity, native_eol,
                                               ctx, iterpool));
                }
            }
        }

      svn_pool_destroy(iterpool);
    }
  else if (entry->kind == svn_node_file)
    {
      SVN_ERR(copy_one_versioned_file(from, to, adm_access, revision,
                                      native_eol, pool));
    }

  return svn_wc_adm_close2(adm_access, pool);
}


/* Abstraction of open_root.
 *
 * Create PATH if it does not exist and is not obstructed, and invoke
 * NOTIFY_FUNC with NOTIFY_BATON on PATH.
 *
 * If PATH exists but is a file, then error with SVN_ERR_WC_NOT_DIRECTORY.
 *
 * If PATH is a already a directory, then error with
 * SVN_ERR_WC_OBSTRUCTED_UPDATE, unless FORCE, in which case just
 * export into PATH with no error.
 */
static svn_error_t *
open_root_internal(const char *path,
                   svn_boolean_t force,
                   svn_wc_notify_func2_t notify_func,
                   void *notify_baton,
                   apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(path, &kind, pool));
  if (kind == svn_node_none)
    SVN_ERR(svn_io_make_dir_recursively(path, pool));
  else if (kind == svn_node_file)
    return svn_error_createf(SVN_ERR_WC_NOT_DIRECTORY, NULL,
                             _("'%s' exists and is not a directory"),
                             svn_path_local_style(path, pool));
  else if ((kind != svn_node_dir) || (! force))
    return svn_error_createf(SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                             _("'%s' already exists"),
                             svn_path_local_style(path, pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(path,
                                                     svn_wc_notify_update_add,
                                                     pool);
      notify->kind = svn_node_dir;
      (*notify_func)(notify_baton, notify, pool);
    }

  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */

/*** A dedicated 'export' editor, which does no .svn/ accounting.  ***/


struct edit_baton
{
  const char *root_path;
  const char *root_url;
  svn_boolean_t force;
  svn_revnum_t *target_revision;
  apr_hash_t *externals;
  const char *native_eol;

  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
  const char *path;
};


struct file_baton
{
  struct edit_baton *edit_baton;

  const char *path;
  const char *tmppath;

  /* We need to keep this around so we can explicitly close it in close_file,
     thus flushing its output to disk so we can copy and translate it. */
  svn_stream_t *tmp_stream;

  /* The MD5 digest of the file's fulltext.  This is all zeros until
     the last textdelta window handler call returns. */
  unsigned char text_digest[APR_MD5_DIGESTSIZE];

  /* The three svn: properties we might actually care about. */
  const svn_string_t *eol_style_val;
  const svn_string_t *keywords_val;
  const svn_string_t *executable_val;
  svn_boolean_t special;

  /* Any keyword vals to be substituted */
  const char *revision;
  const char *url;
  const char *author;
  apr_time_t date;

  /* Pool associated with this baton. */
  apr_pool_t *pool;
};


struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;
  apr_pool_t *pool;
  const char *tmppath;
};


static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  /* Stashing a target_revision in the baton */
  *(eb->target_revision) = target_revision;
  return SVN_NO_ERROR;
}



/* Just ensure that the main export directory exists. */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *db = apr_pcalloc(pool, sizeof(*db));

  SVN_ERR(open_root_internal(eb->root_path, eb->force,
                             eb->notify_func, eb->notify_baton, pool));

  /* Build our dir baton. */
  db->path = eb->root_path;
  db->edit_baton = eb;
  *root_baton = db;

  return SVN_NO_ERROR;
}


/* Ensure the directory exists, and send feedback. */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *db = apr_pcalloc(pool, sizeof(*db));
  struct edit_baton *eb = pb->edit_baton;
  const char *full_path = svn_path_join(eb->root_path, path, pool);
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(full_path, &kind, pool));
  if (kind == svn_node_none)
    SVN_ERR(svn_io_dir_make(full_path, APR_OS_DEFAULT, pool));
  else if (kind == svn_node_file)
    return svn_error_createf(SVN_ERR_WC_NOT_DIRECTORY, NULL,
                             _("'%s' exists and is not a directory"),
                             svn_path_local_style(full_path, pool));
  else if (! (kind == svn_node_dir && eb->force))
    return svn_error_createf(SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                             _("'%s' already exists"),
                             svn_path_local_style(full_path, pool));

  if (eb->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(full_path,
                                                     svn_wc_notify_update_add,
                                                     pool);
      notify->kind = svn_node_dir;
      (*eb->notify_func)(eb->notify_baton, notify, pool);
    }

  /* Build our dir baton. */
  db->path = full_path;
  db->edit_baton = eb;
  *baton = db;

  return SVN_NO_ERROR;
}


/* Build a file baton. */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *fb = apr_pcalloc(pool, sizeof(*fb));
  const char *full_path = svn_path_join(eb->root_path, path, pool);
  const char *full_url = svn_path_join(eb->root_url, path, pool);

  fb->edit_baton = eb;
  fb->path = full_path;
  fb->url = full_url;
  fb->pool = pool;

  *baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  svn_error_t *err;

  err = hb->apply_handler(window, hb->apply_baton);
  if (err)
    {
      /* We failed to apply the patch; clean up the temporary file.  */
      svn_error_clear(svn_io_remove_file(hb->tmppath, hb->pool));
    }

  return err;
}



/* Write incoming data into the tmpfile stream */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct handler_baton *hb = apr_palloc(pool, sizeof(*hb));

  /* Create a temporary file in the same directory as the file. We're going
     to rename the thing into place when we're done. */
  SVN_ERR(svn_stream_open_unique(&fb->tmp_stream, &fb->tmppath,
                                 svn_path_dirname(fb->path, pool),
                                 svn_io_file_del_none, fb->pool, fb->pool));

  hb->pool = pool;
  hb->tmppath = fb->tmppath;

  /* svn_txdelta_apply() closes the stream, but we want to close it in the
     close_file() function, so disown it here. */
  /* ### contrast to when we call svn_ra_get_file() which does NOT close the
     ### tmp_stream. we *should* be much more consistent! */
  svn_txdelta_apply(svn_stream_empty(pool),
                    svn_stream_disown(fb->tmp_stream, pool),
                    fb->text_digest, NULL, pool,
                    &hb->apply_handler, &hb->apply_baton);

  *handler_baton = hb;
  *handler = window_handler;
  return SVN_NO_ERROR;
}


static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;

  if (! value)
    return SVN_NO_ERROR;

  /* Store only the magic three properties. */
  if (strcmp(name, SVN_PROP_EOL_STYLE) == 0)
    fb->eol_style_val = svn_string_dup(value, fb->pool);

  else if (strcmp(name, SVN_PROP_KEYWORDS) == 0)
    fb->keywords_val = svn_string_dup(value, fb->pool);

  else if (strcmp(name, SVN_PROP_EXECUTABLE) == 0)
    fb->executable_val = svn_string_dup(value, fb->pool);

  /* Try to fill out the baton's keywords-structure too. */
  else if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_REV) == 0)
    fb->revision = apr_pstrdup(fb->pool, value->data);

  else if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
    SVN_ERR(svn_time_from_cstring(&fb->date, value->data, fb->pool));

  else if (strcmp(name, SVN_PROP_ENTRY_LAST_AUTHOR) == 0)
    fb->author = apr_pstrdup(fb->pool, value->data);

  else if (strcmp(name, SVN_PROP_SPECIAL) == 0)
    fb->special = TRUE;

  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;

  if (value && (strcmp(name, SVN_PROP_EXTERNALS) == 0))
    add_externals(eb->externals, db->path, value);

  return SVN_NO_ERROR;
}


/* Move the tmpfile to file, and send feedback. */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct edit_baton *eb = fb->edit_baton;

  /* Was a txdelta even sent? */
  if (! fb->tmppath)
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_close(fb->tmp_stream));

  if (text_checksum)
    {
      const char *actual_checksum =
        svn_checksum_to_cstring(svn_checksum__from_digest(fb->text_digest,
                                                          svn_checksum_md5,
                                                          pool), pool);

      if (actual_checksum && (strcmp(text_checksum, actual_checksum) != 0))
        {
          return svn_error_createf
            (SVN_ERR_CHECKSUM_MISMATCH, NULL,
             _("Checksum mismatch for '%s'; expected: '%s', actual: '%s'"),
             svn_path_local_style(fb->path, pool),
             text_checksum, actual_checksum);
        }
    }

  if ((! fb->eol_style_val) && (! fb->keywords_val) && (! fb->special))
    {
      SVN_ERR(svn_io_file_rename(fb->tmppath, fb->path, pool));
    }
  else
    {
      svn_subst_eol_style_t style;
      const char *eol = NULL;
      svn_boolean_t repair = FALSE;
      apr_hash_t *final_kw = NULL;

      if (fb->eol_style_val)
        {
          SVN_ERR(get_eol_style(&style, &eol, fb->eol_style_val->data,
                                eb->native_eol));
          repair = TRUE;
        }

      if (fb->keywords_val)
        SVN_ERR(svn_subst_build_keywords2(&final_kw, fb->keywords_val->data,
                                          fb->revision, fb->url, fb->date,
                                          fb->author, pool));

      SVN_ERR(svn_subst_copy_and_translate3
              (fb->tmppath, fb->path,
               eol, repair, final_kw,
               TRUE, /* expand */
               fb->special,
               pool));

      SVN_ERR(svn_io_remove_file(fb->tmppath, pool));
    }

  if (fb->executable_val)
    SVN_ERR(svn_io_set_file_executable(fb->path, TRUE, FALSE, pool));

  if (fb->date && (! fb->special))
    SVN_ERR(svn_io_set_file_affected_time(fb->date, fb->path, pool));

  if (fb->edit_baton->notify_func)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(fb->path,
                                                     svn_wc_notify_update_add,
                                                     pool);
      notify->kind = svn_node_file;
      (*fb->edit_baton->notify_func)(fb->edit_baton->notify_baton, notify,
                                     pool);
    }

  return SVN_NO_ERROR;
}



/*** Public Interfaces ***/

svn_error_t *
svn_client_export4(svn_revnum_t *result_rev,
                   const char *from,
                   const char *to,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_depth_t depth,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_revnum_t edit_revision = SVN_INVALID_REVNUM;
  const char *url;

  SVN_ERR_ASSERT(peg_revision != NULL);
  SVN_ERR_ASSERT(revision != NULL);

  peg_revision = svn_cl__rev_default_to_head_or_working(peg_revision, from);
  revision = svn_cl__rev_default_to_peg(revision, peg_revision);

  if (svn_path_is_url(from) ||
      ! SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(revision->kind))
    {
      svn_revnum_t revnum;
      svn_ra_session_t *ra_session;
      svn_node_kind_t kind;
      struct edit_baton *eb = apr_pcalloc(pool, sizeof(*eb));
      const char *repos_root_url;

      /* Get the RA connection. */
      SVN_ERR(svn_client__ra_session_from_path(&ra_session, &revnum,
                                               &url, from, NULL,
                                               peg_revision,
                                               revision, ctx, pool));

      /* Get the repository root. */
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url, pool));

      eb->root_path = to;
      eb->root_url = url;
      eb->force = overwrite;
      eb->target_revision = &edit_revision;
      eb->notify_func = ctx->notify_func2;
      eb->notify_baton = ctx->notify_baton2;
      eb->externals = apr_hash_make(pool);
      eb->native_eol = native_eol;

      SVN_ERR(svn_ra_check_path(ra_session, "", revnum, &kind, pool));

      if (kind == svn_node_file)
        {
          apr_hash_t *props;
          apr_hash_index_t *hi;
          struct file_baton *fb = apr_pcalloc(pool, sizeof(*fb));

          /* Since you cannot actually root an editor at a file, we
           * manually drive a few functions of our editor. */

          /* This is the equivalent of a parentless add_file(). */
          fb->edit_baton = eb;
          fb->path = eb->root_path;
          fb->url = eb->root_url;
          fb->pool = pool;

          /* Copied from apply_textdelta(). */
          SVN_ERR(svn_stream_open_unique(&fb->tmp_stream, &fb->tmppath,
                                         svn_path_dirname(fb->path, pool),
                                         svn_io_file_del_none,
                                         fb->pool, fb->pool));

          /* Step outside the editor-likeness for a moment, to actually talk
           * to the repository. */
          /* ### note: the stream will not be closed */
          SVN_ERR(svn_ra_get_file(ra_session, "", revnum,
                                  fb->tmp_stream,
                                  NULL, &props, pool));

          /* Push the props into change_file_prop(), to update the file_baton
           * with information. */
          for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
            {
              const void *key;
              void *val;
              apr_hash_this(hi, &key, NULL, &val);
              SVN_ERR(change_file_prop(fb, key, val, pool));
            }

          /* And now just use close_file() to do all the keyword and EOL
           * work, and put the file into place. */
          SVN_ERR(close_file(fb, NULL, pool));
        }
      else if (kind == svn_node_dir)
        {
          void *edit_baton;
          const svn_delta_editor_t *export_editor;
          const svn_ra_reporter3_t *reporter;
          void *report_baton;
          svn_delta_editor_t *editor = svn_delta_default_editor(pool);
          svn_boolean_t use_sleep = FALSE;

          editor->set_target_revision = set_target_revision;
          editor->open_root = open_root;
          editor->add_directory = add_directory;
          editor->add_file = add_file;
          editor->apply_textdelta = apply_textdelta;
          editor->close_file = close_file;
          editor->change_file_prop = change_file_prop;
          editor->change_dir_prop = change_dir_prop;

          SVN_ERR(svn_delta_get_cancellation_editor(ctx->cancel_func,
                                                    ctx->cancel_baton,
                                                    editor,
                                                    eb,
                                                    &export_editor,
                                                    &edit_baton,
                                                    pool));


          /* Manufacture a basic 'report' to the update reporter. */
          SVN_ERR(svn_ra_do_update2(ra_session,
                                    &reporter, &report_baton,
                                    revnum,
                                    "", /* no sub-target */
                                    depth,
                                    FALSE, /* don't want copyfrom-args */
                                    export_editor, edit_baton, pool));

          SVN_ERR(reporter->set_path(report_baton, "", revnum,
                                     /* Depth is irrelevant, as we're
                                        passing start_empty=TRUE anyway. */
                                     svn_depth_infinity,
                                     TRUE, /* "help, my dir is empty!" */
                                     NULL, pool));

          SVN_ERR(reporter->finish_report(report_baton, pool));

          /* Special case: Due to our sly export/checkout method of
           * updating an empty directory, no target will have been created
           * if the exported item is itself an empty directory
           * (export_editor->open_root never gets called, because there
           * are no "changes" to make to the empty dir we reported to the
           * repository).
           *
           * So we just create the empty dir manually; but we do it via
           * open_root_internal(), in order to get proper notification.
           */
          SVN_ERR(svn_io_check_path(to, &kind, pool));
          if (kind == svn_node_none)
            SVN_ERR(open_root_internal
                    (to, overwrite, ctx->notify_func2,
                     ctx->notify_baton2, pool));

          if (! ignore_externals && depth == svn_depth_infinity)
            SVN_ERR(svn_client__fetch_externals(eb->externals, from, to,
                                                repos_root_url, depth, TRUE,
                                                &use_sleep, ctx, pool));
        }
      else if (kind == svn_node_none)
        {
          return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                   _("URL '%s' doesn't exist"), from);
        }
      /* kind == svn_node_unknown not handled */
    }
  else
    {
      /* This is a working copy export. */

      /* just copy the contents of the working copy into the target path. */
      SVN_ERR(copy_versioned_files(from, to, revision, overwrite,
                                   ignore_externals, depth, native_eol,
                                   ctx, pool));
    }


  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(to,
                               svn_wc_notify_update_completed, pool);
      notify->revision = edit_revision;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  if (result_rev)
    *result_rev = edit_revision;

  return SVN_NO_ERROR;
}
