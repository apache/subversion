/*
 * serf.c :  entry point for ra_serf
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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



#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_private_config.h"


static const svn_version_t *
ra_serf_version (void)
{
  SVN_VERSION_BODY;
}

#define RA_SERF_DESCRIPTION \
    N_("Access repository via WebDAV protocol through serf.")

static const char *
ra_serf_get_description (void)
{
    return _(RA_SERF_DESCRIPTION);
}

static const char * const *
ra_serf_get_schemes (apr_pool_t *pool)
{
    static const char *serf_ssl[] = { "http", "https", NULL };
    static const char *serf_no_ssl[] = { "http", NULL };

    /* TODO: Runtime detection. */
    return serf_ssl;
}

static svn_error_t *
svn_ra_serf__open (svn_ra_session_t *session,
                   const char *repos_URL,
                   const svn_ra_callbacks2_t *callbacks,
                   void *callback_baton,
                   apr_hash_t *config,
                   apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__reparent (svn_ra_session_t *session,
                       const char *url,
                       apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_latest_revnum (svn_ra_session_t *session,
                                svn_revnum_t *latest_revnum,
                                apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_dated_revision (svn_ra_session_t *session,
                                 svn_revnum_t *revision,
                                 apr_time_t tm,
                                 apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__change_rev_prop (svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              const svn_string_t *value,
                              apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__rev_proplist (svn_ra_session_t *session,
                           svn_revnum_t rev,
                           apr_hash_t **props,
                           apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__rev_prop (svn_ra_session_t *session,
                       svn_revnum_t rev,
                       const char *name,
                       svn_string_t **value,
                       apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_commit_editor (svn_ra_session_t *session,
                                const svn_delta_editor_t **editor,
                                void **edit_baton,
                                const char *log_msg,
                                svn_commit_callback2_t callback,
                                void *callback_baton,
                                apr_hash_t *lock_tokens,
                                svn_boolean_t keep_locks,
                                apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_file (svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t revision,
                       svn_stream_t *stream,
                       svn_revnum_t *fetched_rev,
                       apr_hash_t **props,
                       apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_dir (svn_ra_session_t *session,
                      const char *path,
                      svn_revnum_t revision,
                      apr_uint32_t dirent_fields,
                      apr_hash_t **dirents,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__do_update (svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_update_to,
                        const char *update_target,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *update_editor,
                        void *update_baton,
                        apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__do_switch (svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        svn_revnum_t revision_to_switch_to,
                        const char *switch_target,
                        svn_boolean_t recurse,
                        const char *switch_url,
                        const svn_delta_editor_t *switch_editor,
                        void *switch_baton,
                        apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__do_status (svn_ra_session_t *session,
                        const svn_ra_reporter2_t **reporter,
                        void **report_baton,
                        const char *status_target,
                        svn_revnum_t revision,
                        svn_boolean_t recurse,
                        const svn_delta_editor_t *status_editor,
                        void *status_baton,
                        apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__do_diff (svn_ra_session_t *session,
                      const svn_ra_reporter2_t **reporter,
                      void **report_baton,
                      svn_revnum_t revision,
                      const char *diff_target,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t text_deltas,
                      const char *versus_url,
                      const svn_delta_editor_t *diff_editor,
                      void *diff_baton,
                      apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_log (svn_ra_session_t *session,
                      const apr_array_header_t *paths,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      int limit,
                      svn_boolean_t discover_changed_paths,
                      svn_boolean_t strict_node_history,
                      svn_log_message_receiver_t receiver,
                      void *receiver_baton,
                      apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__check_path (svn_ra_session_t *session,
                         const char *path,
                         svn_revnum_t revision,
                         svn_node_kind_t *kind,
                         apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__stat (svn_ra_session_t *session,
                   const char *path,
                   svn_revnum_t revision,
                   svn_dirent_t **dirent,
                   apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_uuid (svn_ra_session_t *session,
                       const char **uuid,
                       apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_repos_root (svn_ra_session_t *session,
                             const char **url,
                             apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_locations (svn_ra_session_t *session,
                            apr_hash_t **locations,
                            const char *path,
                            svn_revnum_t peg_revision,
                            apr_array_header_t *location_revisions,
                            apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_file_revs (svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_ra_file_rev_handler_t handler,
                            void *handler_baton,
                            apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__lock (svn_ra_session_t *session,
                   apr_hash_t *path_revs,
                   const char *comment,
                   svn_boolean_t force,
                   svn_ra_lock_callback_t lock_func,
                   void *lock_baton,
                   apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__unlock (svn_ra_session_t *session,
                     apr_hash_t *path_tokens,
                     svn_boolean_t force,
                     svn_ra_lock_callback_t lock_func,
                     void *lock_baton,
                     apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_lock (svn_ra_session_t *session,
                       svn_lock_t **lock,
                       const char *path,
                       apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__get_locks (svn_ra_session_t *session,
                        apr_hash_t **locks,
                        const char *path,
                        apr_pool_t *pool)
{
    return NULL;
}

static svn_error_t *
svn_ra_serf__replay (svn_ra_session_t *session,
                     svn_revnum_t revision,
                     svn_revnum_t low_water_mark,
                     svn_boolean_t text_deltas,
                     const svn_delta_editor_t *editor,
                     void *edit_baton,
                     apr_pool_t *pool)
{
    return NULL;
}

static const svn_ra__vtable_t serf_vtable = {
  ra_serf_version,
  ra_serf_get_description,
  ra_serf_get_schemes,
  svn_ra_serf__open,
  svn_ra_serf__reparent,
  svn_ra_serf__get_latest_revnum,
  svn_ra_serf__get_dated_revision,
  svn_ra_serf__change_rev_prop,
  svn_ra_serf__rev_proplist,
  svn_ra_serf__rev_prop,
  svn_ra_serf__get_commit_editor,
  svn_ra_serf__get_file,
  svn_ra_serf__get_dir,
  svn_ra_serf__do_update,
  svn_ra_serf__do_switch,
  svn_ra_serf__do_status,
  svn_ra_serf__do_diff,
  svn_ra_serf__get_log,
  svn_ra_serf__check_path,
  svn_ra_serf__stat,
  svn_ra_serf__get_uuid,
  svn_ra_serf__get_repos_root,
  svn_ra_serf__get_locations,
  svn_ra_serf__get_file_revs,
  svn_ra_serf__lock,
  svn_ra_serf__unlock,
  svn_ra_serf__get_lock,
  svn_ra_serf__get_locks,
  svn_ra_serf__replay,
};

svn_error_t *
svn_ra_serf__init (const svn_version_t *loader_version,
                   const svn_ra__vtable_t **vtable,
                   apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };

  SVN_ERR(svn_ver_check_list(ra_serf_version(), checklist));

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    {
      return svn_error_createf
        (SVN_ERR_VERSION_MISMATCH, NULL,
         _("Unsupported RA loader version (%d) for ra_serf"),
         loader_version->major);
    }

  *vtable = &serf_vtable;

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for pre-1.2 subversions.  Needed? */
#define NAME "ra_serf"
#define DESCRIPTION RA_SERF_DESCRIPTION
#define VTBL serf_vtable
#define INITFUNC svn_ra_serf__init
#define COMPAT_INITFUNC svn_ra_serf_init
#include "../libsvn_ra/wrapper_template.h"
