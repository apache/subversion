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
 * @file svn_ra.h
 * @brief structures related to repository access, private to libsvn_ra and the
 * RA implementation libraries.
 */

/* This file is a template for compatibility wrapper for an RA library.
 * It contains an svn_ra_plugin_t and wrappers for all of its functions
 * implemented in terms of svn_ra__vtable_t functions.  It also contains
 * the omplementations of an svn_ra_FOO_init for the FOO RA library.
 *
 * A file in the RA library includes this files providing the following macros
 * before inclusion:
 * NAME             The library name, i.e. "ra_local".
 * DESCRIPTION      The short library description as a string constant.
 * VTBL             The name of an svn_ra_vtable_t object for the library.
 * INITFUNC         The init function for the library, i.e. svn_ra_local__init.
 * COMPAT_INITFUNC  The compatibility init function, i.e. svn_ra_local_init.
 */

/* Check that all our "arguments" are defined. */
#if ! defined(NAME) || ! defined(DESCRIPTION) || ! defined(VTBL) \
    || ! defined(INITFUNC) || ! defined(COMPAT_INITFUNC)
#error Missing define for RA compatibility wrapper.
#endif

static svn_error_t *compat_open (void **session_baton,
                                 const char *repos_URL,
                                 const svn_ra_callbacks_t *callbacks,
                                 void *callback_baton,
                                 apr_hash_t *config,
                                 apr_pool_t *pool)
{
  svn_ra_session_t *sess = apr_pcalloc (pool, sizeof (svn_ra_session_t));
  sess->vtable = &VTBL;
  sess->pool = pool;
  SVN_ERR (VTBL.open (sess, repos_URL, callbacks, callback_baton,
                         config, pool));
  *session_baton = sess;
  return SVN_NO_ERROR;
}

static svn_error_t *compat_get_latest_revnum (void *session_baton,
                                              svn_revnum_t *latest_revnum,
                                              apr_pool_t *pool)
{
  return VTBL.get_latest_revnum (session_baton, latest_revnum, pool);
}

static svn_error_t *compat_get_dated_revision (void *session_baton,
                                               svn_revnum_t *revision,
                                               apr_time_t tm,
                                               apr_pool_t *pool)
{
  return VTBL.get_dated_revision (session_baton, revision, tm, pool);
}

static svn_error_t *compat_change_rev_prop (void *session_baton,
                                            svn_revnum_t rev,
                                            const char *propname,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
  return VTBL.change_rev_prop (session_baton, rev, propname, value, pool);
}

static svn_error_t *compat_rev_proplist (void *session_baton,
                                         svn_revnum_t rev,
                                         apr_hash_t **props,
                                         apr_pool_t *pool)
{
  return VTBL.rev_proplist (session_baton, rev, props, pool);
}

static svn_error_t *compat_rev_prop (void *session_baton,
                                     svn_revnum_t rev,
                                     const char *propname,
                                     svn_string_t **value,
                                     apr_pool_t *pool)
{
  return VTBL.rev_prop (session_baton, rev, propname, value, pool);
}

static svn_error_t *compat_get_commit_editor (void *session_baton,
                                              const svn_delta_editor_t
                                              **editor,
                                              void **edit_baton,
                                              const char *log_msg,
                                              svn_commit_callback_t callback,
                                              void *callback_baton,
                                              apr_pool_t *pool)
{
  return VTBL.get_commit_editor (session_baton, editor, edit_baton, log_msg,
                                 callback, callback_baton, pool);
}

static svn_error_t *compat_get_file (void *session_baton,
                                     const char *path,
                                     svn_revnum_t revision,
                                     svn_stream_t *stream,
                                     svn_revnum_t *fetched_rev,
                                     apr_hash_t **props,
                                     apr_pool_t *pool)
{
  return VTBL.get_file (session_baton, path, revision, stream, fetched_rev,
                        props, pool);
}

static svn_error_t *compat_get_dir (void *session_baton,
                                    const char *path,
                                    svn_revnum_t revision,
                                    apr_hash_t **dirents,
                                    svn_revnum_t *fetched_rev,
                                    apr_hash_t **props,
                                    apr_pool_t *pool)
{
  return VTBL.get_dir (session_baton, path, revision, dirents, fetched_rev,
                       props, pool);
}

static svn_error_t *compat_do_update (void *session_baton,
                                      const svn_ra_reporter_t **reporter,
                                      void **report_baton,
                                      svn_revnum_t revision_to_update_to,
                                      const char *update_target,
                                      svn_boolean_t recurse,
                                      const svn_delta_editor_t *editor,
                                      void *update_baton,
                                      apr_pool_t *pool)
{
  return VTBL.do_update (session_baton, reporter, report_baton,
                         revision_to_update_to, update_target, recurse,
                         editor, update_baton, pool);
}

static svn_error_t *compat_do_switch (void *session_baton,
                                      const svn_ra_reporter_t **reporter,
                                      void **report_baton,
                                      svn_revnum_t revision_to_switch_to,
                                      const char *switch_target,
                                      svn_boolean_t recurse,
                                      const char *switch_url,
                                      const svn_delta_editor_t *editor,
                                      void *switch_baton,
                                      apr_pool_t *pool)
{
  return VTBL.do_switch (session_baton, reporter, report_baton,
                         revision_to_switch_to, switch_target, recurse,
                         switch_url, editor, switch_baton, pool);
}

static svn_error_t *compat_do_status (void *session_baton,
                                      const svn_ra_reporter_t **reporter,
                                      void **report_baton,
                                      const char *status_target,
                                      svn_revnum_t revision,
                                      svn_boolean_t recurse,
                                      const svn_delta_editor_t *editor,
                                      void *status_baton,
                                      apr_pool_t *pool)
{
  return VTBL.do_status (session_baton, reporter, report_baton,
                         status_target,
                       revision, recurse, editor, status_baton, pool);
}

static svn_error_t *compat_do_diff (void *session_baton,
                                    const svn_ra_reporter_t **reporter,
                                    void **report_baton,
                                    svn_revnum_t revision,
                                    const char *diff_target,
                                    svn_boolean_t recurse,
                                    svn_boolean_t ignore_ancestry,
                                    const char *versus_url,
                                    const svn_delta_editor_t *diff_editor,
                                    void *diff_baton,
                                    apr_pool_t *pool)
{
  return VTBL.do_diff (session_baton, reporter, report_baton, revision,
                       diff_target, recurse, ignore_ancestry, versus_url,
                       diff_editor, diff_baton, pool);
}

static svn_error_t *compat_get_log (void *session_baton,
                                    const apr_array_header_t *paths,
                                    svn_revnum_t start,
                                    svn_revnum_t end,
                                    svn_boolean_t discover_changed_paths,
                                    svn_boolean_t strict_node_history,
                                    svn_log_message_receiver_t receiver,
                                    void *receiver_baton,
                                    apr_pool_t *pool)
{
  return VTBL.get_log (session_baton, paths, start, end, 0, /* limit */
                       discover_changed_paths, strict_node_history,
                       receiver, receiver_baton, pool);
}

static svn_error_t *compat_check_path (void *session_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_node_kind_t *kind,
                                       apr_pool_t *pool)
{
  return VTBL.check_path (session_baton, path, revision, kind, pool);
}

static svn_error_t *compat_get_uuid (void *session_baton,
                                     const char **uuid,
                                     apr_pool_t *pool)
{
  return VTBL.get_uuid (session_baton, uuid, pool);
}

static svn_error_t *compat_get_repos_root (void *session_baton,
                                           const char **url,
                                           apr_pool_t *pool)
{
  return VTBL.get_repos_root (session_baton, url, pool);
}

static svn_error_t *compat_get_locations (void *session_baton,
                                          apr_hash_t **locations,
                                          const char *path,
                                          svn_revnum_t peg_revision,
                                          apr_array_header_t *location_revs,
                                          apr_pool_t *pool)
{
  return VTBL.get_locations (session_baton, locations, path, peg_revision,
                             location_revs, pool);
}

static svn_error_t *compat_get_file_revs (void *session_baton,
                                          const char *path,
                                          svn_revnum_t start,
                                          svn_revnum_t end,
                                          svn_ra_file_rev_handler_t handler,
                                          void *handler_baton,
                                          apr_pool_t *pool)
{
  return VTBL.get_file_revs (session_baton, path, start, end, handler,
                             handler_baton, pool);
}

static const svn_version_t *compat_get_version (void)
{
  return VTBL.get_version ();
}

static const svn_ra_plugin_t compat_plugin = {
  NAME,
  DESCRIPTION,
  compat_open,
  compat_get_latest_revnum,
  compat_get_dated_revision,
  compat_change_rev_prop,
  compat_rev_proplist,
  compat_rev_prop,
  compat_get_commit_editor,
  compat_get_file,
  compat_get_dir,
  compat_do_update,
  compat_do_switch,
  compat_do_status,
  compat_do_diff,
  compat_get_log,
  compat_check_path,
  compat_get_uuid,
  compat_get_repos_root,
  compat_get_locations,
  compat_get_file_revs,
  compat_get_version,
};

svn_error_t *
COMPAT_INITFUNC (int abi_version,
                 apr_pool_t *pool,
                 apr_hash_t *hash)
{
  const svn_ra__vtable_t *vtable;
  const char * const * schemes;

  if (abi_version < 1
      || abi_version > SVN_RA_ABI_VERSION)
    return svn_error_createf (SVN_ERR_RA_UNSUPPORTED_ABI_VERSION, NULL,
                              _("Unsupported RA plugin ABI version (%d) "
                                "for %s"), abi_version, NAME);

  /* We call the new init function so it can check library dependencies or
     do other initialization things.  We fake the loader version, since we
     rely on the ABI version check instead. */
  SVN_ERR (INITFUNC (VTBL.get_version(), &vtable));

  /* Sanity check. */
  assert (&VTBL == vtable);

  schemes = VTBL.get_schemes (pool);

  for (; *schemes != NULL; ++schemes)
    apr_hash_set (hash, *schemes, APR_HASH_KEY_STRING, &compat_plugin);

  return SVN_NO_ERROR;
}
