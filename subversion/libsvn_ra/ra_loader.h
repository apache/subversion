/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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
 * @file ra_loader.h
 * @brief structures related to repository access, private to libsvn_ra and the
 * RA implementation libraries.
 */



#ifndef LIBSVN_RA_RA_LOADER_H
#define LIBSVN_RA_RA_LOADER_H

#include "svn_ra.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The RA layer vtable. */
typedef struct svn_ra__vtable_t {
  /* This field should always remain first in the vtable. */
  const svn_version_t *(*get_version) (void);

  /* Return a short description of the RA implementation, as a localized
   * string. */
  const char *(*get_description) (void);

  /* Return a list of actual URI schemes supported by this implementation.
   * The returned array is NULL terminated. */
  const char * const *(*get_schemes)(apr_pool_t *pool);

  /* Implementations of the public API functions. */

  /* All fields in SESSION, except priv, are valid.  SESSION->priv
   * may be set by this function. */
  svn_error_t *(*open) (svn_ra_session_t *session,
                        const char *repos_URL,
                        const svn_ra_callbacks_t *callbacks,
                        void *callback_baton,
                        apr_hash_t *config,
                        apr_pool_t *pool);
  svn_error_t *(*get_latest_revnum) (svn_ra_session_t *session,
                                     svn_revnum_t *latest_revnum,
                                     apr_pool_t *pool);
  svn_error_t *(*get_dated_revision) (svn_ra_session_t *session,
                                      svn_revnum_t *revision,
                                      apr_time_t tm,
                                      apr_pool_t *pool);
  svn_error_t *(*change_rev_prop) (svn_ra_session_t *session,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);

  svn_error_t *(*rev_proplist) (svn_ra_session_t *session,
                                svn_revnum_t rev,
                                apr_hash_t **props,
                                apr_pool_t *pool);
  svn_error_t *(*rev_prop) (svn_ra_session_t *session,
                            svn_revnum_t rev,
                            const char *name,
                            svn_string_t **value,
                            apr_pool_t *pool);
  svn_error_t *(*get_commit_editor) (svn_ra_session_t *session,
                                     const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     const char *log_msg,
                                     svn_commit_callback_t callback,
                                     void *callback_baton,
                                     apr_pool_t *pool);
  svn_error_t *(*get_file) (svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t revision,
                            svn_stream_t *stream,
                            svn_revnum_t *fetched_rev,
                            apr_hash_t **props,
                            apr_pool_t *pool);
  svn_error_t *(*get_dir) (svn_ra_session_t *session,
                           const char *path,
                           svn_revnum_t revision,
                           apr_hash_t **dirents,
                           svn_revnum_t *fetched_rev,
                           apr_hash_t **props,
                           apr_pool_t *pool);
  svn_error_t *(*do_update) (svn_ra_session_t *session,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             const char *update_target,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *update_editor,
                             void *update_baton,
                             apr_pool_t *pool);
  svn_error_t *(*do_switch) (svn_ra_session_t *session,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_switch_to,
                             const char *switch_target,
                             svn_boolean_t recurse,
                             const char *switch_url,
                             const svn_delta_editor_t *switch_editor,
                             void *switch_baton,
                             apr_pool_t *pool);
  svn_error_t *(*do_status) (svn_ra_session_t *session,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             const char *status_target,
                             svn_revnum_t revision,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *status_editor,
                             void *status_baton,
                             apr_pool_t *pool);
  svn_error_t *(*do_diff) (svn_ra_session_t *session,
                           const svn_ra_reporter_t **reporter,
                           void **report_baton,
                           svn_revnum_t revision,
                           const char *diff_target,
                           svn_boolean_t recurse,
                           svn_boolean_t ignore_ancestry,
                           const char *versus_url,
                           const svn_delta_editor_t *diff_editor,
                           void *diff_baton,
                           apr_pool_t *pool);
  svn_error_t *(*get_log) (svn_ra_session_t *session,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           int limit,
                           svn_boolean_t discover_changed_paths,
                           svn_boolean_t strict_node_history,
                           svn_log_message_receiver_t receiver,
                           void *receiver_baton,
                           apr_pool_t *pool);
  svn_error_t *(*check_path) (svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t revision,
                              svn_node_kind_t *kind,
                              apr_pool_t *pool);
  svn_error_t *(*get_uuid) (svn_ra_session_t *session,
                            const char **uuid,
                            apr_pool_t *pool);
  svn_error_t *(*get_repos_root) (svn_ra_session_t *session,
                                  const char **url,
                                  apr_pool_t *pool);
  svn_error_t *(*get_locations) (svn_ra_session_t *session,
                                 apr_hash_t **locations,
                                 const char *path,
                                 svn_revnum_t peg_revision,
                                 apr_array_header_t *location_revisions,
                                 apr_pool_t *pool);
  svn_error_t *(*get_file_revs) (svn_ra_session_t *session,
                                 const char *path,
                                 svn_revnum_t start,
                                 svn_revnum_t end,
                                 svn_ra_file_rev_handler_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool);
} svn_ra__vtable_t;

/* The RA session object. */
struct svn_ra_session_t {
  const svn_ra__vtable_t *vtable;

  /* Pool used to manage this session. */
  apr_pool_t *pool;

  /* Private data for the RA implementation. */
  void *priv;
};

/* Each libsvn_ra_foo defines a function named svn_ra_foo__init of this type.
 *
 * The LOADER_VERSION parameter must remain first in the list, and the
 * function must use the C calling convention on all platforms, so that
 * the init functions can safely read the version parameter.
 *
 * ### need to force this to be __cdecl on Windows... how??
 */
typedef svn_error_t
*(*svn_ra__init_func_t)(const svn_version_t *loader_version,
                        const svn_ra__vtable_t **vtable);

/* Declarations of the init functions for the available RA libraries. */
svn_error_t *svn_ra_local__init(const svn_version_t *loader_version,
                                const svn_ra__vtable_t **vtable);
svn_error_t *svn_ra_svn__init(const svn_version_t *loader_version,
                                const svn_ra__vtable_t **vtable);
svn_error_t *svn_ra_dav__init(const svn_version_t *loader_version,
                                const svn_ra__vtable_t **vtable);

/* Macro to create a compatibility wrapper for a RA library.
 * It creates an svn_ra_plugin_t named compat_plugin that implements the plugin
 * API in terms of the svn_ra__vtable_t VTBL.  NAME and DESCRIPTION
 * are the two first fields of the plugin struct.
 * NOTE: This has to be a macro, so that each RA library can duplicate this.
 * An RA library can't use symbols from libsvn_ra, since that would introduce
 * circular dependencies. */

#ifdef __cplusplus
}
#endif

#endif
