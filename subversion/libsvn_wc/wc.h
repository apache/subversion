/*
 * wc.h :  shared stuff internal to the svn_wc library.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#ifndef SVN_LIBSVN_WC_H
#define SVN_LIBSVN_WC_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#include "private/svn_sqlite.h"
#include "private/svn_wc_private.h"
#include "private/svn_skel.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define SVN_WC__PROP_REJ_EXT  ".prej"

/* We can handle this format or anything lower, and we (should) error
 * on anything higher.
 *
 * There is no format version 0; we started with 1.
 *
 * The change from 1 to 2 was the introduction of the ".svn-work" extension.
 * For example, ".svn/props/foo" became ".svn/props/foo.svn-work".
 *
 * The change from 2 to 3 was the introduction of the entry attribute
 * old-and-busted.c::ENTRIES_ATTR_ABSENT.
 *
 * The change from 3 to 4 was the renaming of the magic "svn:this_dir"
 * entry name to "".
 *
 * == 1.0.x shipped with format 4
 * == 1.1.x shipped with format 4
 * == 1.2.x shipped with format 4
 * == 1.3.x shipped with format 4
 *
 * The change from 4 to 5 was the addition of support for replacing files
 * with history (the "revert base"). This was introduced in 1.4.0, but
 # buggy until 1.4.6.
 *
 * The change from 5 to 6 was the introduction of caching of property
 * modification state and certain properties in the entries file.
 *
 * The change from 6 to 7 was changing the entries file format from XML.
 *
 * The change from 7 to 8 was putting wcprops in one file per directory.
 *
 * == 1.4.x shipped with format 8
 *
 * The change from 8 to 9 was the addition of changelists, keep-local,
 * and sticky depth (for selective/sparse checkouts).
 *
 * == 1.5.x shipped with format 9
 *
 * The change from 9 to 10 was the addition of tree-conflicts, file
 * externals and a different canonicalization of urls.
 *
 * == 1.6.x shipped with format 10
 *
 * The change from 10 to 11 was clearing the has_props, has_prop_mods,
 * cachable_props, and present_props values in the entries file. Older
 * client expect proper values for these fields.
 *
 * The change from 11 to 12 was a switch from 'entries' to 'wc.db'.
 *
 * The change from 12 to 13 added the WORK_QUEUE table into 'wc.db', moved
 * the wcprops into the 'dav_cache' column in BASE_NODE, and stopped using
 * the 'incomplete_children' column of BASE_NODE.
 *
 * The change from 13 to 14 added the WCLOCKS table (and migrated locks
 * from the filesystem into wc.db), and some columns to ACTUAL_NODE for
 * future use.
 *
 * The change from 14 to 15 switched from depth='exclude' on directories to
 * using presence='exclude' within the BASE_NODE and WORKING_NODE tables.
 * This change also enabled exclude support on files and symlinks.
 *
 * The change from 15 to 16 added 'locked_levels' to WC_LOCK, setting
 * any existing locks to a level of 0. The 'md5_checksum' column was
 * added to PRISTINE for future use.
 *
 * The change from 16 to 17 added a '.svn/pristine' dir and moved the text
 * bases into the Pristine Store (the PRISTINE table and '.svn/pristine'
 * dir), and removed the '/.svn/text-base' dir.
 *
 * The change from 17 to 18 moved the properties from separate files in the
 * props and prop-base directory (and .svn for the dir itself) into the
 * wc.db file, and then removes the props and prop-base dir.
 *
 * The change from 18 to 19 introduces the 'single DB' per working copy.
 * All metadata is held in a single '.svn/wc.db' in the root directory of
 * the working copy.
 *
 * The change from 19 to 20 introduces NODES and drops BASE_NODE and
 * WORKING_NODE, op_depth is always 0 or 2.
 *
 * The change from 20 to 21 moved tree conflict storage from the
 * parent to the conflicted node.
 *
 * The change from 21 to 22 moved tree conflict storage from
 * conflict_data column to the tree_conflict_data column.
 *
 * The change from 22 to 23 introduced multi-layer op_depth processing for
 * NODES.
 *
 * The change from 23 to 24 started using the 'refcount' column of the
 * 'pristine' table correctly, instead of always setting it to '1'.
 *
 * The change from 24 to 25 introduced a NODES_CURRENT view.
 *
 * The change from 25 to 26 introduced a NODES_BASE view.
 *
 * The change from 26 to 27 stored conflict files as relpaths rather
 * than basenames.
 *
 * == 1.7.x shipped with format ???
 *
 * Please document any further format changes here.
 */

#define SVN_WC__VERSION 27


/* Formats <= this have no concept of "revert text-base/props".  */
#define SVN_WC__NO_REVERT_FILES 4

/* A version <= this doesn't have property caching in the entries file. */
#define SVN_WC__NO_PROPCACHING_VERSION 5

/* A version <= this has the entries file in XML format. */
#define SVN_WC__XML_ENTRIES_VERSION 6

/* A version <= this has wcprops stored in one file per entry. */
#define SVN_WC__WCPROPS_MANY_FILES_VERSION 7

/* A version < this can have urls that aren't canonical according to the new
   rules. See issue #2475. */
#define SVN_WC__CHANGED_CANONICAL_URLS 10

/* A version < this uses the old 'entries' file mechanism.  */
#define SVN_WC__WC_NG_VERSION 12

/* In this version, the wcprops are "lost" between files and wc.db. We want
   to ignore them in upgrades.  */
#define SVN_WC__WCPROPS_LOST 12

/* A version < this has no work queue (see workqueue.h).  */
#define SVN_WC__HAS_WORK_QUEUE 13

/* A version < this has wcprops located in files OR in wc.db. Versions using
   this format or later will only have wcprops in BASE_NODE.dav_cache.  */
#define SVN_WC__USES_DAV_CACHE 13

/* A version < this does not store properties in wc.db.  */
#define SVN_WC__PROPS_IN_DB 18

/* Return true iff error E indicates an "is not a working copy" type
   of error, either because something wasn't a working copy at all, or
   because it's a working copy from a previous version (in need of
   upgrade). */
#define SVN_WC__ERR_IS_NOT_CURRENT_WC(e) \
            ((e->apr_err == SVN_ERR_WC_NOT_WORKING_COPY) || \
             (e->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED))



/*** Context handling ***/
struct svn_wc_context_t
{
  /* The wc_db handle for this working copy. */
  svn_wc__db_t *db;

  /* Close the DB when we destroy this context?
     (This is used inside backward compat wrappers, and should only be
      modified by the proper create() functions. */
  svn_boolean_t close_db_on_destroy;

  /* The state pool for this context. */
  apr_pool_t *state_pool;
};

/**
 * Just like svn_wc_context_create(), only use the provided DB to construct
 * the context.
 *
 * Even though DB is not allocated from the same pool at *WC_CTX, it is
 * expected to remain open throughout the life of *WC_CTX.
 */
svn_error_t *
svn_wc__context_create_with_db(svn_wc_context_t **wc_ctx,
                               svn_config_t *config,
                               svn_wc__db_t *db,
                               apr_pool_t *result_pool);


/*** Committed Queue ***/

/**
 * Return the pool associated with QUEUE.  (This so we can keep some
 * deprecated functions that need to peek inside the QUEUE struct in
 * deprecated.c).
 */
apr_pool_t *
svn_wc__get_committed_queue_pool(const struct svn_wc_committed_queue_t *queue);


/** Internal helper for svn_wc_process_committed_queue2().
 *
 * ### If @a queue is NULL, then ...?
 * ### else:
 * Bump an item from @a queue (the one associated with @a
 * local_abspath) to @a new_revnum after a commit succeeds, recursing
 * if @a recurse is set.
 *
 * @a new_date is the (server-side) date of the new revision, or 0.
 *
 * @a rev_author is the (server-side) author of the new
 * revision; it may be @c NULL.
 *
 * @a new_dav_cache is a hash of dav property changes to be made to
 * the @a local_abspath.
 *   ### [JAF]  Is it? See svn_wc_queue_committed3(). It ends up being
 *   ### assigned as a whole to wc.db:BASE_NODE:dav_cache.
 *
 * If @a no_unlock is set, don't release any user locks on @a
 * local_abspath; otherwise release them as part of this processing.
 *
 * If @a keep_changelist is set, don't remove any changeset assignments
 * from @a local_abspath; otherwise, clear it of such assignments.
 *
 * If @a sha1_checksum is non-NULL, use it to identify the node's pristine
 * text.
 *
 * If @a old_externals is non-NULL, store the old version of just removed
 * svn:externals definitions on unshadowed directories in @a old_externals.
 *
 * Set TOP_OF_RECURSE to TRUE to show that this the top of a possibly
 * recursive commit operation.
 */
svn_error_t *
svn_wc__process_committed_internal(svn_wc__db_t *db,
                                   const char *local_abspath,
                                   svn_boolean_t recurse,
                                   svn_boolean_t top_of_recurse,
                                   svn_revnum_t new_revnum,
                                   apr_time_t new_date,
                                   const char *rev_author,
                                   apr_hash_t *new_dav_cache,
                                   svn_boolean_t no_unlock,
                                   svn_boolean_t keep_changelist,
                                   const svn_checksum_t *sha1_checksum,
                                   const svn_wc_committed_queue_t *queue,
                                   apr_hash_t *old_externals,
                                   apr_pool_t *scratch_pool);


/*** Update traversals. ***/

struct svn_wc_traversal_info_t
{
  /* The pool in which this structure and everything inside it is
     allocated. */
  apr_pool_t *pool;

  /* The before and after values of the SVN_PROP_EXTERNALS property,
   * for each directory on which that property changed.  These have
   * the same layout as those returned by svn_wc_edited_externals().
   *
   * The hashes, their keys, and their values are allocated in the
   * above pool.
   */
  apr_hash_t *externals_old;
  apr_hash_t *externals_new;

  /* The ambient depths of the working copy directories.  The keys are
     working copy paths (as for svn_wc_edited_externals()), the values
     are the result of svn_depth_to_word(depth_of_each_dir). */
  apr_hash_t *depths;
};


/*** Names and file/dir operations in the administrative area. ***/

/** The files within the administrative subdir. **/
#define SVN_WC__ADM_FORMAT              "format"
#define SVN_WC__ADM_ENTRIES             "entries"
#define SVN_WC__ADM_TMP                 "tmp"
#define SVN_WC__ADM_PRISTINE            "pristine"
#define SVN_WC__ADM_NONEXISTENT_PATH    "nonexistent-path"

/* The basename of the ".prej" file, if a directory ever has property
   conflicts.  This .prej file will appear *within* the conflicted
   directory.  */
#define SVN_WC__THIS_DIR_PREJ           "dir_conflicts"


/* A few declarations for stuff in util.c.
 * If this section gets big, move it all out into a new util.h file. */

/* Ensure that DIR exists. */
svn_error_t *svn_wc__ensure_directory(const char *path, apr_pool_t *pool);


/* Return a hash keyed by 'const char *' property names and with
   'svn_string_t *' values built from PROPS (which is an array of
   pointers to svn_prop_t's) or to NULL if PROPS is NULL or empty.
   PROPS items which lack a value will be ignored.  If PROPS contains
   multiple properties with the same name, each successive such item
   reached in a walk from the beginning to the end of the array will
   overwrite the previous in the returned hash.

   NOTE: While the returned hash will be allocated in RESULT_POOL, the
   items it holds will share storage with those in PROPS.

   ### This is rather the reverse of svn_prop_hash_to_array(), except
   ### that function's arrays contains svn_prop_t's, whereas this
   ### one's contains *pointers* to svn_prop_t's.  So much for
   ### consistency.  */
apr_hash_t *
svn_wc__prop_array_to_hash(const apr_array_header_t *props,
                           apr_pool_t *result_pool);


/* Baton for svn_wc__compat_call_notify_func below. */
typedef struct svn_wc__compat_notify_baton_t {
  /* Wrapped func/baton. */
  svn_wc_notify_func_t func;
  void *baton;
} svn_wc__compat_notify_baton_t;

/* Implements svn_wc_notify_func2_t.  Call BATON->func (BATON is of type
   svn_wc__compat_notify_baton_t), passing BATON->baton and the appropriate
   arguments from NOTIFY. */
void svn_wc__compat_call_notify_func(void *baton,
                                     const svn_wc_notify_t *notify,
                                     apr_pool_t *pool);

/* Set *MODIFIED_P to non-zero if LOCAL_ABSPATH's text is modified with
 * regard to the base revision, else set *MODIFIED_P to zero.  Also
 * set *EXECUTABLE_P and *READ_ONLY_P based on the files current
 * permissions.  (EXECUTABLE_P and READ_ONLY_P can individually be
 * NULL if the caller doesn't care about those attributes of the file.)
 *
 * If FORCE_COMPARISON is true, this function will not allow early
 * return mechanisms that avoid actual content comparison.  Instead,
 * if there is a text base, a full byte-by-byte comparison will be
 * done, and the entry checksum verified as well.  (This means that if
 * the text base is much longer than the working file, every byte of
 * the text base will still be examined.)
 *
 * If COMPARE_TEXTBASES is true, translate LOCAL_ABSPATH's EOL
 * style and keywords to repository-normal form according to its properties,
 * and compare the result with the text base.  If COMPARE_TEXTBASES is
 * false, translate the text base's EOL style and keywords to working-copy
 * form according to LOCAL_ABSPATH's properties, and compare the
 * result with LOCAL_ABSPATH.
 *
 * If LOCAL_ABSPATH does not exist, consider it unmodified.  If it exists
 * but is not under revision control (not even scheduled for
 * addition), return the error SVN_ERR_ENTRY_NOT_FOUND.
 *
 * If the text is unmodified and a write-lock is held this function
 * will ensure that the last-known-unmodified timestamp and
 * filesize of the file as recorded in DB matches the corresponding
 * attributes of the actual file.  (This is often referred to as
 * "timestamp repair", and serves to help future unforced is-modified
 * checks return quickly if the file remains untouched.)
 */
svn_error_t *
svn_wc__internal_file_modified_p(svn_boolean_t *modified_p,
                                 svn_boolean_t *executable_p,
                                 svn_boolean_t *read_only_p,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_boolean_t force_comparison,
                                 svn_boolean_t compare_textbases,
                                 apr_pool_t *scratch_pool);


/* Merge the difference between LEFT_ABSPATH and RIGHT_ABSPATH into
   TARGET_ABSPATH, return the appropriate work queue operations in
   *WORK_ITEMS.

   Note that, in the case of updating, the update can have sent new
   properties, which could affect the way the wc target is
   detranslated and compared with LEFT and RIGHT for merging.

   If COPYFROM_ABSPATH is not NULL, the "local mods" text should be
   taken from the path named there instead of from TARGET_ABSPATH
   (but the merge should still be installed into TARGET_ABSPATH).
   The merge target is allowed to not be under version control in
   this case.

   The merge result is stored in *MERGE_OUTCOME and merge conflicts
   are marked in MERGE_RESULT using LEFT_LABEL, RIGHT_LABEL and
   TARGET_LABEL.

   When DRY_RUN is true, no actual changes are made to the working copy.

   If DIFF3_CMD is specified, the given external diff3 tool will
   be used instead of our built in diff3 routines.

   When MERGE_OPTIONS are specified, they are used by the internal
   diff3 routines, or passed to the external diff3 tool.

   If CONFLICT_FUNC is non-NULL, then call it with CONFLICT_BATON if a
   conflict is encountered, giving the callback a chance to resolve
   the conflict (before marking the file 'conflicted').

   When LEFT_VERSION and RIGHT_VERSION are non-NULL, pass them to the
   conflict resolver as older_version and their_version.

   ## TODO: We should store the information in LEFT_VERSION and RIGHT_VERSION
            in the workingcopy for future retrieval via svn info.

   Property changes sent by the update are provided in PROP_DIFF.

   For a complete description, see svn_wc_merge3() for which this is
   the (loggy) implementation.

   *WORK_ITEMS will be allocated in RESULT_POOL. All temporary allocations
   will be performed in SCRATCH_POOL.
*/
svn_error_t *
svn_wc__internal_merge(svn_skel_t **work_items,
                       enum svn_wc_merge_outcome_t *merge_outcome,
                       svn_wc__db_t *db,
                       const char *left_abspath,
                       const svn_wc_conflict_version_t *left_version,
                       const char *right_abspath,
                       const svn_wc_conflict_version_t *right_version,
                       const char *target_abspath,
                       const char *copyfrom_abspath,
                       const char *left_label,
                       const char *right_label,
                       const char *target_label,
                       svn_boolean_t dry_run,
                       const char *diff3_cmd,
                       const apr_array_header_t *merge_options,
                       const apr_array_header_t *prop_diff,
                       svn_wc_conflict_resolver_func_t conflict_func,
                       void *conflict_baton,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/* A default error handler for svn_wc_walk_entries3().  Returns ERR in
   all cases. */
svn_error_t *
svn_wc__walker_default_error_handler(const char *path,
                                     svn_error_t *err,
                                     void *walk_baton,
                                     apr_pool_t *pool);

/* Set *EDITOR and *EDIT_BATON to an ambient-depth-based filtering
 * editor that wraps WRAPPED_EDITOR and WRAPPED_BATON.  This is only
 * required for operations where the requested depth is @c
 * svn_depth_unknown and the server's editor driver doesn't understand
 * depth.  It is safe for *EDITOR and *EDIT_BATON to start as
 * WRAPPED_EDITOR and WRAPPED_BATON.
 *
 * ANCHOR, TARGET, and DB are as in svn_wc_get_update_editor3.
 *
 * @a requested_depth must be one of the following depth values:
 * @c svn_depth_infinity, @c svn_depth_empty, @c svn_depth_files,
 * @c svn_depth_immediates, or @c svn_depth_unknown.
 *
 * If @a read_base is TRUE, always read the depth data from BASE_NODE
 * instead of from WORKING when that exists.
 *
 * Allocations are done in POOL.
 */
svn_error_t *
svn_wc__ambient_depth_filter_editor(const svn_delta_editor_t **editor,
                                    void **edit_baton,
                                    svn_wc__db_t *db,
                                    const char *anchor_abspath,
                                    const char *target,
                                    svn_boolean_t read_base,
                                    const svn_delta_editor_t *wrapped_editor,
                                    void *wrapped_edit_baton,
                                    apr_pool_t *result_pool);


/* Similar to svn_wc_conflicted_p3(), but with a wc_db parameter in place of
 * a wc_context. */
svn_error_t *
svn_wc__internal_conflicted_p(svn_boolean_t *text_conflicted_p,
                              svn_boolean_t *prop_conflicted_p,
                              svn_boolean_t *tree_conflicted_p,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool);


/* Internal version of svn_wc_transmit_text_deltas3(). */
svn_error_t *
svn_wc__internal_transmit_text_deltas(const char **tempfile,
                                      const svn_checksum_t **new_text_base_md5_checksum,
                                      const svn_checksum_t **new_text_base_sha1_checksum,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      svn_boolean_t fulltext,
                                      const svn_delta_editor_t *editor,
                                      void *file_baton,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);

/* Internal version of svn_wc_transmit_prop_deltas2(). */
svn_error_t *
svn_wc__internal_transmit_prop_deltas(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const svn_delta_editor_t *editor,
                                     void *baton,
                                     apr_pool_t *scratch_pool);

/* Library-internal version of svn_wc_ensure_adm4(). */
svn_error_t *
svn_wc__internal_ensure_adm(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *url,
                            const char *repos_root_url,
                            const char *repos_uuid,
                            svn_revnum_t revision,
                            svn_depth_t depth,
                            apr_pool_t *scratch_pool);


/* Library-internal version of svn_wc__changelist_match(). */
svn_boolean_t
svn_wc__internal_changelist_match(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_hash_t *clhash,
                                  apr_pool_t *scratch_pool);

/* Library-internal version of svn_wc_walk_status(), which see. */
svn_error_t *
svn_wc__internal_walk_status(svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_depth_t depth,
                             svn_boolean_t get_all,
                             svn_boolean_t no_ignore,
                             svn_boolean_t ignore_text_mods,
                             const apr_array_header_t *ignore_patterns,
                             svn_wc_status_func4_t status_func,
                             void *status_baton,
                             svn_wc_external_update_t external_func,
                             void *external_baton,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *scratch_pool);

/* Library-internal version of svn_wc__node_walk_children(), which see. */
svn_error_t *
svn_wc__internal_walk_children(svn_wc__db_t *db,
                               const char *local_abspath,
                               svn_boolean_t show_hidden,
                               svn_wc__node_found_func_t walk_callback,
                               void *walk_baton,
                               svn_depth_t walk_depth,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *scratch_pool);

/* Library-internal version of svn_wc_remove_from_revision_control2,
   which see.*/
svn_error_t *
svn_wc__internal_remove_from_revision_control(svn_wc__db_t *db,
                                              const char *local_abspath,
                                              svn_boolean_t destroy_wf,
                                              svn_boolean_t instant_error,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              apr_pool_t *scratch_pool);


/* Library-internal version of svn_wc__node_is_file_external(). */
svn_error_t *
svn_wc__internal_is_file_external(svn_boolean_t *file_external,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_pool_t *scratch_pool);

/* Library-internal version of svn_wc__node_get_schedule(). */
svn_error_t *
svn_wc__internal_node_get_schedule(svn_wc_schedule_t *schedule,
                                   svn_boolean_t *copied,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   apr_pool_t *scratch_pool);

/* Internal version of svn_wc__node_get_copyfrom_info */
svn_error_t *
svn_wc__internal_get_copyfrom_info(const char **copyfrom_root_url,
                                   const char **copyfrom_repos_relpath,
                                   const char **copyfrom_url,
                                   svn_revnum_t *copyfrom_rev,
                                   svn_boolean_t *is_copy_target,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);



/* Upgrade the wc sqlite database given in SDB for the wc located at
   WCROOT_ABSPATH. It's current/starting format is given by START_FORMAT.
   After the upgrade is complete (to as far as the automatic upgrade will
   perform), the resulting format is RESULT_FORMAT. All allocations are
   performed in SCRATCH_POOL.  */
svn_error_t *
svn_wc__upgrade_sdb(int *result_format,
                    const char *wcroot_abspath,
                    svn_sqlite__db_t *sdb,
                    int start_format,
                    apr_pool_t *scratch_pool);


svn_error_t *
svn_wc__wipe_postupgrade(const char *dir_abspath,
                         svn_boolean_t whole_admin,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool);

/* Check whether a node is a working copy root or switched.
 *
 * If LOCAL_ABSPATH is the root of a working copy, set *WC_ROOT to TRUE,
 * otherwise to FALSE.
 *
 * If KIND is not null, set *KIND to the node type of LOCAL_ABSPATH.
 *
 * If LOCAL_ABSPATH is switched against its parent in the same working copy
 * set *SWITCHED to TRUE, otherwise to FALSE.  SWITCHED can be NULL
 * if the result is not important.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *
svn_wc__check_wc_root(svn_boolean_t *wc_root,
                      svn_wc__db_kind_t *kind,
                      svn_boolean_t *switched,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool);

/* Ensure LOCAL_ABSPATH is still locked in DB.  Returns the error
 * SVN_ERR_WC_NOT_LOCKED if this is not the case.
 */
svn_error_t *
svn_wc__write_check(svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_H */
