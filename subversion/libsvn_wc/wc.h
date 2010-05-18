/*
 * wc.h :  shared stuff internal to the svn_wc library.
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


#ifndef SVN_LIBSVN_WC_H
#define SVN_LIBSVN_WC_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_wc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define SVN_WC__TMP_EXT       ".tmp"
#define SVN_WC__PROP_REJ_EXT  ".prej"
#define SVN_WC__BASE_EXT      ".svn-base" /* for text and prop bases */
#define SVN_WC__WORK_EXT      ".svn-work" /* for working propfiles */
#define SVN_WC__REVERT_EXT    ".svn-revert" /* for reverting a replaced
                                               file */




/* We can handle this format or anything lower, and we (should) error
 * on anything higher.
 *
 * There is no format version 0; we started with 1.
 *
 * The change from 1 to 2 was the introduction of SVN_WC__WORK_EXT.
 * For example, ".svn/props/foo" became ".svn/props/foo.svn-work".
 *
 * The change from 2 to 3 was the introduction of the entry attribute
 * SVN_WC__ENTRY_ATTR_ABSENT.
 *
 * The change from 3 to 4 was the renaming of the magic "svn:this_dir"
 * entry name to "".
 *
 * The change from 4 to 5 was the addition of support for replacing files
 * with history.
 *
 * The change from 5 to 6 was the introduction of caching of property
 * modification state and certain properties in the entries file.
 *
 * The change from 6 to 7 was changing the entries file format from XML.
 *
 * The change from 7 to 8 was putting wcprops in one file per directory.
 *
 * The change from 8 to 9 was the addition of changelists, keep-local,
 * and sticky depth (for selective/sparse checkouts).
 *
 * The change from 9 to 10 was the addition of tree-conflicts, file
 * externals and a different canonicalization of urls.
 *
 * Please document any further format changes here.
 */
#define SVN_WC__VERSION       10

/* A version <= this doesn't have property caching in the entries file. */
#define SVN_WC__NO_PROPCACHING_VERSION 5

/* A version <= this has the entries file in XML format. */
#define SVN_WC__XML_ENTRIES_VERSION 6

/* A version <= this has wcprops stored in one file per entry. */
#define SVN_WC__WCPROPS_MANY_FILES_VERSION 7

/* A version < this can have urls that aren't canonical according to the new
   rules. See issue #2475. */
#define SVN_WC__CHANGED_CANONICAL_URLS 10

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



/*** Timestamps. ***/

/* A special timestamp value which means "use the timestamp from the
   working copy".  This is sometimes used in a log entry like:

   <modify-entry name="foo.c" revision="5" timestamp="working"/>
 */
#define SVN_WC__TIMESTAMP_WC   "working"



/*** Filesizes. ***/

/* A special filesize value which means "use the filesize from the
   working copy".  This is sometimes used in a log entry like:

   <modify-entry name="foo.c" revision="5" working-size="working"/>
 */
#define SVN_WC__WORKING_SIZE_WC   "working"



/*** Names and file/dir operations in the administrative area. ***/

/** The files within the administrative subdir. **/
#define SVN_WC__ADM_FORMAT              "format"
#define SVN_WC__ADM_ENTRIES             "entries"
#define SVN_WC__ADM_LOCK                "lock"
#define SVN_WC__ADM_TMP                 "tmp"
#define SVN_WC__ADM_TEXT_BASE           "text-base"
#define SVN_WC__ADM_PROPS               "props"
#define SVN_WC__ADM_PROP_BASE           "prop-base"
#define SVN_WC__ADM_DIR_PROPS           "dir-props"
#define SVN_WC__ADM_DIR_PROP_BASE       "dir-prop-base"
#define SVN_WC__ADM_DIR_PROP_REVERT     "dir-prop-revert"
#define SVN_WC__ADM_WCPROPS             "wcprops"
#define SVN_WC__ADM_DIR_WCPROPS         "dir-wcprops"
#define SVN_WC__ADM_ALL_WCPROPS         "all-wcprops"
#define SVN_WC__ADM_LOG                 "log"
#define SVN_WC__ADM_KILLME              "KILLME"
#define SVN_WC__ADM_README              "README.txt"
#define SVN_WC__ADM_EMPTY_FILE          "empty-file"

/* The basename of the ".prej" file, if a directory ever has property
   conflicts.  This .prej file will appear *within* the conflicted
   directory.  */
#define SVN_WC__THIS_DIR_PREJ           "dir_conflicts"

/* Possible contents of the killme file.  If present, signals that the
   administrative area only should be deleted. */
#define SVN_WC__KILL_ADM_ONLY           "adm-only"


/* A space separated list of properties that we cache presence/absence of.
 *
 * Note that each entry contains information about which properties are cached
 * in that particular entry.  This constant is only used when writing entries.
 */
#define SVN_WC__CACHABLE_PROPS                                         \
SVN_PROP_SPECIAL " " SVN_PROP_EXTERNALS " " SVN_PROP_NEEDS_LOCK


/* A few declarations for stuff in util.c.
 * If this section gets big, move it all out into a new util.h file. */

/* Ensure that DIR exists. */
svn_error_t *svn_wc__ensure_directory(const char *path, apr_pool_t *pool);

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

/* Set *MODIFIED_P to non-zero if FILENAME's text is modified with
 * regard to the base revision, else set *MODIFIED_P to zero.
 * FILENAME is a path to the file, not just a basename. ADM_ACCESS
 * must be an access baton for @a FILENAME.
 *
 * If FORCE_COMPARISON is true, this function will not allow early
 * return mechanisms that avoid actual content comparison.  Instead,
 * if there is a text base, a full byte-by-byte comparison will be
 * done, and the entry checksum verified as well.  (This means that if
 * the text base is much longer than the working file, every byte of
 * the text base will still be examined.)
 *
 * If COMPARE_TEXTBASES is true, the comparison will be between a
 * detranslated version of *FILENAME and the text base, otherwise, a
 * translated version of the text base and *FILENAME will be compared.
 *
 * If FILENAME does not exist, consider it unmodified.  If it exists
 * but is not under revision control (not even scheduled for
 * addition), return the error SVN_ERR_ENTRY_NOT_FOUND.
 */
svn_error_t *
svn_wc__text_modified_internal_p(svn_boolean_t *modified_p,
                                 const char *filename,
                                 svn_boolean_t force_comparison,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_boolean_t compare_textbases,
                                 apr_pool_t *pool);



/* Merge the difference between LEFT and RIGHT into MERGE_TARGET,
   accumulating instructions to update the working copy into LOG_ACCUM.

   Note that, in the case of updating, the update can have sent new
   properties, which could affect the way the wc target is
   detranslated and compared with LEFT and RIGHT for merging.

   If COPYFROM_TEXT is not NULL, the "local mods" text should be taken
   from the path named there instead of from MERGE_TARGET (but the
   merge should still be installed into MERGE_TARGET).  The merge
   target is allowed to not be under version control in this case.

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
*/
svn_error_t *
svn_wc__merge_internal(svn_stringbuf_t **log_accum,
                       enum svn_wc_merge_outcome_t *merge_outcome,
                       const char *left,
                       svn_wc_conflict_version_t *left_version,
                       const char *right,
                       svn_wc_conflict_version_t *right_version,
                       const char *merge_target,
                       const char *copyfrom_text,
                       svn_wc_adm_access_t *adm_access,
                       const char *left_label,
                       const char *right_label,
                       const char *target_label,
                       svn_boolean_t dry_run,
                       const char *diff3_cmd,
                       const apr_array_header_t *merge_options,
                       const apr_array_header_t *prop_diff,
                       svn_wc_conflict_resolver_func_t conflict_func,
                       void *conflict_baton,
                       apr_pool_t *pool);

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
 * ANCHOR, TARGET, and ADM_ACCESS are as in svn_wc_get_update_editor3.
 *
 * @a requested_depth must be one of the following depth values:
 * @c svn_depth_infinity, @c svn_depth_empty, @c svn_depth_files,
 * @c svn_depth_immediates, or @c svn_depth_unknown.
 *
 * Allocations are done in POOL.
 */
svn_error_t *
svn_wc__ambient_depth_filter_editor(const svn_delta_editor_t **editor,
                                    void **edit_baton,
                                    const svn_delta_editor_t *wrapped_editor,
                                    void *wrapped_edit_baton,
                                    const char *anchor,
                                    const char *target,
                                    svn_wc_adm_access_t *adm_access,
                                    apr_pool_t *pool);

/* Similar to svn_wc_walk_entries3(), but also visit unversioned paths that
 * are tree conflict victims. For such a path, call the "found_entry"
 * callback but with a null "entry" parameter. Walk all entries including
 * hidden and schedule-delete entries, like with "show_hidden = TRUE" in
 * svn_wc_walk_entries3().
 *
 * @a adm_access should be an access baton in a set that includes @a path
 * (unless @a path is an unversioned victim of a tree conflict) and @a
 * path's parent directory (if available).  If neither is available, @a
 * adm_access may be null. */
svn_error_t *
svn_wc__walk_entries_and_tc(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_entry_callbacks2_t *walk_callbacks,
                            void *walk_baton,
                            svn_depth_t depth,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_WC_H */
