/*
 * fs_loader.c:  Front-end to the various FS back ends
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


#include "svn_types.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "fs_loader.h"

#define DEFAULT_FSAP_NAME "base"
#define FSAP_NAME_FILENAME "fsap-name"

/* The implementation of this library is deliberately not separated
   into multiple files, to avoid circular dependency problems with
   Unix static linking.  We want FS back ends to be able to use our
   functions without forcing applications to link against this library
   twice. */


/* --- Utility functions for the loader --- */

extern fs_library_vtable_t svn_fs_base__vtable;
extern fs_library_vtable_t svn_fs_fs__vtable;

/* Fetch a library vtable by name. */
static svn_error_t *
get_library_vtable (fs_library_vtable_t **vtable, const char *fsap_name,
                    apr_pool_t *pool)
{
#if 0
  /* XXX Placeholder implementation.  The real implementation should
     support DSO-loading of back-end libraries and should return an
     error rather than aborting if fsap_name is unrecognized. */
  if (strcmp(fsap_name, "base") == 0)
    *vtable = &svn_fs_base__vtable;
  else if (strcmp(fsap_name, "fsfs") == 0)
    *vtable = &svn_fs_fs__vtable;
  else
    abort();
#endif
  return SVN_NO_ERROR;
}

/* Fetch the library vtable for an existing FS. */
static svn_error_t *
fs_library_vtable (fs_library_vtable_t **vtable, const char *path,
                   apr_pool_t *pool)
{
  const char *fsap_path, *fsap_name;
  char buf[128];
  svn_error_t *err;
  apr_file_t *file;
  apr_size_t len;

  /* Read the fsap-name file to get the FSAP name, or assume the default. */
  fsap_path = svn_path_join (path, FSAP_NAME_FILENAME, pool);
  err = svn_io_file_open (&file, fsap_path, APR_READ|APR_BUFFERED, 0, pool);
  if (err && APR_STATUS_IS_ENOENT (err->apr_err))
    {
      svn_error_clear (err);
      fsap_name = DEFAULT_FSAP_NAME;
    }
  else if (err)
    return err;
  else
    {
      len = sizeof(buf);
      SVN_ERR (svn_io_read_length_line (file, buf, &len, pool));
      SVN_ERR (svn_io_file_close (file, pool));
      fsap_name = buf;
    }

  /* Fetch the library vtable by name, now that we've chosen one. */
  return get_library_vtable (vtable, fsap_name, pool);
}

static svn_error_t *
write_fsap_name (const char *path, const char *fsap_name, apr_pool_t *pool)
{
  const char *fsap_filename;
  apr_file_t *file;

  SVN_ERR (svn_io_dir_make (path, APR_OS_DEFAULT, pool));
  fsap_filename = svn_path_join (path, FSAP_NAME_FILENAME, pool);
  SVN_ERR (svn_io_file_open (&file, fsap_filename,
                             APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                             APR_OS_DEFAULT, pool));
  SVN_ERR (svn_io_file_write_full (file, fsap_name, strlen(fsap_name), NULL,
                                   pool));
  SVN_ERR (svn_io_file_write_full (file, "\n", 1, NULL, pool));
  SVN_ERR (svn_io_file_close (file, pool));
  return SVN_NO_ERROR;
}


/* --- Functions for operating on filesystems by pathname --- */

/* A default warning handling function.  */
static void
default_warning_func (void *baton, svn_error_t *err)
{
  /* The one unforgiveable sin is to fail silently.  Dumping to stderr
     or /dev/tty is not acceptable default behavior for server
     processes, since those may both be equivalent to /dev/null.  */
  abort ();
}

svn_fs_t *
svn_fs_new (apr_hash_t *fs_config, apr_pool_t *pool)
{
  svn_fs_t *fs;
  apr_pool_t *subpool = svn_pool_create (pool);

  fs = apr_palloc (subpool, sizeof (*fs));
  fs->pool = subpool;
  fs->path = NULL;
  fs->warning = default_warning_func;
  fs->warning_baton = NULL;
  fs->config = fs_config;
  fs->vtable = NULL;
  fs->fsap_data = NULL;
  return fs;
}

void
svn_fs_set_warning_func (svn_fs_t *fs, svn_fs_warning_callback_t warning,
                         void *warning_baton)
{
  fs->warning = warning;
  fs->warning_baton = warning_baton;
}

svn_error_t *
svn_fs_create (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  const char *fsap_name;

  if (fs->config)
    fsap_name = apr_hash_get (fs->config, SVN_FS_CONFIG_FSAP_NAME,
                              APR_HASH_KEY_STRING);
  if (fsap_name == NULL)
    fsap_name = DEFAULT_FSAP_NAME;
  SVN_ERR (get_library_vtable (&vtable, fsap_name, pool));

  /* Create the FS directory and write out the fsap-name file. */
  SVN_ERR (svn_io_dir_make (path, APR_OS_DEFAULT, pool));
  SVN_ERR (write_fsap_name (path, fsap_name, pool));

  /* Perform the actual creation. */
  return vtable->create (fs, path, pool);
}

svn_error_t *
svn_fs_open (svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (fs_library_vtable (&vtable, path, pool));
  return vtable->open (fs, path, pool);
}

const char *
svn_fs_path (svn_fs_t *fs, apr_pool_t *pool)
{
  return apr_pstrdup (pool, fs->path);
}

svn_error_t *
svn_fs_delete_fs (const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (fs_library_vtable (&vtable, path, pool));
  return vtable->delete_fs (path, pool);
}

svn_error_t *
svn_fs_hotcopy (const char *src_path, const char *dest_path,
                svn_boolean_t clean, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (fs_library_vtable (&vtable, src_path, pool));
  return vtable->hotcopy (src_path, dest_path, clean, pool);
}


/* --- Berkeley-specific functions --- */

svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, fs->pool));

  /* Create the FS directory and write out the fsap-name file. */
  SVN_ERR (svn_io_dir_make (path, APR_OS_DEFAULT, fs->pool));
  SVN_ERR (write_fsap_name (path, DEFAULT_FSAP_NAME, fs->pool));

  /* Perform the actual creation. */
  return vtable->create (fs, path, fs->pool);
}

svn_error_t *
svn_fs_open_berkeley (svn_fs_t *fs, const char *path)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, fs->pool));
  return vtable->open (fs, path, fs->pool);
}

const char *
svn_fs_berkeley_path (svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_fs_path (fs, pool);
}

svn_error_t *
svn_fs_delete_berkeley (const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, pool));
  return vtable->delete_fs (path, pool);
}

svn_error_t *
svn_fs_hotcopy_berkeley (const char *src_path, const char *dest_path,
                         svn_boolean_t clean_logs, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, pool));
  return vtable->hotcopy (src_path, dest_path, clean_logs, pool);
}

svn_error_t *
svn_fs_berkeley_recover (const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, pool));
  return vtable->bdb_recover (path, pool);
}

svn_error_t *
svn_fs_set_berkeley_errcall (svn_fs_t *fs,
                             void (*handler) (const char *errpfx, char *msg))
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, fs->pool));
  return vtable->bdb_set_errcall (fs, handler);
}

svn_error_t *
svn_fs_berkeley_logfiles (apr_array_header_t **logfiles,
                          const char *path,
                          svn_boolean_t only_unused,
                          apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, DEFAULT_FSAP_NAME, pool));
  return vtable->bdb_logfiles (logfiles, path, only_unused, pool);
}


/* --- Transaction functions --- */

svn_error_t *
svn_fs_begin_txn (svn_fs_txn_t **txn_p, svn_fs_t *fs, svn_revnum_t rev,
                  apr_pool_t *pool)
{
  return fs->vtable->begin_txn (txn_p, fs, rev, pool);
}

svn_error_t *
svn_fs_commit_txn (const char **conflict_p, svn_revnum_t *new_rev,
                   svn_fs_txn_t *txn, apr_pool_t *pool)
{
  return txn->vtable->commit (conflict_p, new_rev, txn, pool);
}

svn_error_t *
svn_fs_abort_txn (svn_fs_txn_t *txn, apr_pool_t *pool)
{
  return txn->vtable->abort (txn, pool);
}

svn_error_t *
svn_fs_purge_txn (svn_fs_t *fs, const char *txn_id, apr_pool_t *pool)
{
  return fs->vtable->purge_txn (fs, txn_id, pool);
}

svn_error_t *
svn_fs_txn_name (const char **name_p, svn_fs_txn_t *txn, apr_pool_t *pool)
{
  *name_p = apr_pstrdup (pool, txn->id);
  return SVN_NO_ERROR;
}

svn_revnum_t
svn_fs_txn_base_revision (svn_fs_txn_t *txn)
{
  return txn->base_rev;
}

svn_error_t *
svn_fs_open_txn (svn_fs_txn_t **txn, svn_fs_t *fs, const char *name,
                 apr_pool_t *pool)
{
  return fs->vtable->open_txn (txn, fs, name, pool);
}

svn_error_t *
svn_fs_list_transactions (apr_array_header_t **names_p, svn_fs_t *fs,
                          apr_pool_t *pool)
{
  return fs->vtable->list_transactions (names_p, fs, pool);
}

svn_error_t *
svn_fs_txn_prop (svn_string_t **value_p, svn_fs_txn_t *txn,
                 const char *propname, apr_pool_t *pool)
{
  return txn->vtable->get_prop (value_p, txn, propname, pool);
}

svn_error_t *
svn_fs_txn_proplist (apr_hash_t **table_p, svn_fs_txn_t *txn, apr_pool_t *pool)
{
  return txn->vtable->get_proplist (table_p, txn, pool);
}

svn_error_t *
svn_fs_change_txn_prop (svn_fs_txn_t *txn, const char *name,
                        const svn_string_t *value, apr_pool_t *pool)
{
  return txn->vtable->change_prop (txn, name, value, pool);
}


/* --- Root functions --- */

svn_error_t *
svn_fs_revision_root (svn_fs_root_t **root_p, svn_fs_t *fs, svn_revnum_t rev,
                      apr_pool_t *pool)
{
  return fs->vtable->revision_root (root_p, fs, rev, pool);
}

svn_error_t *
svn_fs_txn_root (svn_fs_root_t **root_p, svn_fs_txn_t *txn, apr_pool_t *pool)
{
  return txn->vtable->root (root_p, txn, pool);
}

void
svn_fs_close_root (svn_fs_root_t *root)
{
  apr_pool_destroy (root->pool);
}

svn_fs_t *
svn_fs_root_fs (svn_fs_root_t *root)
{
  return root->fs;
}

svn_boolean_t
svn_fs_is_txn_root (svn_fs_root_t *root)
{
  return root->is_txn_root;
}

svn_boolean_t
svn_fs_is_revision_root (svn_fs_root_t *root)
{
  return !root->is_txn_root;
}

const char *
svn_fs_txn_root_name (svn_fs_root_t *root, apr_pool_t *pool)
{
  return root->is_txn_root ? apr_pstrdup (pool, root->txn) : NULL;
}

svn_revnum_t
svn_fs_revision_root_revision (svn_fs_root_t *root)
{
  return root->is_txn_root ? SVN_INVALID_REVNUM : root->rev;
}

svn_error_t *
svn_fs_paths_changed (apr_hash_t **changed_paths_p, svn_fs_root_t *root,
                      apr_pool_t *pool)
{
  return root->vtable->paths_changed (changed_paths_p, root, pool);
}

svn_error_t *
svn_fs_check_path (svn_node_kind_t *kind_p, svn_fs_root_t *root,
                   const char *path, apr_pool_t *pool)
{
  return root->vtable->check_path (kind_p, root, path, pool);
}

svn_error_t *
svn_fs_node_history (svn_fs_history_t **history_p, svn_fs_root_t *root,
                     const char *path, apr_pool_t *pool)
{
  return root->vtable->node_history (history_p, root, path, pool);
}

svn_error_t *
svn_fs_history_prev (svn_fs_history_t **prev_history_p,
                     svn_fs_history_t *history, svn_boolean_t cross_copies,
                     apr_pool_t *pool)
{
  return history->vtable->prev (prev_history_p, history, cross_copies, pool);
}

svn_error_t *
svn_fs_history_location (const char **path, svn_revnum_t *revision,
                         svn_fs_history_t *history, apr_pool_t *pool)
{
  return history->vtable->location (path, revision, history, pool);
}

svn_error_t *
svn_fs_is_dir (svn_boolean_t *is_dir, svn_fs_root_t *root, const char *path,
               apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (root->vtable->check_path (&kind, root, path, pool));
  *is_dir = (kind == svn_node_dir);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_is_file (svn_boolean_t *is_file, svn_fs_root_t *root, const char *path,
                apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR (root->vtable->check_path (&kind, root, path, pool));
  *is_file = (kind == svn_node_file);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_node_id (const svn_fs_id_t **id_p, svn_fs_root_t *root,
                const char *path, apr_pool_t *pool)
{
  return root->vtable->node_id (id_p, root, path, pool);
}

svn_error_t *
svn_fs_node_created_rev (svn_revnum_t *revision, svn_fs_root_t *root,
                         const char *path, apr_pool_t *pool)
{
  return root->vtable->node_created_rev (revision, root, path, pool);
}

svn_error_t *
svn_fs_node_created_path (const char **created_path, svn_fs_root_t *root,
                          const char *path, apr_pool_t *pool)
{
  return root->vtable->node_created_path (created_path, root, path, pool);
}

svn_error_t *
svn_fs_node_prop (svn_string_t **value_p, svn_fs_root_t *root,
                  const char *path, const char *propname, apr_pool_t *pool)
{
  return root->vtable->node_prop (value_p, root, path, propname, pool);
}

svn_error_t *
svn_fs_node_proplist (apr_hash_t **table_p, svn_fs_root_t *root,
                      const char *path, apr_pool_t *pool)
{
  return root->vtable->node_proplist (table_p, root, path, pool);
}

svn_error_t *
svn_fs_change_node_prop (svn_fs_root_t *root, const char *path,
                         const char *name, const svn_string_t *value,
                         apr_pool_t *pool)
{
  return root->vtable->change_node_prop (root, path, name, value, pool);
}

svn_error_t *
svn_fs_props_changed (svn_boolean_t *changed_p, svn_fs_root_t *root1,
                      const char *path1, svn_fs_root_t *root2,
                      const char *path2, apr_pool_t *pool)
{
  return root1->vtable->props_changed (changed_p, root1, path1, root2, path2,
                                       pool);
}

svn_error_t *
svn_fs_copied_from (svn_revnum_t *rev_p, const char **path_p,
                    svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return root->vtable->copied_from (rev_p, path_p, root, path, pool);
}

svn_error_t *
svn_fs_merge (const char **conflict_p, svn_fs_root_t *source_root,
              const char *source_path, svn_fs_root_t *target_root,
              const char *target_path, svn_fs_root_t *ancestor_root,
              const char *ancestor_path, apr_pool_t *pool)
{
  return target_root->vtable->merge (conflict_p, source_root, source_path,
                                     target_root, target_path, ancestor_root,
                                     ancestor_path, pool);
}

svn_error_t *
svn_fs_dir_entries (apr_hash_t **entries_p, svn_fs_root_t *root,
                    const char *path, apr_pool_t *pool)
{
  return root->vtable->dir_entries (entries_p, root, path, pool);
}

svn_error_t *
svn_fs_make_dir (svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return root->vtable->make_dir (root, path, pool);
}

svn_error_t *
svn_fs_delete (svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return root->vtable->delete_node (root, path, pool);
}

svn_error_t *
svn_fs_copy (svn_fs_root_t *from_root, const char *from_path,
             svn_fs_root_t *to_root, const char *to_path, apr_pool_t *pool)
{
  return to_root->vtable->copy (from_root, from_path, to_root, to_path, pool);
}

svn_error_t *
svn_fs_revision_link (svn_fs_root_t *from_root, svn_fs_root_t *to_root,
                      const char *path, apr_pool_t *pool)
{
  return to_root->vtable->revision_link (from_root, to_root, path, pool);
}

svn_error_t *
svn_fs_file_length (svn_filesize_t *length_p, svn_fs_root_t *root,
                    const char *path, apr_pool_t *pool)
{
  return root->vtable->file_length (length_p, root, path, pool);
}

svn_error_t *
svn_fs_file_md5_checksum (unsigned char digest[], svn_fs_root_t *root,
                          const char *path, apr_pool_t *pool)
{
  return root->vtable->file_md5_checksum (digest, root, path, pool);
}

svn_error_t *
svn_fs_file_contents (svn_stream_t **contents, svn_fs_root_t *root,
                      const char *path, apr_pool_t *pool)
{
  return root->vtable->file_contents (contents, root, path, pool);
}

svn_error_t *
svn_fs_make_file (svn_fs_root_t *root, const char *path, apr_pool_t *pool)
{
  return root->vtable->make_file (root, path, pool);
}

svn_error_t *
svn_fs_apply_textdelta (svn_txdelta_window_handler_t *contents_p,
                        void **contents_baton_p, svn_fs_root_t *root,
                        const char *path, const char *base_checksum,
                        const char *result_checksum, apr_pool_t *pool)
{
  return root->vtable->apply_textdelta (contents_p, contents_baton_p, root,
                                        path, base_checksum, result_checksum,
                                        pool);
}

svn_error_t *
svn_fs_apply_text (svn_stream_t **contents_p, svn_fs_root_t *root,
                   const char *path, const char *result_checksum,
                   apr_pool_t *pool)
{
  return root->vtable->apply_text (contents_p, root, path, result_checksum,
                                   pool);
}

svn_error_t *
svn_fs_contents_changed (svn_boolean_t *changed_p, svn_fs_root_t *root1,
                         const char *path1, svn_fs_root_t *root2,
                         const char *path2, apr_pool_t *pool)
{
  return root1->vtable->contents_changed (changed_p, root1, path1, root2,
                                          path2, pool);
}

svn_error_t *
svn_fs_youngest_rev (svn_revnum_t *youngest_p, svn_fs_t *fs, apr_pool_t *pool)
{
  return fs->vtable->youngest_rev (youngest_p, fs, pool);
}

svn_error_t *
svn_fs_deltify_revision (svn_fs_t *fs, svn_revnum_t revision, apr_pool_t *pool)
{
  return fs->vtable->deltify (fs, revision, pool);
}

svn_error_t *
svn_fs_revision_prop (svn_string_t **value_p, svn_fs_t *fs, svn_revnum_t rev,
                      const char *propname, apr_pool_t *pool)
{
  return fs->vtable->revision_prop (value_p, fs, rev, propname, pool);
}

svn_error_t *
svn_fs_revision_proplist (apr_hash_t **table_p, svn_fs_t *fs, svn_revnum_t rev,
                          apr_pool_t *pool)
{
  return fs->vtable->revision_proplist (table_p, fs, rev, pool);
}

svn_error_t *
svn_fs_change_rev_prop (svn_fs_t *fs, svn_revnum_t rev, const char *name,
                        const svn_string_t *value, apr_pool_t *pool)
{
  return fs->vtable->change_rev_prop (fs, rev, name, value, pool);
}

svn_error_t *
svn_fs_get_file_delta_stream (svn_txdelta_stream_t **stream_p,
                              svn_fs_root_t *source_root,
                              const char *source_path,
                              svn_fs_root_t *target_root,
                              const char *target_path, apr_pool_t *pool)
{
  return target_root->vtable->get_file_delta_stream (stream_p, source_root,
                                                     source_path, target_root,
                                                     target_path, pool);
}

svn_error_t *
svn_fs_get_uuid (svn_fs_t *fs, const char **uuid, apr_pool_t *pool)
{
  return fs->vtable->get_uuid (fs, uuid, pool);
}

svn_error_t *
svn_fs_set_uuid (svn_fs_t *fs, const char *uuid, apr_pool_t *pool)
{
  return fs->vtable->set_uuid (fs, uuid, pool);
}


/* --- Public node-ID functions --- */

svn_fs_id_t *
svn_fs_parse_id (const char *data, apr_size_t data_len, apr_pool_t *pool)
{
  svn_fs_id_t *id;
  char *data_copy;
  char *dot;

  /* Dup the ID data into POOL.  Our returned ID will have references
     into this memory. */
  data_copy = apr_pstrmemdup (pool, data, data_len);

  /* Alloc a new svn_fs_id_t structure. */
  id = apr_palloc (pool, sizeof (*id));

  /* Now, we basically just need to "split" this data on `.'
     characters.  There should be exactly three pieces (around two
     `.'s) as a result.  To do this, we'll just replace the `.'s with
     NULL terminators, and do fun pointer-y things.  */

  /* Node Id */
  id->node_id = data_copy;
  dot = strchr (id->node_id, '.');
  if ((! dot) || (dot <= id->node_id))
    return NULL;
  *dot = 0;

  /* Copy Id */
  id->copy_id = dot + 1;
  dot = strchr (id->copy_id, '.');
  if ((! dot) || (dot <= id->copy_id))
    return NULL;
  *dot = 0;
  
  /* Txn Id */
  id->txn_id = dot + 1;
  dot = strchr (id->copy_id, '.');
  if (dot)
    return NULL;

  /* Return our ID */
  return id;
}

svn_string_t *
svn_fs_unparse_id (const svn_fs_id_t *id, apr_pool_t *pool)
{
  return svn_string_createf (pool, "%s.%s.%s", 
                             id->node_id, id->copy_id, id->txn_id);
}

svn_boolean_t
svn_fs_check_related (const svn_fs_id_t *id1, const svn_fs_id_t *id2)
{
  if (id1 == id2)
    return TRUE;
  if (id1->node_id == id2->node_id)
    return TRUE;
  return (strcmp (id1->node_id, id2->node_id) == 0) ? TRUE : FALSE;
}

int 
svn_fs_compare_ids (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  if (svn_fs__id_eq (a, b))
    return 0;
  return (svn_fs_check_related (a, b) ? 1 : -1);
}


/* --- Node-rev ID utility functions --- */

svn_fs_id_t *
svn_fs__create_id (const char *node_id, const char *copy_id,
                   const char *txn_id, apr_pool_t *pool)
{
  svn_fs_id_t *id = apr_palloc (pool, sizeof (*id));
  id->node_id = apr_pstrdup (pool, node_id);
  id->copy_id = apr_pstrdup (pool, copy_id);
  id->txn_id = apr_pstrdup (pool, txn_id);
  return id;
}

const char *
svn_fs__id_node_id (const svn_fs_id_t *id)
{
  return id->node_id;
}

const char *
svn_fs__id_copy_id (const svn_fs_id_t *id)
{
  return id->copy_id;
}

const char *
svn_fs__id_txn_id (const svn_fs_id_t *id)
{
  return id->txn_id;
}

svn_fs_id_t *
svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_palloc (pool, sizeof (*new_id));

  new_id->node_id = apr_pstrdup (pool, id->node_id);
  new_id->copy_id = apr_pstrdup (pool, id->copy_id);
  new_id->txn_id = apr_pstrdup (pool, id->txn_id);
  return new_id;
}

svn_boolean_t
svn_fs__id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  if (a != b)
    {  
      if (a->node_id != b->node_id && strcmp (a->node_id, b->node_id) != 0)
        return FALSE;
      if (a->copy_id != b->copy_id && strcmp (a->copy_id, b->copy_id) != 0)
        return FALSE;
      if (a->txn_id != b->txn_id && strcmp (a->txn_id, b->txn_id) != 0)
        return FALSE;
    }
  return TRUE;
}


/* --- Miscellaneous utility functions --- */

const char *
svn_fs__canonicalize_abspath (const char *path, apr_pool_t *pool)
{
  char *newpath;
  int path_len;
  int path_i = 0, newpath_i = 0;
  svn_boolean_t eating_slashes = FALSE;

  /* No PATH?  No problem. */
  if (! path)
    return NULL;
  
  /* Empty PATH?  That's just "/". */
  if (! *path)
    return apr_pstrdup (pool, "/");

  /* Now, the fun begins.  Alloc enough room to hold PATH with an
     added leading '/'. */
  path_len = strlen (path);
  newpath = apr_pcalloc (pool, path_len + 2);

  /* No leading slash?  Fix that. */
  if (*path != '/')
    {
      newpath[newpath_i++] = '/';
    }
  
  for (path_i = 0; path_i < path_len; path_i++)
    {
      if (path[path_i] == '/')
        {
          /* The current character is a '/'.  If we are eating up
             extra '/' characters, skip this character.  Else, note
             that we are now eating slashes. */
          if (eating_slashes)
            continue;
          eating_slashes = TRUE;
        }
      else
        {
          /* The current character is NOT a '/'.  If we were eating
             slashes, we need not do that any more. */
          if (eating_slashes)
            eating_slashes = FALSE;
        }

      /* Copy the current character into our new buffer. */
      newpath[newpath_i++] = path[path_i];
    }
  
  /* Did we leave a '/' attached to the end of NEWPATH (other than in
     the root directory case)? */
  if ((newpath[newpath_i - 1] == '/') && (newpath_i > 1))
    newpath[newpath_i - 1] = '\0';

  return newpath;
}
