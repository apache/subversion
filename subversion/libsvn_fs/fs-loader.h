/*
 * fs_loader.h:  Declarations for the FS loader library
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


#ifndef LIBSVN_FS_FS_H
#define LIBSVN_FS_FS_H

#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* The FS loader library implements the a front end to "filesystem
   abstract providers" (FSAPs), which implement the svn_fs API.

   The loader library divides up the FS API into five categories:

     - Top-level functions, which operate on paths to an FS
     - Functions which operate on an FS object
     - Functions which operate on a transaction object
     - Functions which operate on a root object
     - Functions which operate on a history object

   Some generic fields of the FS, transaction, root, and history
   objects are defined by the loader library; the rest are stored in
   the "fsap_data" field which is defined by the FSAP.  Likewise, some
   of the very simple svn_fs API functions (such as svn_fs_root_fs)
   are defined by the loader library, while the rest are implemented
   through vtable calls defined by the FSAP.

   If you are considering writing a new database-backed filesystem
   implementation, it may be appropriate to add a second, lower-level
   abstraction to the libsvn_fs_base library which currently
   implements the BDB filesystem type.  Consult the dev list for
   details on the "FSP-level" abstraction concept.
*/
   


/*** Top-level library vtable type ***/

/* This number will change when the ABI between the loader library and
   FS modules changes incompatibly, to make sure that old FS modules
   don't get accidently linked into a newer version of Subversion, or
   vice versa. */
#define FS_ABI_VERSION 1

typedef struct fs_library_vtable_t
{
  svn_error_t *(*create) (svn_fs_t *fs, const char *path, apr_pool_t *pool);
  svn_error_t *(*open) (svn_fs_t *fs, const char *path, apr_pool_t *pool);
  svn_error_t *(*delete_fs) (const char *path, apr_pool_t *pool);
  svn_error_t *(*hotcopy) (const char *src_path, const char *dest_path,
                           svn_boolean_t clean, apr_pool_t *pool);

  /* Provider-specific functions should go here, even if they could go
     in an object vtable, so that they are all kept together. */
  svn_error_t *(*bdb_set_errcall) (svn_fs_t *fs,
                                   void (*handler) (const char *errpfx,
                                                    char *msg));
  svn_error_t *(*bdb_recover) (const char *path, apr_pool_t *pool);
  svn_error_t *(*bdb_logfiles) (apr_array_header_t **logfiles,
                                const char *path, svn_boolean_t only_unused,
                                apr_pool_t *pool);

  /* This is to let the base provider implement the deprecated
     svn_fs_parse_id, which we've decided doesn't belong in the FS
     API.  If we change our minds and decide to add a real
     svn_fs_parse_id variant which takes an FS object, it should go
     into the FS vtable. */
  svn_fs_id_t *(*parse_id) (const char *data, apr_size_t len,
                            apr_pool_t *pool);
} fs_library_vtable_t;

/* This is the type of symbol an FS module defines to fetch the
   library vtable. */
typedef svn_error_t *(*fs_init_func_t) (fs_library_vtable_t **vtable,
                                        int abi_version);

/* Here are the declarations for the FS module init functions.  If we
   are using DSO loading, they won't actually be linked into
   libsvn_fs. */
svn_error_t *svn_fs_base__init (fs_library_vtable_t **vtable, int abi_version);
svn_error_t *svn_fs_fs__init (fs_library_vtable_t **vtable, int abi_version);



/*** vtable types for the abstract FS objects ***/

typedef struct fs_vtable_t
{
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
  svn_error_t *(*get_uuid) (svn_fs_t *fs, const char **uuid, apr_pool_t *pool);
  svn_error_t *(*set_uuid) (svn_fs_t *fs, const char *uuid, apr_pool_t *pool);
  svn_error_t *(*revision_root) (svn_fs_root_t **root_p, svn_fs_t *fs,
                                 svn_revnum_t rev, apr_pool_t *pool);
  svn_error_t *(*begin_txn) (svn_fs_txn_t **txn_p, svn_fs_t *fs,
                             svn_revnum_t rev, apr_pool_t *pool);
  svn_error_t *(*open_txn) (svn_fs_txn_t **txn, svn_fs_t *fs,
                            const char *name, apr_pool_t *pool);
  svn_error_t *(*purge_txn) (svn_fs_t *fs, const char *txn_id,
                             apr_pool_t *pool);
  svn_error_t *(*list_transactions) (apr_array_header_t **names_p,
                                     svn_fs_t *fs, apr_pool_t *pool);
  svn_error_t *(*deltify) (svn_fs_t *fs, svn_revnum_t rev, apr_pool_t *pool);
} fs_vtable_t;


typedef struct txn_vtable_t
{
  svn_error_t *(*commit) (const char **conflict_p, svn_revnum_t *new_rev,
			  svn_fs_txn_t *txn, apr_pool_t *pool);
  svn_error_t *(*abort) (svn_fs_txn_t *txn, apr_pool_t *pool);
  svn_error_t *(*get_prop) (svn_string_t **value_p, svn_fs_txn_t *txn,
                            const char *propname, apr_pool_t *pool);
  svn_error_t *(*get_proplist) (apr_hash_t **table_p, svn_fs_txn_t *txn,
                                apr_pool_t *pool);
  svn_error_t *(*change_prop) (svn_fs_txn_t *txn, const char *name,
			       const svn_string_t *value, apr_pool_t *pool);
  svn_error_t *(*root) (svn_fs_root_t **root_p, svn_fs_txn_t *txn,
			apr_pool_t *pool);
} txn_vtable_t;


/* Some of these operations accept multiple root arguments.  Since the
   roots may not all have the same vtable, we need a rule to determine
   which root's vtable is used.  The rule is: if one of the roots is
   named "target", we use that root's vtable; otherwise, we use the
   first root argument's vtable. */
typedef struct root_vtable_t
{
  /* Determining what has changed in a root */
  svn_error_t *(*paths_changed) (apr_hash_t **changed_paths_p,
                                 svn_fs_root_t *root,
                                 apr_pool_t *pool);

  /* Generic node operations */
  svn_error_t *(*check_path) (svn_node_kind_t *kind_p, svn_fs_root_t *root,
                              const char *path, apr_pool_t *pool);
  svn_error_t *(*node_history) (svn_fs_history_t **history_p,
                                svn_fs_root_t *root, const char *path,
                                apr_pool_t *pool);
  svn_error_t *(*node_id) (const svn_fs_id_t **id_p, svn_fs_root_t *root,
                           const char *path, apr_pool_t *pool);
  svn_error_t *(*node_created_rev) (svn_revnum_t *revision,
                                    svn_fs_root_t *root, const char *path,
                                    apr_pool_t *pool);
  svn_error_t *(*node_created_path) (const char **created_path,
                                     svn_fs_root_t *root, const char *path,
                                     apr_pool_t *pool);
  svn_error_t *(*delete_node) (svn_fs_root_t *root, const char *path,
                               apr_pool_t *pool);
  svn_error_t *(*copied_from) (svn_revnum_t *rev_p, const char **path_p,
                               svn_fs_root_t *root, const char *path,
                               apr_pool_t *pool);

  /* Property operations */
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

  /* Directories */
  svn_error_t *(*dir_entries) (apr_hash_t **entries_p, svn_fs_root_t *root,
                               const char *path, apr_pool_t *pool);
  svn_error_t *(*make_dir) (svn_fs_root_t *root, const char *path,
                            apr_pool_t *pool);
  svn_error_t *(*copy) (svn_fs_root_t *from_root, const char *from_path,
                        svn_fs_root_t *to_root, const char *to_path,
                        apr_pool_t *pool);
  svn_error_t *(*revision_link) (svn_fs_root_t *from_root,
                                 svn_fs_root_t *to_root,
                                 const char *path,
                                 apr_pool_t *pool);

  /* Files */
  svn_error_t *(*file_length) (svn_filesize_t *length_p, svn_fs_root_t *root,
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

  /* Merging. */
  svn_error_t *(*merge) (const char **conflict_p,
                         svn_fs_root_t *source_root,
                         const char *source_path,
                         svn_fs_root_t *target_root,
                         const char *target_path,
                         svn_fs_root_t *ancestor_root,
                         const char *ancestor_path,
                         apr_pool_t *pool);
} root_vtable_t;


typedef struct history_vtable_t
{
  svn_error_t *(*prev) (svn_fs_history_t **prev_history_p,
                        svn_fs_history_t *history, svn_boolean_t cross_copies,
                        apr_pool_t *pool);
  svn_error_t *(*location) (const char **path, svn_revnum_t *revision,
                            svn_fs_history_t *history, apr_pool_t *pool);
} history_vtable_t;


typedef struct id_vtable_t
{
  svn_string_t *(*unparse) (const svn_fs_id_t *id, apr_pool_t *pool);
  int (*compare) (const svn_fs_id_t *a, const svn_fs_id_t *b);
} id_vtable_t;



/*** Definitions of the abstract FS object types ***/

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
  apr_hash_t *config;

  /* FSAP-specific vtable and private data */
  fs_vtable_t *vtable;
  void *fsap_data;
};


struct svn_fs_txn_t
{
  /* The filesystem to which this transaction belongs */
  svn_fs_t *fs;

  /* The revision on which this transaction is based, or
     SVN_INVALID_REVISION if the transaction is not based on a
     revision at all */
  svn_revnum_t base_rev;

  /* The ID of this transaction */
  const char *id;

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
  svn_boolean_t is_txn_root;

  /* For transaction roots, the name of the transaction  */
  const char *txn;

  /* For revision roots, the number of the revision.  */
  svn_revnum_t rev;

  /* FSAP-specific vtable and private data */
  root_vtable_t *vtable;
  void *fsap_data;
};


struct svn_fs_history_t
{
  /* FSAP-specific vtable and private data */
  history_vtable_t *vtable;
  void *fsap_data;
};


struct svn_fs_id_t
{
  /* FSAP-specific vtable and private data */
  id_vtable_t *vtable;
  void *fsap_data;
};


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
