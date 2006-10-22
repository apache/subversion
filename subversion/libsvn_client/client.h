/*
 * client.h :  shared stuff internal to the client library.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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


#ifndef SVN_LIBSVN_CLIENT_H
#define SVN_LIBSVN_CLIENT_H


#include <apr_pools.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"
#include "svn_client.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Set *REVNUM to the revision number identified by REVISION.
 *
 * If REVISION->kind is svn_opt_revision_number, just use
 * REVISION->value.number, ignoring PATH and RA_SESSION.
 *
 * Else if REVISION->kind is svn_opt_revision_committed,
 * svn_opt_revision_previous, or svn_opt_revision_base, or
 * svn_opt_revision_working, then the revision can be identified
 * purely based on the working copy's administrative information for
 * PATH, so RA_SESSION is ignored.  If PATH is not under revision
 * control, return SVN_ERR_UNVERSIONED_RESOURCE, or if PATH is null,
 * return SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED.
 * 
 * Else if REVISION->kind is svn_opt_revision_date or
 * svn_opt_revision_head, then RA_SESSION is used to retrieve the
 * revision from the repository (using REVISION->value.date in the
 * former case), and PATH is ignored.  If RA_SESSION is null,
 * return SVN_ERR_CLIENT_RA_ACCESS_REQUIRED. 
 *
 * Else if REVISION->kind is svn_opt_revision_unspecified, set
 * *REVNUM to SVN_INVALID_REVNUM.  
 *
 * Else return SVN_ERR_CLIENT_BAD_REVISION.
 * 
 * Use POOL for any temporary allocation.
 */
svn_error_t *
svn_client__get_revision_number(svn_revnum_t *revnum,
                                svn_ra_session_t *ra_session,
                                const svn_opt_revision_t *revision,
                                const char *path,
                                apr_pool_t *pool);

/* Return true if REVISION1 and REVISION2 would result in the same
   revision number if interpreted in the context of the same working
   copy and path and repository, or if both are of kind
   svn_opt_revision_unspecified.  Otherwise, return false. */
svn_boolean_t
svn_client__compare_revisions(svn_opt_revision_t *revision1,
                              svn_opt_revision_t *revision2);


/* Return true if the revision number for REVISION can be determined
   from just the working copy, or false if it can be determined from
   just the repository.
 
   NOTE: No other kinds of revisions should be possible; but if one
   day there are, this will return true for those kinds.
 */ 
svn_boolean_t 
svn_client__revision_is_local(const svn_opt_revision_t *revision);


/* Given the CHANGED_PATHS and REVISION from an instance of a
   svn_log_message_receiver_t function, determine at which location
   PATH may be expected in the next log message, and set *PREV_PATH_P
   to that value.  KIND is the node kind of PATH.  Set *ACTION_P to a
   character describing the change that caused this revision (as
   listed in svn_log_changed_path_t) and set *COPYFROM_REV_P to the
   revision PATH was copied from, or SVN_INVALID_REVNUM if it was not
   copied.  ACTION_P and COPYFROM_REV_P may be NULL, in which case
   they are not used.  Perform all allocations in POOL.

   This is useful for tracking the various changes in location a
   particular resource has undergone when performing an RA->get_logs()
   operation on that resource.  */
svn_error_t *svn_client__prev_log_path(const char **prev_path_p,
                                       char *action_p,
                                       svn_revnum_t *copyfrom_rev_p,
                                       apr_hash_t *changed_paths,
                                       const char *path,
                                       svn_node_kind_t kind,
                                       svn_revnum_t revision,
                                       apr_pool_t *pool);


/* Set *START_URL and *START_REVISION (and maybe *END_URL
   and *END_REVISION) to the revisions and repository URLs of one
   (or two) points of interest along a particular versioned resource's
   line of history.  PATH as it exists in "peg revision"
   REVISION identifies that line of history, and START and END
   specify the point(s) of interest (typically the revisions referred
   to as the "operative range" for a given operation) along that history.

   END may be of kind svn_opt_revision_unspecified (in which case
   END_URL and END_REVISION are not touched by the function);
   START and REVISION may not.

   RA_SESSION should be an open RA session pointing at the URL of PATH,
   or NULL, in which case this function will open its own temporary session.

   A NOTE ABOUT FUTURE REPORTING:

   If either START or END are greater than REVISION, then do a
   sanity check (since we cannot search future history yet): verify
   that PATH in the future revision(s) is the "same object" as the
   one pegged by REVISION.  In other words, all three objects must
   be connected by a single line of history which exactly passes
   through PATH at REVISION.  If this sanity check fails, return
   SVN_ERR_CLIENT_UNRELATED_RESOURCES.  If PATH doesn't exist in the future
   revision, SVN_ERR_FS_NOT_FOUND may also be returned.

   CTX is the client context baton.

   Use POOL for all allocations.  */
svn_error_t *
svn_client__repos_locations(const char **start_url,
                            svn_opt_revision_t **start_revision,
                            const char **end_url,
                            svn_opt_revision_t **end_revision,
                            svn_ra_session_t *ra_session,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            const svn_opt_revision_t *start,
                            const svn_opt_revision_t *end,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);


/* Given PATH_OR_URL, which contains either a working copy path or an
   absolute URL, a peg revision PEG_REVISION, and a desired revision
   REVISION, create an RA connection to that object as it exists in
   that revision, following copy history if necessary.  If REVISION is
   younger than PEG_REVISION, then PATH_OR_URL will be checked to see
   that it is the same node in both PEG_REVISION and REVISION.  If it
   is not, then @c SVN_ERR_CLIENT_UNRELATED_RESOURCES is returned.

   If PEG_REVISION's kind is svn_opt_revision_unspecified, it is
   interpreted as "head" for a URL or "working" for a working-copy path.

   Store the resulting ra_session in *RA_SESSION_P.  Store the actual
   revision number of the object in *REV_P, and the final resulting
   URL in *URL_P.

   Use authentication baton cached in CTX to authenticate against the
   repository.

   Use POOL for all allocations. */
svn_error_t *
svn_client__ra_session_from_path(svn_ra_session_t **ra_session_p,
                                 svn_revnum_t *rev_p,
                                 const char **url_p,
                                 const char *path_or_url,
                                 const svn_opt_revision_t *peg_revision,
                                 const svn_opt_revision_t *revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** RA callbacks ***/


/* This is the baton that we pass to RA->open(), and is associated with
   the callback table we provide to RA. */
typedef struct
{
  /* Holds the directory that corresponds to the REPOS_URL at RA->open()
     time. When callbacks specify a relative path, they are joined with
     this base directory. */
  const char *base_dir;
  svn_wc_adm_access_t *base_access;

  /* When true, makes sure temporary files are created
     outside the working copy. */
  svn_boolean_t read_only_wc;

  /* An array of svn_client_commit_item2_t * structures, present only
     during working copy commits. */
  apr_array_header_t *commit_items;

  /* A client context. */
  svn_client_ctx_t *ctx;

  /* The pool to use for session-related items. */
  apr_pool_t *pool;

} svn_client__callback_baton_t;


/* Open an RA session, returning it in *RA_SESSION.

   The root of the session is specified by BASE_URL and BASE_DIR.
   BASE_ACCESS is an access baton for BASE_DIR administrative data.

   Additional control parameters:

      - COMMIT_ITEMS is an array of svn_client_commit_item_t *
        structures, present only for working copy commits, NULL otherwise.

      - USE_ADMIN indicates that the RA layer should create tempfiles
        in the administrative area instead of in the working copy itself,
        and read properties from the administrative area.

      - READ_ONLY_WC indicates that the RA layer should not attempt to
        modify the WC props directly.

   BASE_DIR may be NULL if the RA operation does not correspond to a
   working copy (in which case, USE_ADMIN should be FALSE, and
   BASE_ACCESS should be null).

   The calling application's authentication baton is provided in CTX,
   and allocations related to this session are performed in POOL.

   NOTE: The reason for the _internal suffix of this function's name is to
   avoid confusion with the public API svn_client_open_ra_session(). */
svn_error_t *
svn_client__open_ra_session_internal(svn_ra_session_t **ra_session,
                                     const char *base_url,
                                     const char *base_dir,
                                     svn_wc_adm_access_t *base_access,
                                     apr_array_header_t *commit_items,
                                     svn_boolean_t use_admin,
                                     svn_boolean_t read_only_wc,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);



/* ---------------------------------------------------------------- */

/*** Commit ***/

/* Get the commit_baton to be used in couple with commit_callback. */
svn_error_t *svn_client__commit_get_baton(void **baton,
                                          svn_commit_info_t **info,
                                          apr_pool_t *pool);

/* The commit_callback function for storing svn_client_commit_info_t
   pointed by commit_baton. If the commit_info supplied by get_baton
   points to NULL after close_edit, it means the commit is a no-op.
*/
svn_error_t *svn_client__commit_callback(const svn_commit_info_t *commit_info,
                                         void *baton,
                                         apr_pool_t *pool);

/* ---------------------------------------------------------------- */

/*** Status ***/

/* Verify that the path can be deleted without losing stuff,
   i.e. ensure that there are no modified or unversioned resources
   under PATH.  This is similar to checking the output of the status
   command.  CTX is used for the client's config options.  POOL is
   used for all temporary allocations. */
svn_error_t * svn_client__can_delete(const char *path,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** Add/delete ***/

/* Read automatic properties matching PATH from CTX->config.
   Set *PROPERTIES to a hash containing propname/value pairs
   (const char * keys mapping to svn_string_t * values), or if
   auto-props are disabled, set *PROPERTIES to NULL.
   Set *MIMETYPE to the mimetype, if any, or to NULL.
   Allocate the hash table, keys, values, and mimetype in POOL. */
svn_error_t *svn_client__get_auto_props(apr_hash_t **properties,
                                        const char **mimetype,
                                        const char *path,
                                        svn_client_ctx_t *ctx,
                                        apr_pool_t *pool);
                            

/* The main logic for client deletion from a working copy. Deletes PATH
   from ADM_ACCESS.  If PATH (or any item below a directory PATH) is
   modified the delete will fail and return an error unless FORCE is TRUE.
   If DRY_RUN is TRUE all the checks are made to ensure that the delete can
   occur, but the working copy is not modifed.  If NOTIFY_FUNC is not
   null, it is called with NOTIFY_BATON for each file or directory deleted. */
svn_error_t * svn_client__wc_delete(const char *path,
                                    svn_wc_adm_access_t *adm_access,
                                    svn_boolean_t force,
                                    svn_boolean_t dry_run,
                                    svn_wc_notify_func2_t notify_func,
                                    void *notify_baton,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *pool);

/* Return the list of WC paths to entries which would have been
   deleted by an update/merge if not in "dry run" mode, or NULL if not
   in "dry run" mode.  MERGE_CMD_BATON contains the list, which is
   intended for direct modification. */
apr_hash_t *svn_client__dry_run_deletions(void *merge_cmd_baton);

/* ---------------------------------------------------------------- */

/*** Checkout, update and switch ***/

/* Update a working copy PATH to REVISION, and (if not NULL) set
   RESULT_REV to the update revision.  RECURSE if so commanded;
   likewise, possibly IGNORE_EXTERNALS.  If TIMESTAMP_SLEEP is NULL this
   function will sleep before returning to ensure timestamp integrity.
   If TIMESTAMP_SLEEP is not NULL then the function will not sleep but
   will set *TIMESTAMP_SLEEP to TRUE if a sleep is required, and will
   not change *TIMESTAMP_SLEEP if no sleep is required. */
svn_error_t *
svn_client__update_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            svn_boolean_t recurse,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* Checkout into PATH a working copy of URL at REVISION, and (if not
   NULL) set RESULT_REV to the checked out revision.  RECURSE if so
   commanded; likewise, possibly IGNORE_EXTERNALS.  If TIMESTAMP_SLEEP
   is NULL this function will sleep before returning to ensure
   timestamp integrity.  If TIMESTAMP_SLEEP is not NULL then the
   function will not sleep but will set *TIMESTAMP_SLEEP to TRUE if a
   sleep is required, and will not change *TIMESTAMP_SLEEP if no sleep
   is required. */
svn_error_t *
svn_client__checkout_internal(svn_revnum_t *result_rev,
                              const char *URL,
                              const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *revision,
                              svn_boolean_t recurse,
                              svn_boolean_t ignore_externals,
                              svn_boolean_t *timestamp_sleep,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool);

/* Switch a working copy PATH to URL at REVISION, and (if not NULL)
   set RESULT_REV to the switch revision.  RECURSE if so commanded.
   If TIMESTAMP_SLEEP is NULL this function will sleep before
   returning to ensure timestamp integrity.  If TIMESTAMP_SLEEP is not
   NULL then the function will not sleep but will set *TIMESTAMP_SLEEP
   to TRUE if a sleep is required, and will not change
   *TIMESTAMP_SLEEP if no sleep is required. */
svn_error_t *
svn_client__switch_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const char *url,
                            const svn_opt_revision_t *revision,
                            svn_boolean_t recurse,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* ---------------------------------------------------------------- */

/*** Editor for repository diff ***/

/* Create an editor for a pure repository comparison, i.e. comparing one
 * repository version against the other. 
 *
 * TARGET is a working-copy path, the base of the hierarchy to be
 * compared.  It corresponds to the URL opened in RA_SESSION below.
 *
 * ADM_ACCESS is an access baton with a write lock for the anchor of
 * TARGET.  It should lock the entire TARGET tree if RECURSE is TRUE.
 * ADM_ACCESS may be NULL, in which case the DIFF_CMD callbacks will be
 * passed a NULL access baton.
 *
 * DIFF_CMD/DIFF_CMD_BATON represent the callback and calback argument that
 * implement the file comparison function
 *
 * RECURSE is set if the diff is to be recursive.
 *
 * DRY_RUN is set if this is a dry-run merge. It is not relevant for diff.
 *
 * RA_SESSION defines the additional RA session for requesting file
 * contents.
 *
 * REVISION is the start revision in the comparison.
 *
 * If NOTIFY_FUNC is non-null, invoke it with NOTIFY_BATON for each
 * file and directory operated on during the edit.
 *
 * EDITOR/EDIT_BATON return the newly created editor and baton/
 */
svn_error_t *
svn_client__get_diff_editor(const char *target,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_diff_callbacks2_t *diff_cmd,
                            void *diff_cmd_baton,
                            svn_boolean_t recurse,
                            svn_boolean_t dry_run,
                            svn_ra_session_t *ra_session, 
                            svn_revnum_t revision,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            const svn_delta_editor_t **editor,
                            void **edit_baton,
                            apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** Editor for diff summary ***/

/* Create an editor for a repository diff summary, i.e. comparing one
 * repository version against the other and only providing information
 * about the changed items without the text deltas.
 *
 * @a summarize_func is called with @a summarize_baton as parameter by the
 * created svn_delta_editor_t for each changed item.
 *
 * See svn_client__get_diff_editor() for a description of the other
 * parameters.
 */
svn_error_t *
svn_client__get_diff_summarize_editor(const char *target,
                                      svn_client_diff_summarize_func_t
                                      summarize_func,
                                      void *summarize_baton,
                                      svn_ra_session_t *ra_session, 
                                      svn_revnum_t revision,
                                      svn_cancel_func_t cancel_func,
                                      void *cancel_baton,
                                      const svn_delta_editor_t **editor,
                                      void **edit_baton,
                                      apr_pool_t *pool);

/* ---------------------------------------------------------------- */

/*** Commit Stuff ***/

/* WARNING: This is all new, untested, un-peer-reviewed conceptual
   stuff.

   The day that 'svn switch' came into existence, our old commit
   crawler (svn_wc_crawl_local_mods) became obsolete.  It relied far
   too heavily on the on-disk hierarchy of files and directories, and
   simply had no way to support disjoint working copy trees or nest
   working copies.  The primary reason for this is that commit
   process, in order to guarantee atomicity, is a single drive of a
   commit editor which is based not on working copy paths, but on
   URLs.  With the completion of 'svn switch', it became all too
   likely that the on-disk working copy hierarchy would no longer be
   guaranteed to map to a similar in-repository hierarchy.

   Aside from this new brokenness of the old system, an unrelated
   feature request had cropped up -- the ability to know in advance of
   your commit, exactly what would be committed (so that log messages
   could be initially populated with this information).  Since the old
   crawler discovered commit candidates while in the process of
   committing, it was impossible to harvest this information upfront.
   As a workaround, svn_wc_statuses() was used to stat the whole
   working copy for changes before the commit started...and then the
   commit would again stat the whole tree for changes.

   Enter the new system.

   The primary goal of this system is very straightforward: harvest
   all commit candidate information up front, and cache enough info in
   the process to use this to drive a URL-sorted commit.

   *** END-OF-KNOWLEDGE ***

   The prototypes below are still in development.  In general, the
   idea is that commit-y processes ('svn mkdir URL', 'svn delete URL',
   'svn commit', 'svn copy WC_PATH URL', 'svn copy URL1 URL2', 'svn
   move URL1 URL2', others?) generate the cached commit candidate
   information, and hand this information off to a consumer which is
   responsible for driving the RA layer's commit editor in a
   URL-depth-first fashion and reporting back the post-commit
   information.

*/



/* ### This is TEMPORARY! Until we can find out the canonical
   repository URL of a given entry, we'll just use this bogus value in
   for our single committables hash key.  By the time we support
   multiple repositories we will have to be storing the canonical
   repository URLs anyway, so this will go away and the real URLs will
   be the keys of the committables hash. */
#define SVN_CLIENT__SINGLE_REPOS_NAME "svn:single-repos"


/* Recursively crawl a set of working copy paths (PARENT_DIR + each
   item in the TARGETS array) looking for commit candidates, locking
   working copy directories as the crawl progresses.  For each
   candidate found:

     - create svn_client_commit_item_t for the candidate.

     - add the structure to an apr_array_header_t array of commit
       items that are in the same repository, creating a new array if
       necessary.

     - add (or update) a reference to this array to the COMMITTABLES
       hash, keyed on the canonical repository name.  ### todo, until
       multi-repository support actually exists, the single key here
       will actually be some arbitrary thing to be ignored.  

     - if the candidate has a lock token, add it to the LOCK_TOKENS hash.

     - if the candidate is a directory scheduled for deletion, crawl
       the directories children recursively for any lock tokens and
       add them to the LOCK_TOKENS array.

   At the successful return of this function, COMMITTABLES will be an
   apr_hash_t * hash of apr_array_header_t * arrays (of
   svn_client_commit_item_t * structures), keyed on const char *
   canonical repository URLs.  LOCK_TOKENS will point to a hash table
   with const char * lock tokens, keyed on const char * URLs.  Also,
   LOCKED_DIRS will be an apr_hash_t * hash of svn_wc_adm_access_t *
   keyed
   on const char * working copy path directory names which were locked
   in the process of this crawl.  These will need to be unlocked again post-commit.

   If NONRECURSIVE is specified, subdirectories of directory targets
   found in TARGETS will not be crawled for modifications.

   If JUST_LOCKED is TRUE, treat unmodified items with lock tokens as
   commit candidates.

   If CTX->CANCEL_FUNC is non-null, it will be called with 
   CTX->CANCEL_BATON while harvesting to determine if the client has 
   cancelled the operation.  */
svn_error_t *
svn_client__harvest_committables(apr_hash_t **committables,
                                 apr_hash_t **lock_tokens,
                                 svn_wc_adm_access_t *parent_dir,
                                 apr_array_header_t *targets,
                                 svn_boolean_t nonrecursive,
                                 svn_boolean_t just_locked,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);


/* Recursively crawl the working copy path TARGET, harvesting
   commit_items into a COMMITABLES hash (see the docstring for
   svn_client__harvest_committables for what that really means, and
   for the relevance of LOCKED_DIRS) as if every entry at or below
   TARGET was to be committed as a set of adds (mostly with history)
   to a new repository URL (NEW_URL).

   If CTX->CANCEL_FUNC is non-null, it will be called with 
   CTX->CANCEL_BATON while harvesting to determine if the client has 
   cancelled the operation.  */
svn_error_t *
svn_client__get_copy_committables(apr_hash_t **committables,
                                  const char *new_url,
                                  const char *target,
                                  svn_wc_adm_access_t *adm_access,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool);
               

/* A qsort()-compatible sort routine for sorting an array of
   svn_client_commit_item_t's by their URL member. */
int svn_client__sort_commit_item_urls(const void *a, const void *b);


/* Rewrite the COMMIT_ITEMS array to be sorted by URL.  Also, discover
   a common *BASE_URL for the items in the array, and rewrite those
   items' URLs to be relative to that *BASE_URL.  

   Afterwards, some of the items in COMMIT_ITEMS may contain data
   allocated in POOL. */
svn_error_t *
svn_client__condense_commit_items(const char **base_url,
                                  apr_array_header_t *commit_items,
                                  apr_pool_t *pool);


/* Commit the items in the COMMIT_ITEMS array using EDITOR/EDIT_BATON
   to describe the committed local mods.  Prior to this call,
   COMMIT_ITEMS should have been run through (and BASE_URL generated
   by) svn_client__condense_commit_items.

   CTX->NOTIFY_FUNC/CTX->BATON will be called as the commit progresses, as 
   a way of describing actions to the application layer (if non NULL).

   NOTIFY_PATH_PREFIX is used to send shorter, relative paths to the
   notify_func (it's a prefix that will be subtracted from the front
   of the paths.)

   If the caller wants to keep track of any outstanding temporary
   files left after the transmission of text and property mods,
   *TEMPFILES is the place to look.

   MD5 checksums, if available,  for the new text bases of committed
   files are stored in *DIGESTS, which maps const char* paths (from the
   items' paths) to const unsigned char* digests.  DIGESTS may be
   null.  */
svn_error_t *
svn_client__do_commit(const char *base_url,
                      apr_array_header_t *commit_items,
                      svn_wc_adm_access_t *adm_access,
                      const svn_delta_editor_t *editor,
                      void *edit_baton,
                      const char *notify_path_prefix,
                      apr_hash_t **tempfiles,
                      apr_hash_t **digests,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);



/*** Externals (Modules) ***/

/* Handle changes to the svn:externals property in the tree traversed
   by TRAVERSAL_INFO (obtained from svn_wc_get_checkout_editor,
   svn_wc_get_update_editor, svn_wc_get_switch_editor, for example).

   For each changed value of the property, discover the nature of the
   change and behave appropriately -- either check a new "external"
   subdir, or call svn_wc_remove_from_revision_control() on an
   existing one, or both.

   Pass NOTIFY_FUNC with NOTIFY_BATON along to svn_client_checkout().

   ### todo: AUTH_BATON may not be so useful.  It's almost like we
       need access to the original auth-obtaining callbacks that
       produced auth baton in the first place.  Hmmm. ###

   If UPDATE_UNCHANGED, then run svn_client_update() on any external
   items that are the same in both the before and after traversal
   info.

   *TIMESTAMP_SLEEP will be set TRUE if a sleep is required to ensure
   timestamp integrity, *TIMESTAMP_SLEEP will be unchanged if no sleep
   is required.

   Use POOL for temporary allocation. */
svn_error_t *
svn_client__handle_externals(svn_wc_traversal_info_t *traversal_info,
                             svn_boolean_t update_unchanged,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);


/* Fetch externals definitions described by EXTERNALS, a hash of the
   form returned by svn_wc_edited_externals() (which see).  If
   IS_EXPORT is set, the external items will be exported instead of
   checked out -- they will have no administrative subdirectories.

   *TIMESTAMP_SLEEP will be set TRUE if a sleep is required to ensure
   timestamp integrity, *TIMESTAMP_SLEEP will be unchanged if no sleep
   is required.

   Use POOL for temporary allocation. */
svn_error_t *
svn_client__fetch_externals(apr_hash_t *externals,
                            svn_boolean_t is_export,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);


/* Perform status operations on each external in TRAVERSAL_INFO.  All
   other options are the same as those passed to svn_client_status(). */
svn_error_t *
svn_client__do_external_status(svn_wc_traversal_info_t *traversal_info,
                               svn_wc_status_func2_t status_func,
                               void *status_baton,
                               svn_boolean_t get_all,
                               svn_boolean_t update,
                               svn_boolean_t no_ignore,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool);



/* Retrieves log message using *CTX->log_msg_func or
 * *CTX->log_msg_func2 callbacks.
 * Other arguments same as svn_client_get_commit_log2_t. */
svn_error_t * svn_client__get_log_msg(const char **log_msg,
                                      const char **tmp_file,
                                      const apr_array_header_t *commit_items,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_CLIENT_H */
