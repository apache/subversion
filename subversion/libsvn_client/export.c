/*
 * export.c:  export a tree.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_subst.h"
#include "svn_md5.h"
#include "client.h"


/*** Code. ***/

svn_error_t *
svn_client__remove_admin_dirs (const char *dir,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  apr_hash_index_t *hi;

  SVN_ERR (svn_io_get_dirents (&dirents, dir, pool));

  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      const svn_node_kind_t *type;
      const char *item;
      const void *key;
      void *val;

      apr_hash_this (hi, &key, NULL, &val);

      item = key;
      type = val;

      if (ctx->cancel_func)
        SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

      /* ### We could also invoke ctx->notify_func somewhere in
         ### here... Is it called for, though?  Not sure. */ 

      if (*type == svn_node_dir)
        {
          const char *dir_path = svn_path_join (dir, key, subpool);

          if (strcmp (item, SVN_WC_ADM_DIR_NAME) == 0)
            {
              SVN_ERR (svn_io_remove_dir (dir_path, subpool));
            }
          else
            {
              SVN_ERR (svn_client__remove_admin_dirs (dir_path, ctx, subpool));
            } 
        }

      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_versioned_files (const char *from,
                      const char *to,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *dirents;
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_error_t *err;

  SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, from, FALSE, FALSE, pool));
  err = svn_wc_entry (&entry, from, adm_access, FALSE, subpool);
  SVN_ERR (svn_wc_adm_close (adm_access));

  if (err && err->apr_err != SVN_ERR_WC_NOT_DIRECTORY)
    return err;

  /* we don't want to copy some random non-versioned directory. */
  if (entry)
    {
      apr_hash_index_t *hi;
      apr_finfo_t finfo;

      SVN_ERR (svn_io_stat (&finfo, from, APR_FINFO_PROT, subpool));

      SVN_ERR (svn_io_dir_make (to, finfo.protection, subpool));

      SVN_ERR (svn_io_get_dirents (&dirents, from, pool));

      for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
        {
          const svn_node_kind_t *type;
          const char *item;
          const void *key;
          void *val;

          apr_hash_this (hi, &key, NULL, &val);

          item = key;
          type = val;

          if (ctx->cancel_func)
            SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

          /* ### We could also invoke ctx->notify_func somewhere in
             ### here... Is it called for, though?  Not sure. */ 

          if (*type == svn_node_dir)
            {
              if (strcmp (item, SVN_WC_ADM_DIR_NAME) == 0)
                {
                  ; /* skip this, it's an administrative directory. */
                }
              else
                {
                  const char *new_from = svn_path_join (from, key, subpool);
                  const char *new_to = svn_path_join (to, key, subpool);

                  SVN_ERR (copy_versioned_files (new_from, new_to,
                                                 ctx, subpool));
                }
            }
          else if (*type == svn_node_file)
            {
              const char *copy_from = svn_path_join (from, item, subpool);
              const char *copy_to = svn_path_join (to, item, subpool);

              err = svn_wc_entry (&entry, copy_from, adm_access, FALSE,
                                  subpool);

              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_NOT_FILE)
                    return err;
                  svn_error_clear (err);
                }

              /* don't copy it if it isn't versioned. */
              if (entry)
                {
                  SVN_ERR (svn_io_copy_file (copy_from, copy_to, TRUE,
                                             subpool));
                }
            }

          svn_pool_clear (subpool);
        }
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_export (const char *from,
                   const char *to,
                   svn_opt_revision_t *revision,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  if (svn_path_is_url (from))
    {
#if 1 /* new export-editor */
      const char *URL;
      svn_revnum_t revnum;
      void *ra_baton, *session;
      svn_ra_plugin_t *ra_lib;
      void *edit_baton;
      const svn_delta_editor_t *export_editor;

      SVN_ERR (svn_client__get_export_editor (&export_editor, &edit_baton,
                                              to, ctx, pool));

      URL = svn_path_canonicalize (from, pool);
      
      if (revision->kind == svn_opt_revision_number)
        revnum = revision->value.number;
      else
        revnum = SVN_INVALID_REVNUM;

      SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
      SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, URL, pool));

      SVN_ERR (svn_client__open_ra_session (&session, ra_lib, URL, NULL,
                                            NULL, NULL, FALSE, TRUE,
                                            ctx, pool));

      /* Tell RA to do a checkout of REVISION; if we pass an invalid
         revnum, that means RA will fetch the latest revision.  */
      SVN_ERR (ra_lib->do_checkout (session, revnum,
                                    TRUE, /* recurse */
                                    export_editor, edit_baton, pool));
#else  /* old export method */

      /* export directly from the repository by doing a checkout first. */
      SVN_ERR (svn_client_checkout (from,
                                    to,
                                    revision,
                                    TRUE,
                                    ctx,
                                    pool));
      
      /* walk over the wc and remove the administrative directories. */
      SVN_ERR (svn_client__remove_admin_dirs (to, ctx, pool));

#endif
    }
  else
    {
      /* just copy the contents of the working copy into the target path. */
      SVN_ERR (copy_versioned_files (from, to, ctx, pool));
    }

  return SVN_NO_ERROR;
}


/* ---------------------------------------------------------------------- */

/*** A dedicated 'export' editor, which does no .svn/ accounting.  ***/


struct edit_baton
{
  const char *root_path;

  svn_wc_notify_func_t notify_func;
  void *notify_baton;
};


struct dir_baton
{
  struct edit_baton *edit_baton;
};


struct file_baton
{
  struct dir_baton *parent_dir_baton;

  const char *path;
  const char *tmppath;
  apr_hash_t *props;

  /* The MD5 digest of the file's fulltext.  This is all zeros until
     the last textdelta window handler call returns. */
  unsigned char text_digest[MD5_DIGESTSIZE];
};


struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;
  apr_pool_t *pool;
  const char *tmppath;
};



/* Just ensure that the main export directory exists. */
static svn_error_t *
open_root (void *edit_baton,
           svn_revnum_t base_revision,
           apr_pool_t *pool,
           void **root_baton)
{
  struct edit_baton *eb = edit_baton;  
  struct dir_baton *db = apr_pcalloc (pool, sizeof(*db));
  svn_node_kind_t kind;
  
  db->edit_baton = edit_baton;

  SVN_ERR (svn_io_check_path (eb->root_path, &kind, pool));
  if (kind != svn_node_none)
    return svn_error_create (SVN_ERR_WC_OBSTRUCTED_UPDATE,
                             NULL, eb->root_path);

  SVN_ERR (svn_io_dir_make (eb->root_path, APR_OS_DEFAULT, pool));

  if (db->edit_baton->notify_func)
    (*db->edit_baton->notify_func) (db->edit_baton->notify_baton,
                                    eb->root_path,
                                    svn_wc_notify_update_add,
                                    svn_node_dir,
                                    NULL,
                                    svn_wc_notify_state_unknown,
                                    svn_wc_notify_state_unknown,
                                    SVN_INVALID_REVNUM);

  *root_baton = db;
  return SVN_NO_ERROR;
}


/* Ensure the directory exists, and send feedback. */
static svn_error_t *
add_directory (const char *path,
               void *parent_baton,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_revision,
               apr_pool_t *pool,
               void **baton)
{
  struct dir_baton *db = apr_pcalloc (pool, sizeof(*db));
  struct dir_baton *parent = parent_baton;
  const char *full_path = svn_path_join (parent->edit_baton->root_path,
                                         path, pool);

  db->edit_baton = parent->edit_baton;

  SVN_ERR (svn_io_dir_make (full_path, APR_OS_DEFAULT, pool));

  if (db->edit_baton->notify_func)
    (*db->edit_baton->notify_func) (db->edit_baton->notify_baton,
                                    full_path,
                                    svn_wc_notify_update_add,
                                    svn_node_dir,
                                    NULL,
                                    svn_wc_notify_state_unknown,
                                    svn_wc_notify_state_unknown,
                                    SVN_INVALID_REVNUM);

  *baton = db;
  return SVN_NO_ERROR;
}


/* Build a file baton. */
static svn_error_t *
add_file (const char *path,
          void *parent_baton,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_revision,
          apr_pool_t *pool,
          void **baton)
{
  struct dir_baton *parent = parent_baton;
  struct file_baton *fb = apr_pcalloc (pool, sizeof(*fb));
  const char *full_path = svn_path_join (parent->edit_baton->root_path,
                                         path, pool);

  fb->parent_dir_baton = parent;
  fb->path = full_path;
  fb->props = apr_hash_make (pool);

  *baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
window_handler (svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  svn_error_t *err;

  err = hb->apply_handler (window, hb->apply_baton);
  if (window != NULL && err == SVN_NO_ERROR)
    return err;

  if (err != SVN_NO_ERROR)
    {
      /* We failed to apply the patch; clean up the temporary file.  */
      apr_file_remove (hb->tmppath, hb->pool);
    }

  return err;
}



/* Write incoming data into the tmpfile stream */
static svn_error_t *
apply_textdelta (void *file_baton,
                 const char *base_checksum,
                 apr_pool_t *pool,
                 svn_txdelta_window_handler_t *handler,
                 void **handler_baton)
{
  struct file_baton *fb = file_baton;
  struct handler_baton *hb = apr_palloc (pool, sizeof (*hb));
  apr_file_t *tmp_file; 

  SVN_ERR (svn_io_open_unique_file (&tmp_file, &(fb->tmppath),
                                    fb->path, ".tmp", FALSE, pool));

  hb->pool = pool;
  hb->tmppath = fb->tmppath;

  svn_txdelta_apply (svn_stream_empty (pool),
                     svn_stream_from_aprfile (tmp_file, pool),
                     fb->text_digest, NULL, pool,
                     &hb->apply_handler, &hb->apply_baton);
  
  *handler_baton = hb;
  *handler = window_handler;
  return SVN_NO_ERROR;
}


/* Cache props in the file baton */
static svn_error_t *
change_file_prop (void *file_baton,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
 
  apr_hash_set (fb->props, apr_pstrdup (pool, name),
                APR_HASH_KEY_STRING,
                value ? svn_string_dup (value, pool) : NULL);

  return SVN_NO_ERROR;
}



/* Move the tmpfile to file, and send feedback. */
static svn_error_t *
close_file (void *file_baton,
            const char *text_checksum,
            apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  struct dir_baton *db = fb->parent_dir_baton;
  const svn_string_t *eol_value, *keywords_value, *executable_value;

  if (! fb->tmppath)
    /* No txdelta was ever sent. */
    return SVN_NO_ERROR;

  /* Look for props that may affect the final file. */
  eol_value = apr_hash_get (fb->props,
                            SVN_PROP_EOL_STYLE, APR_HASH_KEY_STRING);
  keywords_value = apr_hash_get (fb->props,
                                 SVN_PROP_KEYWORDS, APR_HASH_KEY_STRING);
  executable_value = apr_hash_get (fb->props,
                                   SVN_PROP_EXECUTABLE, APR_HASH_KEY_STRING);

  if (text_checksum)
    {
      const char *actual_checksum
        = svn_md5_digest_to_cstring (fb->text_digest, pool);

      if (actual_checksum && (strcmp (text_checksum, actual_checksum) != 0))
        {
          return svn_error_createf
            (SVN_ERR_CHECKSUM_MISMATCH, NULL,
             "close_file: checksum mismatch for resulting fulltext\n"
             "(%s):\n"
             "   expected checksum:  %s\n"
             "   actual checksum:    %s\n",
             fb->path, text_checksum, actual_checksum);
        }
    }

  if (! eol_value)
    {
      SVN_ERR (svn_io_file_rename (fb->tmppath, fb->path, pool));
    }
  else
    {
      svn_subst_eol_style_t style;
      const char *eol;

      svn_subst_eol_style_from_value (&style, &eol, eol_value->data);

      /* ### do keyword translation! */

      SVN_ERR (svn_subst_copy_and_translate (fb->tmppath, fb->path,
                                             eol, TRUE, /* repair */
                                             NULL, /* no keywords */
                                             FALSE, /* don't expand */
                                             pool));
      SVN_ERR (svn_io_remove_file (fb->tmppath, pool));
    }
      
  if (executable_value)
    SVN_ERR (svn_io_set_file_executable (fb->path, TRUE, FALSE, pool));

  if (db->edit_baton->notify_func)
    (*db->edit_baton->notify_func) (db->edit_baton->notify_baton,
                                    fb->path,
                                    svn_wc_notify_update_add,
                                    svn_node_file,
                                    NULL,
                                    svn_wc_notify_state_unknown,
                                    svn_wc_notify_state_unknown,
                                    SVN_INVALID_REVNUM);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_client__get_export_editor (const svn_delta_editor_t **editor,
                               void **edit_baton,
                               const char *root_path,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  struct edit_baton *eb = apr_pcalloc (pool, sizeof (*eb));
  svn_delta_editor_t *export_editor = svn_delta_default_editor (pool);

  eb->root_path = apr_pstrdup (pool, root_path);
  eb->notify_func = ctx->notify_func;
  eb->notify_baton = ctx->notify_baton;

  export_editor->open_root = open_root;
  export_editor->add_directory = add_directory;
  export_editor->add_file = add_file;
  export_editor->apply_textdelta = apply_textdelta;
  export_editor->close_file = close_file;
  export_editor->change_file_prop = change_file_prop;

  SVN_ERR (svn_delta_get_cancellation_editor (ctx->cancel_func,
                                              ctx->cancel_baton,
                                              export_editor,
                                              eb,
                                              editor,
                                              edit_baton,
                                              pool));
  
  return SVN_NO_ERROR;
}
