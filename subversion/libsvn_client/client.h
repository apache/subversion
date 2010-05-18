/*
 * client.h :  shared stuff internal to the client library.
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


#ifndef SVN_LIBSVN_CLIENT_H
#define SVN_LIBSVN_CLIENT_H


#include <apr_pools.h>

#include "svn_types.h"
#include "svn_opt.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"
#include "svn_client.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Set *URL and *PEG_REVNUM (the latter is ignored if NULL) to the
   repository URL of PATH_OR_URL.  If PATH_OR_URL is a WC path and
   PEG_REVISION->kind is svn_opt_revision_working, use the
   corresponding entry's copyfrom info.  RA_SESSION and ADM_ACCESS may
   be NULL, regardless of whether PATH_OR_URL is a URL.  Use CTX for
   cancellation (ignored if NULL), and POOL for all allocations. */
svn_error_t *
svn_client__derive_location(const char **url,
                            svn_revnum_t *peg_revnum,
                            const char *path_or_url,
                            const svn_opt_revision_t *peg_revision,
                            svn_ra_session_t *ra_session,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* Get the repository URL and revision number for WC entry ENTRY,
   which is sometimes the entry's copyfrom info rather than its actual
   URL and revision. */
svn_error_t *
svn_client__entry_location(const char **url,
                           svn_revnum_t *revnum,
                           const char *path_or_url,
                           enum svn_opt_revision_kind peg_rev_kind,
                           const svn_wc_entry_t *entry,
                           apr_pool_t *pool);

/* Set *REVNUM to the revision number identified by REVISION.

   If REVISION->kind is svn_opt_revision_number, just use
   REVISION->value.number, ignoring PATH and RA_SESSION.

   Else if REVISION->kind is svn_opt_revision_committed,
   svn_opt_revision_previous, or svn_opt_revision_base, or
   svn_opt_revision_working, then the revision can be identified
   purely based on the working copy's administrative information for
   PATH, so RA_SESSION is ignored.  If PATH is not under revision
   control, return SVN_ERR_UNVERSIONED_RESOURCE, or if PATH is null,
   return SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED.

   Else if REVISION->kind is svn_opt_revision_date or
   svn_opt_revision_head, then RA_SESSION is used to retrieve the
   revision from the repository (using REVISION->value.date in the
   former case), and PATH is ignored.  If RA_SESSION is null,
   return SVN_ERR_CLIENT_RA_ACCESS_REQUIRED.

   Else if REVISION->kind is svn_opt_revision_unspecified, set
   *REVNUM to SVN_INVALID_REVNUM.

   If YOUNGEST_REV is non-NULL, it is an in/out parameter.  If
   *YOUNGEST_REV is valid, use it as the youngest revision in the
   repository (regardless of reality) -- don't bother to lookup the
   true value for HEAD, and don't return any value in *REVNUM greater
   than *YOUNGEST_REV.  If *YOUNGEST_REV is not valid, and a HEAD
   lookup is required to populate *REVNUM, then also populate
   *YOUNGEST_REV with the result.  This is useful for making multiple
   serialized calls to this function with a basically static view of
   the repository, avoiding race conditions which could occur between
   multiple invocations with HEAD lookup requests.

   Else return SVN_ERR_CLIENT_BAD_REVISION.

   Use POOL for any temporary allocation.  */
svn_error_t *
svn_client__get_revision_number(svn_revnum_t *revnum,
                                svn_revnum_t *youngest_rev,
                                svn_ra_session_t *ra_session,
                                const svn_opt_revision_t *revision,
                                const char *path,
                                apr_pool_t *pool);

/* Set *COPYFROM_PATH and *COPYFROM_REV to the path (without initial '/')
   and revision that served as the source of the copy from which PATH_OR_URL
   at REVISION was created, or NULL and SVN_INVALID_REVNUM (respectively) if
   PATH_OR_URL at REVISION was not the result of a copy operation. */
svn_error_t *svn_client__get_copy_source(const char *path_or_url,
                                         const svn_opt_revision_t *revision,
                                         const char **copyfrom_path,
                                         svn_revnum_t *copyfrom_rev,
                                         svn_client_ctx_t *ctx,
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


/* Set *SEGMENTS to an array of svn_location_segment_t * objects, each
   representing a reposition location segment for the history of PATH
   (which is relative to RA_SESSION's session URL) in PEG_REVISION
   between END_REVISION and START_REVISION, ordered from oldest
   segment to youngest.  *SEGMENTS may be empty but it will never
   be NULL.

   This is basically a thin de-stream-ifying wrapper around the
   svn_ra_get_location_segments() interface, which see for the rules
   governing PEG_REVISION, START_REVISION, and END_REVISION.

   CTX is the client context baton.

   Use POOL for all allocations.  */
svn_error_t *
svn_client__repos_location_segments(apr_array_header_t **segments,
                                    svn_ra_session_t *ra_session,
                                    const char *path,
                                    svn_revnum_t peg_revision,
                                    svn_revnum_t start_revision,
                                    svn_revnum_t end_revision,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *pool);


/* Set *ANCESTOR_PATH and *ANCESTOR_REVISION to the youngest common
   ancestor path (a path relative to the root of the repository) and
   revision, respectively, of the two locations identified as
   PATH_OR_URL1@REV1 and PATH_OR_URL2@REV1.  Use the authentication
   baton cached in CTX to authenticate against the repository.
   This function assumes that PATH_OR_URL1@REV1 and PATH_OR_URL2@REV1
   both refer to the same repository.  Use POOL for all allocations. */
svn_error_t *
svn_client__get_youngest_common_ancestor(const char **ancestor_path,
                                         svn_revnum_t *ancestor_revision,
                                         const char *path_or_url1,
                                         svn_revnum_t rev1,
                                         const char *path_or_url2,
                                         svn_revnum_t rev2,
                                         svn_client_ctx_t *ctx,
                                         apr_pool_t *pool);

/* Given PATH_OR_URL, which contains either a working copy path or an
   absolute URL, a peg revision PEG_REVISION, and a desired revision
   REVISION, create an RA connection to that object as it exists in
   that revision, following copy history if necessary.  If REVISION is
   younger than PEG_REVISION, then PATH_OR_URL will be checked to see
   that it is the same node in both PEG_REVISION and REVISION.  If it
   is not, then @c SVN_ERR_CLIENT_UNRELATED_RESOURCES is returned.

   BASE_ACCESS is the working copy the ra_session corresponds to, should
   only be used if PATH_OR_URL is a url.

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
                                 svn_wc_adm_access_t *base_access,
                                 const svn_opt_revision_t *peg_revision,
                                 const svn_opt_revision_t *revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);

/* Set *REL_PATH to a relative path which, when URI-encoded and joined
   with RA_SESSION's session url, will result in a string that matches URL. */
svn_error_t *
svn_client__path_relative_to_session(const char **rel_path,
                                     svn_ra_session_t *ra_session,
                                     const char *url,
                                     apr_pool_t *pool);

/* Ensure that RA_SESSION's session URL matches SESSION_URL,
   reparenting that session if necessary.  If reparenting occurs,
   store the previous session URL in *OLD_SESSION_URL (so that if the
   reparenting is meant to be temporary, the caller can reparent the
   session back to where it was); otherwise set *OLD_SESSION_URL to
   NULL.

   If SESSION_URL is NULL, treat this as a magic value meaning "point
   the RA session to the root of the repository".  */
svn_error_t *
svn_client__ensure_ra_session_url(const char **old_session_url,
                                  svn_ra_session_t *ra_session,
                                  const char *session_url,
                                  apr_pool_t *pool);

/* Set REPOS_ROOT to the URL which represents the root of the
   repository in with PATH_OR_URL (at PEG_REVISION) is versioned.  Use
   the authentication baton cached in CTX as necessary.

   ADM_ACCESS is a working copy administrative access baton associated
   with PATH_OR_URL (if PATH_OR_URL is a working copy path), or NULL.

   Use POOL for all allocations. */
svn_error_t *
svn_client__get_repos_root(const char **repos_root,
                           const char *path_or_url,
                           const svn_opt_revision_t *peg_revision,
                           svn_wc_adm_access_t *adm_access,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool);

/* Return the path of PATH_OR_URL relative to the repository root
   (REPOS_ROOT) in REL_PATH (URI-decoded).  If INCLUDE_LEADING_SLASH
   is set, the returned result will have a leading slash; otherwise,
   it will not.

   The remaining parameters are used to procure the repository root.
   Either REPOS_ROOT or RA_SESSION -- but not both -- may be NULL.
   REPOS_ROOT or ADM_ACCESS (which may also be NULL) should be passed
   when available as an optimization (in that order of preference).

   CAUTION:  While having a leading slash on a so-called relative path
   might work out well for functionality that interacts with
   mergeinfo, it results in a relative path that cannot be naively
   svn_path_join()'d with a repository root URL to provide a full URL.
*/
svn_error_t *
svn_client__path_relative_to_root(const char **rel_path,
                                  const char *path_or_url,
                                  const char *repos_root,
                                  svn_boolean_t include_leading_slash,
                                  svn_ra_session_t *ra_session,
                                  svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool);

/* Return the property value for any PROPNAME set on TARGET in *PROPS,
   with WC paths of char * for keys and property values of
   svn_string_t * for values.  Assumes that PROPS is non-NULL.

   CHANGELISTS is an array of const char * changelist names, used as a
   restrictive filter on items whose properties are set; that is,
   don't set properties on any item unless it's a member of one of
   those changelists.  If CHANGELISTS is empty (or altogether NULL),
   no changelist filtering occurs.

   Treat DEPTH as in svn_client_propget3().
*/
svn_error_t *
svn_client__get_prop_from_wc(apr_hash_t *props,
                             const char *propname,
                             const char *target,
                             svn_boolean_t pristine,
                             const svn_wc_entry_t *entry,
                             svn_wc_adm_access_t *adm_access,
                             svn_depth_t depth,
                             const apr_array_header_t *changelists,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);

/* Retrieve the oldest revision of the node at REL_PATH at REV since
   it was last copied (if applicable), and store it in OLDEST_REV.  If
   REL_PATH does not exist in that REV, set *OLDEST_REV to
   SVN_INVALID_REVNUM.  Use POOL for temporary allocations. */
svn_error_t *
svn_client__oldest_rev_at_path(svn_revnum_t *oldest_rev,
                               svn_ra_session_t *ra_session,
                               const char *rel_path,
                               svn_revnum_t rev,
                               apr_pool_t *pool);

/* A default error handler for use with svn_wc_walk_entries3().  Returns
   ERR in all cases. */
svn_error_t *
svn_client__default_walker_error_handler(const char *path,
                                         svn_error_t *err,
                                         void *walk_baton,
                                         apr_pool_t *pool);


/* ---------------------------------------------------------------- */

/*** RA callbacks ***/


/* CTX is of type "svn_client_ctx_t *". */
#define SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx) \
        ((ctx)->log_msg_func3 || (ctx)->log_msg_func2 || (ctx)->log_msg_func)

/* This is the baton that we pass svn_ra_open3(), and is associated with
   the callback table we provide to RA. */
typedef struct
{
  /* Holds the directory that corresponds to the REPOS_URL at svn_ra_open3()
     time. When callbacks specify a relative path, they are joined with
     this base directory. */
  const char *base_dir;
  svn_wc_adm_access_t *base_access;

  /* When true, makes sure temporary files are created
     outside the working copy. */
  svn_boolean_t read_only_wc;

  /* An array of svn_client_commit_item3_t * structures, present only
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
   modified the delete will fail and return an error unless FORCE or KEEP_LOCAL
   is TRUE.

   If KEEP_LOCAL is TRUE then PATH is only scheduled from deletion from the
   repository and a local copy of PATH will be kept in the working copy.

   If DRY_RUN is TRUE all the checks are made to ensure that the delete can
   occur, but the working copy is not modified.  If NOTIFY_FUNC is not
   null, it is called with NOTIFY_BATON for each file or directory deleted. */
svn_error_t * svn_client__wc_delete(const char *path,
                                    svn_wc_adm_access_t *adm_access,
                                    svn_boolean_t force,
                                    svn_boolean_t dry_run,
                                    svn_boolean_t keep_local,
                                    svn_wc_notify_func2_t notify_func,
                                    void *notify_baton,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *pool);

/* Return the set of WC paths to entries which would have been deleted
   by an update/merge if not in "dry run" mode, or NULL if not in "dry
   run" mode.  MERGE_CMD_BATON is expected to be of type "struct
   merge_cmd_baton" (from merge.c).  It contains the list, which is
   intended for direct modification. */
apr_hash_t *svn_client__dry_run_deletions(void *merge_cmd_baton);

/* Make PATH and add it to the working copy, optionally making all the
   intermediate parent directories if MAKE_PARENTS is TRUE. */
svn_error_t *
svn_client__make_local_parents(const char *path,
                               svn_boolean_t make_parents,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool);

/* ---------------------------------------------------------------- */

/*** Checkout, update and switch ***/

/* Update a working copy PATH to REVISION, and (if not NULL) set
   RESULT_REV to the update revision.

   If DEPTH is svn_depth_unknown, then use whatever depth is already
   set for PATH, or @c svn_depth_infinity if PATH does not exist.

   Else if DEPTH is svn_depth_infinity, then update fully recursively
   (resetting the existing depth of the working copy if necessary).
   Else if DEPTH is svn_depth_files, update all files under PATH (if
   any), but exclude any subdirectories.  Else if DEPTH is
   svn_depth_immediates, update all files and include immediate
   subdirectories (at svn_depth_empty).  Else if DEPTH is
   svn_depth_empty, just update PATH; if PATH is a directory, that
   means touching only its properties not its entries.

   If DEPTH_IS_STICKY is set and DEPTH is not svn_depth_unknown, then
   in addition to updating PATH, also set its sticky ambient depth
   value to DEPTH.

   If IGNORE_EXTERNALS is true, do no externals processing.

   If TIMESTAMP_SLEEP is NULL this function will sleep before
   returning to ensure timestamp integrity.  If TIMESTAMP_SLEEP is not
   NULL then the function will not sleep but will set *TIMESTAMP_SLEEP
   to TRUE if a sleep is required, and will not change
   *TIMESTAMP_SLEEP if no sleep is required.

   If ALLOW_UNVER_OBSTRUCTIONS is TRUE, unversioned children of PATH
   that obstruct items added from the repos are tolerated; if FALSE,
   these obstructions cause the update to fail.

   If SEND_COPYFROM_ARGS is true, then request that the server not
   send file contents when adding files that have been created by
   explicit copying; instead, just send copyfrom-args to add_file(),
   and possibly follow up with an apply_textdelta() against the copied
   file.
*/
svn_error_t *
svn_client__update_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t *timestamp_sleep,
                            svn_boolean_t send_copyfrom_args,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* Structure holding the results of svn_client__ra_session_from_path()
   plus the repository root URL and UUID and the node kind for the
   input URL, REVISION and PEG_REVISION .  See
   svn_client__ra_session_from_path() for the meaning of these fields.
   This structure is used by svn_client__checkout_internal() to save
   one or more round-trips if the client already gathered some of this
   information.  Not all the fields need to be filled in.  */
typedef struct
{
  /* The repository root URL.  A NULL value means the root URL is
     unknown.*/
  const char *repos_root_url;

  /* The repository UUID.  A NULL value means the UUID is unknown.  */
  const char *repos_uuid;

  /* The actual final resulting URL for the input URL.  This may be
     different because of copy history.  A NULL value means the
     resulting URL is unknown.  */
  const char *ra_session_url;

  /* The actual final resulting revision for the input URL.  An
     invalid revnum as determined by SVN_IS_VALID_REVNUM() means the
     revnum is unknown.  */
  svn_revnum_t ra_revnum;

  /* An optional node kind for the URL.  Since there is no enum value
     for an unknown node kind, it is represented as a pointer to a
     svn_node_kind_t with a NULL pointer indicating an unknown
     value. */
  svn_node_kind_t *kind_p;
} svn_client__ra_session_from_path_results;

/* Checkout into PATH a working copy of URL at REVISION, and (if not
   NULL) set RESULT_REV to the checked out revision.

   If DEPTH is svn_depth_infinity, then check out fully recursively.
   Else if DEPTH is svn_depth_files, checkout all files under PATH (if
   any), but not subdirectories.  Else if DEPTH is
   svn_depth_immediates, check out all files and include immediate
   subdirectories (at svn_depth_empty).  Else if DEPTH is
   svn_depth_empty, just check out PATH, with none of its entries.

   DEPTH must be a definite depth, not (e.g.) svn_depth_unknown.

   RA_CACHE is a pointer to a cache of information for the URL at
   REVISION based of the PEG_REVISION.  Any information not in
   *RA_CACHE is retrieved by a round-trip to the repository.  RA_CACHE
   may be NULL which indicates that no cache information is available.

   If IGNORE_EXTERNALS is true, do no externals processing.

   If TIMESTAMP_SLEEP is NULL this function will sleep before
   returning to ensure timestamp integrity.  If TIMESTAMP_SLEEP is not
   NULL then the function will not sleep but will set *TIMESTAMP_SLEEP
   to TRUE if a sleep is required, and will not change
   *TIMESTAMP_SLEEP if no sleep is required.  If
   ALLOW_UNVER_OBSTRUCTIONS is TRUE, unversioned children of PATH that
   obstruct items added from the repos are tolerated; if FALSE, these
   obstructions cause the checkout to fail. */
svn_error_t *
svn_client__checkout_internal(svn_revnum_t *result_rev,
                              const char *URL,
                              const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *revision,
                              const svn_client__ra_session_from_path_results *ra_cache,
                              svn_depth_t depth,
                              svn_boolean_t ignore_externals,
                              svn_boolean_t allow_unver_obstructions,
                              svn_boolean_t *timestamp_sleep,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool);

/* Switch a working copy PATH to URL@PEG_REVISION at REVISION, and (if not
   NULL) set RESULT_REV to the switch revision.  ADM_ACCESS may be NULL, but
   if is not, it is a write locked working copy administrative access baton
   that has an associated baton for PATH.  Only switch as deeply as DEPTH
   indicates.  If TIMESTAMP_SLEEP is NULL this function will sleep before
   returning to ensure timestamp integrity.  If TIMESTAMP_SLEEP is not
   NULL then the function will not sleep but will set *TIMESTAMP_SLEEP
   to TRUE if a sleep is required, and will not change
   *TIMESTAMP_SLEEP if no sleep is required.  If IGNORE_EXTERNALS is true,
   don't process externals.  If ALLOW_UNVER_OBSTRUCTIONS is TRUE, unversioned
   children of PATH that obstruct items added from the repos are tolerated;
   if FALSE, these obstructions cause the switch to fail.

   DEPTH and DEPTH_IS_STICKY behave as for svn_client__update_internal(). */
svn_error_t *
svn_client__switch_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const char *url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_wc_adm_access_t *adm_access,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t *timestamp_sleep,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* ---------------------------------------------------------------- */

/*** Editor for repository diff ***/

/* Create an editor for a pure repository comparison, i.e. comparing one
   repository version against the other.

   TARGET is a working-copy path, the base of the hierarchy to be
   compared.  It corresponds to the URL opened in RA_SESSION below.

   ADM_ACCESS is an access baton with a write lock for the anchor of
   TARGET.  It should lock the entire TARGET tree if RECURSE is TRUE.
   ADM_ACCESS may be NULL, in which case the DIFF_CMD callbacks will be
   passed a NULL access baton.

   DIFF_CMD/DIFF_CMD_BATON represent the callback and callback argument that
   implement the file comparison function

   DEPTH is the depth to recurse.

   DRY_RUN is set if this is a dry-run merge. It is not relevant for diff.

   RA_SESSION defines the additional RA session for requesting file
   contents.

   REVISION is the start revision in the comparison.

   If NOTIFY_FUNC is non-null, invoke it with NOTIFY_BATON for each
   file and directory operated on during the edit.

   EDITOR/EDIT_BATON return the newly created editor and baton/  */
svn_error_t *
svn_client__get_diff_editor(const char *target,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_diff_callbacks3_t *diff_cmd,
                            void *diff_cmd_baton,
                            svn_depth_t depth,
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
   repository version against the other and only providing information
   about the changed items without the text deltas.

   TARGET is the target of the diff, relative to the root of the edit.

   SUMMARIZE_FUNC is called with SUMMARIZE_BATON as parameter by the
   created svn_delta_editor_t for each changed item.

   See svn_client__get_diff_editor() for a description of the other
   parameters.  */
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

/*** Copy Stuff ***/

/* This structure is used to associate a specific copy or move SRC with a
   specific copy or move destination.  It also contains information which
   various helper functions may need.  Not every copy function uses every
   field.
*/
typedef struct
{
    /* The source path or url. */
    const char *src;

    /* The absolute path of the source. */
    const char *src_abs;

    /* The base name of the object.  It should be the same for both src
       and dst. */
    const char *base_name;

    /* The node kind of the source */
    svn_node_kind_t src_kind;

    /* The original source name.  (Used when the source gets overwritten by a
       peg revision lookup.) */
    const char *src_original;

    /* The source operational revision. */
    svn_opt_revision_t src_op_revision;

    /* The source peg revision. */
    svn_opt_revision_t src_peg_revision;

    /* The source revision number. */
    svn_revnum_t src_revnum;

    /* The destination path or url */
    const char *dst;

    /* The destination's parent path */
    const char *dst_parent;
} svn_client__copy_pair_t;

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
   keyed on const char * working copy path directory names which were
   locked in the process of this crawl.  These will need to be
   unlocked again post-commit.

   If DEPTH is specified, descend (or not) into each target in TARGETS
   as specified by DEPTH; the behavior is the same as that described
   for svn_client_commit4().

   If JUST_LOCKED is TRUE, treat unmodified items with lock tokens as
   commit candidates.

   If CHANGELISTS is non-NULL, it is an array of const char *
   changelist names used as a restrictive filter
   when harvesting committables; that is, don't add a path to
   COMMITTABLES unless it's a member of one of those changelists.

   If CTX->CANCEL_FUNC is non-null, it will be called with
   CTX->CANCEL_BATON while harvesting to determine if the client has
   cancelled the operation.  */
svn_error_t *
svn_client__harvest_committables(apr_hash_t **committables,
                                 apr_hash_t **lock_tokens,
                                 svn_wc_adm_access_t *parent_dir,
                                 apr_array_header_t *targets,
                                 svn_depth_t depth,
                                 svn_boolean_t just_locked,
                                 const apr_array_header_t *changelists,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);


/* Recursively crawl each working copy path SRC in COPY_PAIRS, harvesting
   commit_items into a COMMITABLES hash (see the docstring for
   svn_client__harvest_committables for what that really means, and
   for the relevance of LOCKED_DIRS) as if every entry at or below
   the SRC was to be committed as a set of adds (mostly with history)
   to a new repository URL (DST in COPY_PAIRS).

   If CTX->CANCEL_FUNC is non-null, it will be called with
   CTX->CANCEL_BATON while harvesting to determine if the client has
   cancelled the operation.  */
svn_error_t *
svn_client__get_copy_committables(apr_hash_t **committables,
                                  const apr_array_header_t *copy_pairs,
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

   NOTIFY_PATH_PREFIX will be passed to CTX->notify_func2() as the
   common absolute path prefix of the committed paths.  It can be NULL.

   If the caller wants to keep track of any outstanding temporary
   files left after the transmission of text and property mods,
   *TEMPFILES is the place to look.

   MD5 checksums, if available,  for the new text bases of committed
   files are stored in *CHECKSUMS, which maps const char* paths (from the
   items' paths) to const svn_checksum_t * digests.  CHECKSUMS may be
   null.  */
svn_error_t *
svn_client__do_commit(const char *base_url,
                      apr_array_header_t *commit_items,
                      svn_wc_adm_access_t *adm_access,
                      const svn_delta_editor_t *editor,
                      void *edit_baton,
                      const char *notify_path_prefix,
                      apr_hash_t **tempfiles,
                      apr_hash_t **checksums,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);



/*** Externals (Modules) ***/

/* Handle changes to the svn:externals property in the tree traversed
   by TRAVERSAL_INFO (obtained from svn_wc_get_update_editor or
   svn_wc_get_switch_editor, for example).  The tree's top level
   directory is at TO_PATH and should have a write lock in ADM_ACCESS
   and corresponds to FROM_URL URL in the repository, which has a root
   URL of REPOS_ROOT_URL.

   For each changed value of the property, discover the nature of the
   change and behave appropriately -- either check a new "external"
   subdir, or call svn_wc_remove_from_revision_control() on an
   existing one, or both.

   REQUESTED_DEPTH is the requested depth of the driving operation
   (e.g., update, switch, etc).  If it is neither svn_depth_infinity
   nor svn_depth_unknown, then changes to svn:externals will have no
   effect.  If REQUESTED_DEPTH is svn_depth_unknown, then the ambient
   depth of each working copy directory holding an svn:externals value
   will determine whether that value is interpreted there (the ambient
   depth must be svn_depth_infinity).  If REQUESTED_DEPTH is
   svn_depth_infinity, then it is presumed to be expanding any
   shallower ambient depth, so changes to svn:externals values will be
   interpreted.

   Pass NOTIFY_FUNC with NOTIFY_BATON along to svn_client_checkout().

   ### todo: AUTH_BATON may not be so useful.  It's almost like we
       need access to the original auth-obtaining callbacks that
       produced auth baton in the first place.  Hmmm. ###

   *TIMESTAMP_SLEEP will be set TRUE if a sleep is required to ensure
   timestamp integrity, *TIMESTAMP_SLEEP will be unchanged if no sleep
   is required.

   Use POOL for temporary allocation. */
svn_error_t *
svn_client__handle_externals(svn_wc_adm_access_t *adm_access,
                             svn_wc_traversal_info_t *traversal_info,
                             const char *from_url,
                             const char *to_path,
                             const char *repos_root_url,
                             svn_depth_t requested_depth,
                             svn_boolean_t *timestamp_sleep,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);


/* Fetch externals definitions described by EXTERNALS, a hash of the
   form returned by svn_wc_edited_externals() (which see).  If
   IS_EXPORT is set, the external items will be exported instead of
   checked out -- they will have no administrative subdirectories.

   The checked out or exported tree's top level directory is at
   TO_PATH and corresponds to FROM_URL URL in the repository, which
   has a root URL of REPOS_ROOT_URL.

   REQUESTED_DEPTH is the requested_depth of the driving operation; it
   behaves as for svn_client__handle_externals(), except that ambient
   depths are presumed to be svn_depth_infinity.

   *TIMESTAMP_SLEEP will be set TRUE if a sleep is required to ensure
   timestamp integrity, *TIMESTAMP_SLEEP will be unchanged if no sleep
   is required.

   Use POOL for temporary allocation. */
svn_error_t *
svn_client__fetch_externals(apr_hash_t *externals,
                            const char *from_url,
                            const char *to_path,
                            const char *repos_root_url,
                            svn_depth_t requested_depth,
                            svn_boolean_t is_export,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);


/* Perform status operations on each external in TRAVERSAL_INFO.  All
   other options are the same as those passed to svn_client_status(). */
svn_error_t *
svn_client__do_external_status(svn_wc_traversal_info_t *traversal_info,
                               svn_wc_status_func3_t status_func,
                               void *status_baton,
                               svn_depth_t depth,
                               svn_boolean_t get_all,
                               svn_boolean_t update,
                               svn_boolean_t no_ignore,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool);



/* Retrieve log messages using the first provided (non-NULL) callback
   in the set of *CTX->log_msg_func3, CTX->log_msg_func2, or
   CTX->log_msg_func.  Other arguments same as
   svn_client_get_commit_log3_t. */
svn_error_t *
svn_client__get_log_msg(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool);

/* Return the revision properties stored in REVPROP_TABLE_IN, adding
   LOG_MSG as SVN_PROP_REVISION_LOG in *REVPROP_TABLE_OUT, allocated in
   POOL.  *REVPROP_TABLE_OUT will map const char * property names to
   svn_string_t values.  If REVPROP_TABLE_IN is non-NULL, check that
   it doesn't contain any of the standard Subversion properties.  In
   that case, return SVN_ERR_CLIENT_PROPERTY_NAME. */
svn_error_t *
svn_client__ensure_revprop_table(apr_hash_t **revprop_table_out,
                                 const apr_hash_t *revprop_table_in,
                                 const char *log_msg,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);


/* Return true if KIND is a revision kind that is dependent on the working
 * copy. Otherwise, return false. */
#define SVN_CLIENT__REVKIND_NEEDS_WC(kind)                                 \
  ((kind) == svn_opt_revision_base ||                                      \
   (kind) == svn_opt_revision_previous ||                                  \
   (kind) == svn_opt_revision_working ||                                   \
   (kind) == svn_opt_revision_committed)                                   \

/* Return true if KIND is a revision kind that the WC can supply without
 * contacting the repository. Otherwise, return false. */
#define SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(kind)                           \
  ((kind) == svn_opt_revision_base ||                                      \
   (kind) == svn_opt_revision_working ||                                   \
   (kind) == svn_opt_revision_committed)

/* Return REVISION unless its kind is 'unspecified' in which case return
 * a pointer to a statically allocated revision structure of kind 'head'
 * if PATH_OR_URL is a URL or 'base' if it is a WC path. */
const svn_opt_revision_t *
svn_cl__rev_default_to_head_or_base(const svn_opt_revision_t *revision,
                                    const char *path_or_url);

/* Return REVISION unless its kind is 'unspecified' in which case return
 * a pointer to a statically allocated revision structure of kind 'head'
 * if PATH_OR_URL is a URL or 'working' if it is a WC path. */
const svn_opt_revision_t *
svn_cl__rev_default_to_head_or_working(const svn_opt_revision_t *revision,
                                       const char *path_or_url);

/* Return REVISION unless its kind is 'unspecified' in which case return
 * PEG_REVISION. */
const svn_opt_revision_t *
svn_cl__rev_default_to_peg(const svn_opt_revision_t *revision,
                           const svn_opt_revision_t *peg_revision);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_CLIENT_H */
