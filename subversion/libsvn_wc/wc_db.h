/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_wc_db.h
 * @brief The Subversion Working Copy Library - Metadata/Base-Text Support
 *
 * Requires:
 *            - A working copy
 *
 * Provides:
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:
 *            - The main working copy library
 */

#ifndef SVN_WC_DB_H
#define SVN_WC_DB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Context data structure for interacting with the administrative data.
 */
typedef struct svn_wc_db_t svn_wc_db_t;


typedef enum svn_wc_db_kind_t {
    svn_wc_db_kind_dir,
    svn_wc_db_kind_file,
    svn_wc_db_kind_symlink,

    svn_wc_db_kind_absent_dir,
    svn_wc_db_kind_absent_file,
    svn_wc_db_kind_absent_symlink
} svn_wc_db_kind_t;


/**
 * Open the administrative database for the working copy identified by the
 * (absolute) @a path. The (opaque) handle for interacting with the database
 * will be returned in @a db.
 *
 * The configuration options are provided by @a config, and must live at
 * least as long as the database.
 *
 * Intermediate allocations will be performed in @a scratch_pool, and the
 * resulting context will be allocated in @a result_pool.
 */
svn_error_t *
svn_wc__db_open(svn_wc_db_t **db,
                const char *path,
                svn_config_t *config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool);


/**
 * @defgroup svn_wc__db_base  BASE tree management
 * @{
 */

/* ### base_add_* can also replace. should be okay? */

/* ### props optional.  can children be optional? don't think so. */
svn_error_t *
svn_wc__db_base_add_directory(svn_wc_db_t *db,
                              const char *path,
                              svn_revnum_t revision,
                              apr_hash_t *props,
                              const apr_array_header_t *children,
                              const char *repos_url,
                              const char *repos_uuid,
                              apr_pool_t *scratch_pool);


/* ### contents, props, checksum are optional */
svn_error_t *
svn_wc__db_base_add_file(svn_wc_db_t *db,
                         const char *path,
                         svn_revnum_t revision,
                         apr_hash_t *props,
                         svn_stream_t *contents,
                         const char *checksum,
                         apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_base_set_props(svn_wc_db_t *db,
                          const char *path,
                          apr_hash_t *props,
                          apr_pool_t *scratch_pool);


/* ### lib pulls contents into storage. checksum optional. */
svn_error_t *
svn_wc__db_base_set_contents(svn_wc_db_t *db,
                             const char *path,
                             svn_stream_t *contents,
                             const char *checksum,
                             apr_pool_t *scratch_pool);


/* ### caller pushes contents into storage. checksum optional. */
svn_error_t *
svn_wc__db_base_get_writable_contents(svn_stream_t **contents,
                                      svn_wc_db_t *db,
                                      const char *path,
                                      const char *checksum,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);


/* ### what data to keep for a symlink? props optional. */
svn_error_t *
svn_wc__db_base_add_symlink(svn_wc_db_t *db,
                            const char *path,
                            svn_revnum_t revision,
                            apr_hash_t *props,
                            const char *target,
                            apr_pool_t *scratch_pool);


/* ### keep the revision? */
svn_error_t *
svn_wc__db_base_add_absent_node(svn_wc_db_t *db,
                                const char *path,
                                svn_revnum_t revision,
                                svn_wc_db_kind_t kind,
                                apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_base_delete(svn_wc_db_t *db,
                       const char *path,
                       apr_pool_t *scratch_pool);


/* ### revision is for dst_path. */
svn_error_t *
svn_wc__db_base_move(svn_wc_db_t *db,
                     const char *src_path,
                     const char *dst_path,
                     svn_revnum_t revision,
                     apr_pool_t *scratch_pool);


/* ### NULL may be given for OUT params */
svn_error_t *
svn_wc__db_base_get_info(svn_wc_db_kind_t *kind,
                         svn_revnum_t *revision,
                         const char **url,
                         const char **repos_url,
                         const char **repos_uuid,
                         svn_wc_db_t *db,
                         const char *path,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_base_get_prop(const svn_string_t **propval,
                         svn_wc_db_t *db,
                         const char *path,
                         const char *propname,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_base_get_props(apr_hash_t **props,
                          svn_wc_db_t *db,
                          const char *path,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/* ### return some basic info for each child? e.g. kind */
svn_error_t *
svn_wc__db_base_get_children(const apr_array_header_t **children,
                             svn_wc_db_t *db,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


/* ### NULL allowed for OUT params */
svn_error_t *
svn_wc__db_base_get_contents(svn_stream_t **contents,
                             const char **checksum,
                             svn_wc_db_t *db,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

svn_error_t *
svn_wc__db_base_get_symlink_target(const char **target,
                                   svn_wc_db_t *db,
                                   const char *path,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);


/** @} */

/**
 * @defgroup svn_wc__db_op  Operations on WORKING tree
 * @{
 */

/* ### svn cp WCPATH WCPATH ... can copy mixed base/working around */
svn_error_t *
svn_wc__db_op_copy(svn_wc_db_t *db,
                   const char *src_path,
                   const char *dst_path,
                   apr_pool_t *scratch_pool);


/* ### svn cp URL WCPATH ... copies pure repos into wc. only this "root"
   ### metadata is present. caller needs to "set" all information recursively.
   ### and caller definitely has to populate ACTUAL. */
/* ### mark node as absent? adding children or props: auto-convert away
   ### from absent? */
svn-error_t *
svn_wc__db_op_copy_url(svn_wc_db_t *db,
                       const char *path,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_revision,
                       apr_pool_t *scratch_pool);


/* ### props, children may be NULL */
svn_error_t *
svn_wc__db_op_add_directory(svn_wc_db_t *db,
                            const char *path,
                            apr_hash_t *props,
                            const apr_array_header_t *children,
                            apr_pool_t *scratch_pool);


/* ### props may be NULL */
svn_error_t *
svn_wc__db_op_add_file(svn_wc_db_t *db,
                       const char *path,
                       apr_hash_t *props,
                       apr_pool_t *scratch_pool);


/* ### props may be NULL */
svn_error_t *
svn_wc__db_op_add_symlink(svn_wc_db_t *db,
                          const char *path,
                          apr_hash_t *props,
                          const char *target,
                          apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_op_add_absent_node(svn_wc_db_t *db,
                              const char *path,
                              svn_wc_db_kind_t kind,
                              apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_op_set_prop(svn_wc_db_t *db,
                       const char *path,
                       const char *propname,
                       const svn_string_t *propval,
                       apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_op_set_props(svn_wc_db_t *db,
                        const char *path,
                        apr_hash_t *props,
                        apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_op_delete(svn_wc_db_t *db,
                     const char *path,
                     apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_op_move(svn_wc_db_t *db,
                   const char *src_path,
                   const char *dst_path,
                   apr_pool_t *scratch_pool);


/* ### mark PATH as (possibly) modified. "svn edit" ... right API here? */
svn_error_t *
svn_wc__db_op_modified(svn_wc_db_t *db,
                       const char *path,
                       apr_pool_t *scratch_pool);


/* ### any other operations possible on the working copy? */
/* ### relocate. revert. changelists. post-commit handling. resolved. status */

/* ### how to handle depth? */


/** @} */

/**
 * @defgroup svn_wc__db_read  Read operations on the BASE/WORKING tree
 * @{
 */

/* ### NULL may be given for OUT params. if the node has not been committed:
   ### url, repos_* will be NULL and revision will be SVN_INVALID_REVNUM
*/
svn_error_t *
svn_wc__db_read_get_info(svn_wc_db_kind_t *kind,
                         svn_revnum_t *revision,
                         const char **url,
                         const char **repos_url,
                         const char **repos_uuid,
                         svn_wc_db_t *db,
                         const char *path,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_read_get_prop(const svn_string_t **propval,
                         svn_wc_db_t *db,
                         const char *path,
                         const char *propname,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_read_get_props(apr_hash_t **props,
                          svn_wc_db_t *db,
                          const char *path,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/* ### return some basic info for each child? e.g. kind */
svn_error_t *
svn_wc__db_read_get_children(const apr_array_header_t **children,
                             svn_wc_db_t *db,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__db_read_get_symlink_target(const char **target,
                                   svn_wc_db_t *db,
                                   const char *path,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);


/* ### changelists.  */
/* ### bulk stuff like revision_status. */


/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_DB_H */
