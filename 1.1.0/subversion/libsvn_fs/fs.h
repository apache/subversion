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


typedef struct fs_vtable_t
{
  svn_error_t *(*create) (svn_fs_t *fs, const char *path, svn_config_t *cfg);
  svn_error_t *(*open) (svn_fs_t *fs, const char *path, const int flag,
			   svn_config_t *cfg);
  void (*set_warning_func) (svn_fs_t *fs,svn_fs_warning_callback_t warning,
                            void *warning_baton);
  const char *(*get_path) (svn_fs_t *fs, apr_pool_t *pool);
  svn_error_t *(*delete_fs) (const char *PATH, apr_pool_t *pool);
  svn_error_t *(*recover) (const char *path, apr_pool_t *pool);
  svn_error_t *(*youngest_rev) (svn_revnum_t *youngest_p, svn_fs_t *fs,
                                apr_pool_t *pool);
  svn_error_t *(*revision_prop) (svn_string_t **value_p, svn_fs_t *fs,
                                 svn_revnum_t rev, const char *propname,
                                 apr_pool_t *pool);
  svn_error_t *(*revision_proplist) (apr_hash_t **table_p, svn_fs_t *fs,
                                     svn_revnum_t rev, apr_pool_t *pool);
  svn_error_t *(*change_rev_prop) (svn_fs_t *fs, svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);
  svn_error_t *(get_uuid) (svn_fs_t *fs, const char **uuid, apr_pool_t *pool);
  svn_error_t *(set_uuid) (svn_fs_t *fs, const char *uuid, 
                           apr_pool_t *pool);
  svn_error_t *(*revision_root) (svn_fs_root_t **root_p, svn_fs_t *fs,
                                 svn_revnum_t rev, apr_pool_t *pool);
  svn_error_t *(*begin_txn) (svn_fs_txn_t **txn_p, svn_fs_t *fs,
                             svn_revnum_t rev, svn_config_t *cfg, 
                             apr_pool_t *pool);
  svn_error_t *(*open_txn) (svn_fs_txn_t **txn, svn_fs_t *fs,
                            const char *name, svn_config_t *cfg, 
                            apr_pool_t *pool);
  svn_error_t *(*list_transactions) (apr_array_header_t **names_p,
                                     svn_fs_t *fs, apr_pool_t *pool);
  svn_error_t *(*deltify) (svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool);
} fs_vtable_t;

typedef struct txn_vtable_t
{
  svn_error_t *(*commit) (const char **conflict_p, svn_revnum_t *new_rev,
			  svn_fs_txn_t *txn);
  svn_error_t *(*abort) (svn_fs_txn_t *txn);
  svn_revnum_t (*base_revision) (svn_fs_txn_t *txn);
  svn_error_t *(*get_prop) (svn_string_t **value_p, svn_fs_txn_t *txn,
                            const char *propname, apr_pool_t *pool);
  svn_error_t *(*get_proplist) (apr_hash_t **table_p, svn_fs_txn_t *txn,
                                apr_pool_t *pool);
  svn_error_t *(*change_prop) (svn_fs_txn_t *txn, const char *name,
			       const svn_string_t *value, apr_pool_t *pool);
  svn_error_t *(*root) (svn_fs_root_t **root_p, svn_fs_txn_t *txn,
			svn_config_t *cfg, apr_pool_t *pool);
};

struct root_vtable_t
{
  /* Determining what has changed under a ROOT. */
  svn_error_t *(*paths_changed) (apr_hash_t **changed_paths_p,
                                 svn_fs_root_t *root,
                                 apr_pool_t *pool);
  svn_error_t (*check_path) (svn_node_kind_t *kind, svn_fs_root_t *root, cons
			     char *path, apr_pool_t *pool);
  svn_error_t *(*node_id) (const svn_fs_id_t **id_p, svn_fs_root_t *root,
                           const char *path, apr_pool_t *pool);
  svnn_error_t *(*node_created_rev) (svn_revnum_t *revision,
                                    svn_fs_root_t *root, const char *path,
                                    apr_pool_t *pool);
  svn_error_t *(*node_prop) (svn_string_t **value_p, svn_fs_root_t *root,
                             const char *path, const char *propname,
                             apr_pool_t *pool);
  svn_error_t *(*node_proplist) (apr_hash_t **table_p, svn_fs_root_t *root,
                                 const char *path, apr_pool_t *pool);
  svn_error_t *(*change_node_prop) (svn_fs_root_t *root, const char *path,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool);
  svn_error_t *(*props_changed) (int *changed_p, svn_fs_root_t *root1,
                                 const char *path1, svn_fs_root_t *root2,
                                 const char *path2, apr_pool_t *pool);
  svn_error_t *(*copied_from) (svn_revnum_t *rev_p, const char **path_p,
                               svn_fs_root_t *root, const char *path,
                               apr_pool_t *pool);

  /* Directories.  */
  svn_error_t *(*dir_entries) (apr_hash_t **entries_p, svn_fs_root_t *root,
                               const char *path, apr_pool_t *pool);
  svn_error_t *(*make_dir) (svn_fs_root_t *root, const char *path,
                            apr_pool_t *pool);
  svn_error_t *(*delete_node) (svn_fs_root_t *root, const char *path,
                              apr_pool_t *pool);
  svn_error_t *(*delete_tree) (svn_fs_root_t *root, const char *path,
                               apr_pool_t *pool);
  svn_error_t *(*rename) (svn_fs_root_t *root, const char *from,
                          const char *to, apr_pool_t *pool);
  svn_error_t *(*copy) (svn_fs_root_t *from_root, const char *from_path,
                        svn_fs_root_t *to_root, const char *to_path,
                        apr_pool_t *pool);
  svn_error_t *(*revision_link) (svn_fs_root_t *from_root,
                                 svn_fs_root_t *to_root,
                                 const char *path,
                                 apr_pool_t *pool);

  /* Files.  */
  svn_error_t *(*file_length) (apr_off_t *length_p, svn_fs_root_t *root,
                               const char *path, apr_pool_t *pool);
  svn_error_t *(*file_md5_checksum) (unsigned char digest[],
				     svn_fs_root_t *root,
                                     const char *path, apr_pool_t *pool);
  svn_error_t *(*file_contents) (svn_stream_t **contents,
				 svn_fs_root_t *root, const char *path,
				 apr_pool_t *pool);
  svn_error_t *(*make_file) (svn_fs_root_t *root, const char *path,
			     apr_pool_t *pool);
  svn_error_t *(*apply_textdelta) (svn_txdelta_window_handler_t *contents_p,
                                   void **contents_baton_p,
                                   svn_fs_root_t *root, const char *path,
                                   const char *base_checksum,
				   const char *result_checksum,
				   apr_pool_t *pool);
  svn_error_t *(*apply_text) (svn_stream_t **contents_p, svn_fs_root_t *root,
                              const char *path, const char *result_checksum,
                              apr_pool_t *pool);
  svn_error_t *(*contents_changed) (int *changed_p, svn_fs_root_t *root1,
                                    const char *path1, svn_fs_root_t *root2,
                                    const char *path2, apr_pool_t *pool);
  svn_error_t *(*get_file_delta_stream) (svn_txdelta_stream_t **stream_p,
                                         svn_fs_root_t *source_root,
                                         const char *source_path,
                                         svn_fs_root_t *target_root,
                                         const char *target_path,
                                         apr_pool_t *pool);

  svn_error_t *(*merge) (const char **conflict_p,
                         svn_fs_root_t *source_root,
                         const char *source_path,
                         svn_fs_root_t *target_root,
                         const char *target_path,
                         svn_fs_root_t *ancestor_root,
                         const char *ancestor_path,
                         apr_pool_t *pool);
};

struct svn_fs_t
{
  /* A pool managing this filesystem */
  apr_pool_t *pool;

  /* The path to the repository's top-level directory */
  char *path;

  /* A callback for printing warning messages */
  svn_fs_warning_callback_t warning;
  void *warning_baton;

  /* The filesystem configuration */
  svn_config_t *config;

  /* FSAP-specific vtable and private data */
  fs_vtable_t *vtable;
  void *fsap_data;
};

struct svn_fs_txn_t
{
  /* A pool managing this transaction.  Freeing the pool releases any
     DB resources used by the transaction, but does not delete the
     transaction. */
  apr_pool_t *pool;

  /* The filesystem to which this transaction belongs */
  svn_fs_t *fs;

  /* The revision on which this transaction is based, or
     SVN_INVALID_REVISION if the transaction is not based on a
     revision at all */
  svn_revnum_t base_rev;

  /* The ID of this transaction */
  const char *name;

  /* FSAP-specific vtable and private data */
  txn_vtable_t *vtable;
  void *fsap_data;
};

struct svn_fs_root_t
{
  /* A pool managing this root */
  apr_pool_t *pool;

  /* The filesystem to which this root belongs */
  svn_fs_t *fs;

  /* The kind of root this is */
  root_kind_t kind;

  /* For transaction roots, the name of the transaction  */
  const char *txn;

  /* For revision roots, the number of the revision.  */
  svn_revnum_t rev;

  /* FSAP-specific vtable and private data */
  root_vtable_t *vtable;
  void *fsap_data;
};
