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


#include <string.h>
#include <apr.h>
#include <apr_hash.h>
#include <apr_dso.h>

#include "svn_types.h"
#include "svn_version.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "fs-loader.h"

/* This is defined by configure on platforms which use configure, but
   we need to define a fallback for Windows. */
#ifndef DEFAULT_FS_TYPE
#define DEFAULT_FS_TYPE "bdb"
#endif

#define FS_TYPE_FILENAME "fs-type"

/* The implementation of this library is deliberately not separated
   into multiple files, to avoid circular dependency problems with
   Unix static linking.  We want FS back ends to be able to use our
   functions without forcing applications to link against this library
   twice. */


/* --- Utility functions for the loader --- */

static const struct fs_type_defn {
  const char *fs_type;
  const char *fsap_name;
  fs_init_func_t initfunc;
} fs_modules[] = {
  {
    SVN_FS_TYPE_BDB, "base",
#ifdef SVN_LIBSVN_FS_LINKS_FS_BASE
    svn_fs_base__init
#endif
  },

  {
    SVN_FS_TYPE_FSFS, "fs",
#ifdef SVN_LIBSVN_FS_LINKS_FS_FS
    svn_fs_fs__init
#endif
  },

  { NULL }
};

static svn_error_t *
load_module (fs_init_func_t *initfunc, const char *name, apr_pool_t *pool)
{
  *initfunc = NULL;

#if APR_HAS_DSO
  {
    apr_dso_handle_t *dso;
    apr_dso_handle_sym_t symbol;
    const char *libname;
    const char *funcname;
    apr_status_t status;

    libname = apr_psprintf (pool, "libsvn_fs_%s-%d.so.0",
                            name, SVN_VER_MAJOR);
    funcname = apr_psprintf (pool, "svn_fs_%s__init", name);

    /* Find/load the specified library.  If we get an error, assume
       the library doesn't exist.  The library will be unloaded when
       pool is destroyed. */
    status = apr_dso_load (&dso, libname, pool);
    if (status)
      return SVN_NO_ERROR;

    /* find the initialization routine */
    status = apr_dso_sym (&symbol, dso, funcname);
    if (status)
      return svn_error_wrap_apr (status, _("'%s' does not define '%s()'"),
                                 libname, funcname);

    *initfunc = (fs_init_func_t) symbol;
  }
#endif /* APR_HAS_DSO */

  return SVN_NO_ERROR;
}

/* Fetch a library vtable by FS type. */
static svn_error_t *
get_library_vtable (fs_library_vtable_t **vtable, const char *fs_type,
                    apr_pool_t *pool)
{
  const struct fs_type_defn *fst;
  const char *fsap_name;
  fs_init_func_t initfunc = NULL;
  const svn_version_t *my_version = svn_fs_version();
  const svn_version_t *fs_version;

  for (fst = fs_modules; fst->fs_type; fst++)
    {
      if (strcmp (fs_type, fst->fs_type) == 0)
        break;
    }

  if (fst->fs_type)
    {
      fsap_name = fst->fsap_name;
      initfunc = fst->initfunc;
      if (! initfunc)
        SVN_ERR (load_module (&initfunc, fsap_name, pool));
    }

  if (! initfunc)
    return svn_error_createf (SVN_ERR_FS_UNKNOWN_FS_TYPE, NULL,
                              _("Unknown FS type '%s'"), fs_type);

  SVN_ERR (initfunc (my_version, vtable));
  fs_version = (*vtable)->get_version();
  if (!svn_ver_equal (my_version, fs_version))
    return svn_error_createf (SVN_ERR_VERSION_MISMATCH, NULL,
                              _("Mismatched FS module version for '%s':"
                                " found %d.%d.%d%s,"
                                " expected %d.%d.%d%s"),
                              fs_type,
                              my_version->major, my_version->minor,
                              my_version->patch, my_version->tag,
                              fs_version->major, fs_version->minor,
                              fs_version->patch, fs_version->tag);
  return SVN_NO_ERROR;
}

/* Fetch the library vtable for an existing FS. */
static svn_error_t *
fs_library_vtable (fs_library_vtable_t **vtable, const char *path,
                   apr_pool_t *pool)
{
  const char *filename, *fs_type;
  char buf[128];
  svn_error_t *err;
  apr_file_t *file;
  apr_size_t len;

  /* Read the fsap-name file to get the FSAP name, or assume the default. */
  filename = svn_path_join (path, FS_TYPE_FILENAME, pool);
  err = svn_io_file_open (&file, filename, APR_READ|APR_BUFFERED, 0, pool);
  if (err && APR_STATUS_IS_ENOENT (err->apr_err))
    {
      svn_error_clear (err);
      fs_type = SVN_FS_TYPE_BDB;
    }
  else if (err)
    return err;
  else
    {
      len = sizeof(buf);
      SVN_ERR (svn_io_read_length_line (file, buf, &len, pool));
      SVN_ERR (svn_io_file_close (file, pool));
      fs_type = buf;
    }

  /* Fetch the library vtable by name, now that we've chosen one. */
  return get_library_vtable (vtable, fs_type, pool);
}

static svn_error_t *
write_fs_type (const char *path, const char *fs_type, apr_pool_t *pool)
{
  const char *filename;
  apr_file_t *file;

  filename = svn_path_join (path, FS_TYPE_FILENAME, pool);
  SVN_ERR (svn_io_file_open (&file, filename,
                             APR_WRITE|APR_CREATE|APR_TRUNCATE|APR_BUFFERED,
                             APR_OS_DEFAULT, pool));
  SVN_ERR (svn_io_file_write_full (file, fs_type, strlen(fs_type), NULL,
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

  fs = apr_palloc (pool, sizeof (*fs));
  fs->pool = pool;
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
svn_fs_create (svn_fs_t **fs_p, const char *path, apr_hash_t *fs_config,
               apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  const char *fs_type = NULL;

  if (fs_config)
    fs_type = apr_hash_get (fs_config, SVN_FS_CONFIG_FS_TYPE,
                            APR_HASH_KEY_STRING);
  if (fs_type == NULL)
    fs_type = DEFAULT_FS_TYPE;
  SVN_ERR (get_library_vtable (&vtable, fs_type, pool));

  /* Create the FS directory and write out the fsap-name file. */
  SVN_ERR (svn_io_dir_make_sgid (path, APR_OS_DEFAULT, pool));
  SVN_ERR (write_fs_type (path, fs_type, pool));

  /* Perform the actual creation. */
  *fs_p = svn_fs_new (fs_config, pool);
  return vtable->create (*fs_p, path, pool);
}

svn_error_t *
svn_fs_open (svn_fs_t **fs_p, const char *path, apr_hash_t *fs_config,
             apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (fs_library_vtable (&vtable, path, pool));
  *fs_p = svn_fs_new (fs_config, pool);
  return vtable->open (*fs_p, path, pool);
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
  const char *path;
  svn_node_kind_t kind;

  SVN_ERR (fs_library_vtable (&vtable, src_path, pool));
  SVN_ERR (vtable->hotcopy (src_path, dest_path, clean, pool));

  /* Copy the fs-type file. */
  path = svn_path_join (src_path, FS_TYPE_FILENAME, pool);
  SVN_ERR (svn_io_check_path (path, &kind, pool));
  if (kind != svn_node_none)
    SVN_ERR (svn_io_dir_file_copy (src_path, dest_path, FS_TYPE_FILENAME,
                                   pool));

  return SVN_NO_ERROR;
}


/* --- Berkeley-specific functions --- */

svn_error_t *
svn_fs_create_berkeley (svn_fs_t *fs, const char *path)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, fs->pool));

  /* Create the FS directory and write out the fsap-name file. */
  SVN_ERR (svn_io_dir_make (path, APR_OS_DEFAULT, fs->pool));
  SVN_ERR (write_fs_type (path, SVN_FS_TYPE_BDB, fs->pool));

  /* Perform the actual creation. */
  return vtable->create (fs, path, fs->pool);
}

svn_error_t *
svn_fs_open_berkeley (svn_fs_t *fs, const char *path)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, fs->pool));
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

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, pool));
  return vtable->delete_fs (path, pool);
}

svn_error_t *
svn_fs_hotcopy_berkeley (const char *src_path, const char *dest_path,
                         svn_boolean_t clean_logs, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, pool));
  SVN_ERR (vtable->hotcopy (src_path, dest_path, clean_logs, pool));
  SVN_ERR (write_fs_type (dest_path, SVN_FS_TYPE_BDB, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_berkeley_recover (const char *path, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, pool));
  return vtable->bdb_recover (path, pool);
}

svn_error_t *
svn_fs_set_berkeley_errcall (svn_fs_t *fs,
                             void (*handler) (const char *errpfx, char *msg))
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, fs->pool));
  return vtable->bdb_set_errcall (fs, handler);
}

svn_error_t *
svn_fs_berkeley_logfiles (apr_array_header_t **logfiles,
                          const char *path,
                          svn_boolean_t only_unused,
                          apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;

  SVN_ERR (get_library_vtable (&vtable, SVN_FS_TYPE_BDB, pool));
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
  SVN_ERR (svn_path_check_valid (path, pool));
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
  SVN_ERR (svn_path_check_valid (to_path, pool));
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
  SVN_ERR (svn_path_check_valid (path, pool));
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


/* --- History functions --- */

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


/* --- Node-ID functions --- */

svn_fs_id_t *
svn_fs_parse_id (const char *data, apr_size_t len, apr_pool_t *pool)
{
  fs_library_vtable_t *vtable;
  svn_error_t *err;

  err = get_library_vtable (&vtable, SVN_FS_TYPE_BDB, pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }
  return vtable->parse_id (data, len, pool);
}

svn_string_t *
svn_fs_unparse_id (const svn_fs_id_t *id, apr_pool_t *pool)
{
  return id->vtable->unparse (id, pool);
}

svn_boolean_t
svn_fs_check_related (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  return (a->vtable->compare (a, b) != -1);
}

int 
svn_fs_compare_ids (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  return a->vtable->compare (a, b);
}

/* Return the library version number. */
const svn_version_t *
svn_fs_version (void)
{
  SVN_VERSION_BODY;
}
