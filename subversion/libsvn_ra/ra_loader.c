/*
 * ra_loader.c:  logic for loading different RA library implementations
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

/* ==================================================================== */

/*** Includes. ***/

#include <string.h>
#include <apr.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_dso.h>

#include "svn_version.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "ra_loader.h"
#include "svn_private_config.h"


/* ### this file maps URL schemes to particular RA libraries. This is not
   ### entirely correct, as a single scheme could potentially be served
   ### by more than one loader. However, we can ignore that until we
   ### actually run into a conflict within the scheme portion of a URL. */


/* These are the URI schemes that the respective libraries *may* support.
 * The schemes actually supported may be a subset of the schemes listed below.
 * This can't be determine until the library is loaded.
 * (Currently, this applies to the https scheme of ra_dav, which is only
 * available if SSL is supported.) */
static const char * const dav_schemes[] = { "http", "https", NULL };
static const char * const svn_schemes[] = { "svn", NULL };
static const char * const local_schemes[] = { "file", NULL };

static const struct ra_lib_defn {
  /* the name of this RA library (e.g. "dav" or "local") */
  const char *ra_name;

  const char * const *schemes;
  /* the initialization function if linked in; otherwise, NULL */
  svn_ra__init_func_t initfunc;
  svn_ra_init_func_t compat_initfunc;
} ra_libraries[] = {
  {
    "dav",
    dav_schemes,
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_DAV
    svn_ra_dav__init,
    svn_ra_dav_init
#endif
  },

  {
    "svn",
    svn_schemes,
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_SVN
    svn_ra_svn__init,
    svn_ra_svn_init
#endif
  },

  {
    "local",
    local_schemes,
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL
    svn_ra_local__init,
    svn_ra_local_init
#endif
  },

  /* ADD NEW RA IMPLEMENTATIONS HERE (as they're written) */

  /* sentinel */
  { NULL }
};

/* Ensure that the RA library NAME is loaded.
 * If FUNC is non-NULL, set *FUNC to the address of the svn_ra_NAME__init
 * function of the library.
 * If COMPAT_FUNC is non-NULL, set *COMPAT_FUNC to the address of the
 * svn_ra_NAME_init compatibility init function of the library. */
static svn_error_t *
load_ra_module (svn_ra__init_func_t *func,
                svn_ra_init_func_t *compat_func,
                const char *ra_name, apr_pool_t *pool)
{
  if (func)
    *func = NULL;
  if (compat_func)
    *compat_func = NULL;

#if APR_HAS_DSO
  {
    apr_dso_handle_t *dso;
    apr_dso_handle_sym_t symbol;
    const char *libname;
    const char *funcname;
    const char *compat_funcname;
    apr_status_t status;

    libname = apr_psprintf (pool, "libsvn_ra_%s-%d.so.0",
                            ra_name, SVN_VER_MAJOR);
    funcname = apr_psprintf (pool, "svn_ra_%s__init", ra_name);
    compat_funcname = apr_psprintf (pool, "svn_ra_%s_init", ra_name);

    /* find/load the specified library */
    status = apr_dso_load (&dso, libname, pool);
    if (status)
      {
#if 0
        char errbuf[200];
        apr_dso_error(dso, errbuf, sizeof(errbuf));

        fprintf(stderr, "DSO error: %s\n", errbuf);
#endif

        /* Just ignore the error. Assume the library isn't present */
        return SVN_NO_ERROR;
      }
    /* note: the library will be unloaded at pool cleanup */

    /* find the initialization routines */
    if (func)
      {
        status = apr_dso_sym (&symbol, dso, funcname);
        if (status)
          {
            return svn_error_wrap_apr (status,
                                       _("'%s' does not define '%s()'"),
                                       libname, funcname);
          }

        *func = (svn_ra__init_func_t) symbol;
      }

    if (compat_func)
      {
        status = apr_dso_sym (&symbol, dso, compat_funcname);
        if (status)
          {
            return svn_error_wrap_apr (status,
                                       _("'%s' does not define '%s()'"),
                                       libname, compat_funcname);
          }

        *compat_func = (svn_ra_init_func_t) symbol;
      }
  }
#endif /* APR_HAS_DSO */

  return SVN_NO_ERROR;
}

/* If DEFN may support URL, return the scheme.  Else, return NULL. */
static const char *
has_scheme_of (const struct ra_lib_defn *defn, const char *url)
{
  const char * const *schemes;
  apr_size_t len;

  for (schemes = defn->schemes; *schemes != NULL; ++schemes)
    {
      len = strlen (*schemes);
      /* Case-insensitive comparison, per RFC 2396 section 3.1.  Allow
         URL to contain a trailing "+foo" section in the scheme, since
         that's how we specify tunnel schemes in ra_svn. */
      if (strncasecmp (*schemes, url, len) == 0 &&
          (url[len] == ':' || url[len] == '+'))
        return *schemes;
    }

  return NULL;
}


/* -------------------------------------------------------------- */

/*** Public Interfaces ***/
svn_error_t *svn_ra_open (svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const svn_ra_callbacks_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  svn_ra_session_t *session;
  const struct ra_lib_defn *defn;
  const svn_ra__vtable_t *vtable = NULL;

  /* Find the library. */
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      const char *scheme;

      if ((scheme = has_scheme_of (defn, repos_URL)))
        {
          const svn_version_t *my_version = svn_ra_version();
          const svn_version_t *ra_version;
          svn_ra__init_func_t initfunc;

          initfunc = defn->initfunc;
          if (! initfunc)
            SVN_ERR (load_ra_module (&initfunc, NULL, defn->ra_name,
                                     pool));
          if (! initfunc)
            /* Library not found. */
            break;

          SVN_ERR (initfunc (my_version, &vtable));

          ra_version = vtable->get_version();
          if (!svn_ver_equal (my_version, ra_version))
            return svn_error_createf (SVN_ERR_VERSION_MISMATCH, NULL,
                                      _("Mismatched RA version"
                                        " for '%s':"
                                        " found %d.%d.%d%s,"
                                        " expected %d.%d.%d%s"),
                                      scheme,
                                      my_version->major, my_version->minor,
                                      my_version->patch, my_version->tag,
                                      ra_version->major, ra_version->minor,
                                      ra_version->patch, ra_version->tag);
        }
    }
    
  if (vtable == NULL)
    return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, NULL,
                              _("Unrecognized URL scheme for '%s'"),
                              repos_URL);

  /* Create the session object. */
  session = apr_pcalloc (pool, sizeof (*session));
  session->vtable = vtable;
  session->pool = pool;

  /* Ask the library to open the session. */
  SVN_ERR (vtable->open (session, repos_URL, callbacks, callback_baton,
                         config, pool));

  *session_p = session;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_latest_revnum (svn_ra_session_t *session,
                                       svn_revnum_t *latest_revnum,
                                       apr_pool_t *pool)
{
  return session->vtable->get_latest_revnum (session, latest_revnum, pool);
}

svn_error_t *svn_ra_get_dated_revision (svn_ra_session_t *session,
                                        svn_revnum_t *revision,
                                        apr_time_t tm,
                                        apr_pool_t *pool)
{
  return session->vtable->get_dated_revision (session, revision, tm, pool);
}

svn_error_t *svn_ra_change_rev_prop (svn_ra_session_t *session,
                                     svn_revnum_t rev,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool)
{
  return session->vtable->change_rev_prop (session, rev, name, value, pool);
}

svn_error_t *svn_ra_rev_proplist (svn_ra_session_t *session,
                                  svn_revnum_t rev,
                                  apr_hash_t **props,
                                  apr_pool_t *pool)
{
  return session->vtable->rev_proplist (session, rev, props, pool);
}

svn_error_t *svn_ra_rev_prop (svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              svn_string_t **value,
                              apr_pool_t *pool)
{
  return session->vtable->rev_prop (session, rev, name, value, pool);
}

svn_error_t *svn_ra_get_commit_editor (svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *log_msg,
                                       svn_commit_callback_t callback,
                                       void *callback_baton,
                                       apr_pool_t *pool)
{
  return session->vtable->get_commit_editor (session, editor, edit_baton,
                                             log_msg, callback, callback_baton,
                                             pool);
}

svn_error_t *svn_ra_get_file (svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t revision,
                              svn_stream_t *stream,
                              svn_revnum_t *fetched_rev,
                              apr_hash_t **props,
                              apr_pool_t *pool)
{
  return session->vtable->get_file (session, path, revision, stream,
                                    fetched_rev, props, pool);
}

svn_error_t *svn_ra_get_dir (svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t revision,
                             apr_hash_t **dirents,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             apr_pool_t *pool)
{
  return session->vtable->get_dir (session, path, revision, dirents,
                                   fetched_rev, props, pool);
}

svn_error_t *svn_ra_do_update (svn_ra_session_t *session,
                               const svn_ra_reporter_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_update_to,
                               const char *update_target,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *update_editor,
                               void *update_baton,
                               apr_pool_t *pool)
{
  return session->vtable->do_update (session, reporter, report_baton,
                                     revision_to_update_to, update_target,
                                     recurse, update_editor, update_baton,
                                     pool);
}

svn_error_t *svn_ra_do_switch (svn_ra_session_t *session,
                               const svn_ra_reporter_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_switch_to,
                               const char *switch_target,
                               svn_boolean_t recurse,
                               const char *switch_url,
                               const svn_delta_editor_t *switch_editor,
                               void *switch_baton,
                               apr_pool_t *pool)
{
  return session->vtable->do_switch (session, reporter, report_baton,
                                     revision_to_switch_to, switch_target,
                                     recurse, switch_url, switch_editor,
                                     switch_baton, pool);
}

svn_error_t *svn_ra_do_status (svn_ra_session_t *session,
                               const svn_ra_reporter_t **reporter,
                               void **report_baton,
                               const char *status_target,
                               svn_revnum_t revision,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *status_editor,
                               void *status_baton,
                               apr_pool_t *pool)
{
  return session->vtable->do_status (session, reporter, report_baton,
                                     status_target, revision, recurse,
                                     status_editor, status_baton, pool);
}

svn_error_t *svn_ra_do_diff (svn_ra_session_t *session,
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
  return session->vtable->do_diff (session, reporter, report_baton, revision,
                                   diff_target, recurse, ignore_ancestry,
                                   versus_url, diff_editor, diff_baton, pool);
}

svn_error_t *svn_ra_get_log (svn_ra_session_t *session,
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
  return session->vtable->get_log (session, paths, start, end, limit,
                                   discover_changed_paths, strict_node_history,
                                   receiver, receiver_baton, pool);
}

svn_error_t *svn_ra_check_path (svn_ra_session_t *session,
                                const char *path,
                                svn_revnum_t revision,
                                svn_node_kind_t *kind,
                                apr_pool_t *pool)
{
  return session->vtable->check_path (session, path, revision, kind, pool);
}

svn_error_t *svn_ra_get_uuid (svn_ra_session_t *session,
                              const char **uuid,
                              apr_pool_t *pool)
{
  return session->vtable->get_uuid (session, uuid, pool);
}

svn_error_t *svn_ra_get_repos_root (svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool)
{
  return session->vtable->get_repos_root (session, url, pool);
}

svn_error_t *svn_ra_get_locations (svn_ra_session_t *session,
                                   apr_hash_t **locations,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   apr_array_header_t *location_revisions,
                                   apr_pool_t *pool)
{
  return session->vtable->get_locations (session, locations, path,
                                         peg_revision, location_revisions,
                                         pool);
}

svn_error_t *svn_ra_get_file_revs (svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t start,
                                   svn_revnum_t end,
                                   svn_ra_file_rev_handler_t handler,
                                   void *handler_baton,
                                   apr_pool_t *pool)
{
  return session->vtable->get_file_revs (session, path, start, end, handler,
                                         handler_baton, pool);
}




svn_error_t *
svn_ra_print_ra_libraries (svn_stringbuf_t **descriptions,
                           void *ra_baton,
                           apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  const char * const *schemes;
  svn_ra__init_func_t initfunc;
  const svn_ra__vtable_t *vtable;
  apr_pool_t *iterpool = svn_pool_create (pool);

  *descriptions = svn_stringbuf_create ("", pool);
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      char *line;

      svn_pool_clear (iterpool);

      initfunc = defn->initfunc;
      if (! initfunc)
        SVN_ERR (load_ra_module (&initfunc, NULL, defn->ra_name,
                                 iterpool));

      if (initfunc)
        {
          SVN_ERR (initfunc (svn_ra_version(), &vtable));
          line = apr_psprintf (iterpool, "* ra_%s : %s\n",
                               defn->ra_name,
                               vtable->get_description());
          svn_stringbuf_appendcstr (*descriptions, line);

          for (schemes = vtable->get_schemes(iterpool); *schemes != NULL;
               ++schemes)
            {
              line = apr_psprintf (iterpool, _("  - handles '%s' schema\n"),
                                   *schemes);
              svn_stringbuf_appendcstr (*descriptions, line);
            }
        }
    }

  svn_pool_destroy (iterpool);

  return SVN_NO_ERROR;
}


/* Return the library version number. */
const svn_version_t *
svn_ra_version (void)
{
  SVN_VERSION_BODY;
}


/*** Compatibility Interfaces **/
svn_error_t *
svn_ra_init_ra_libs (void **ra_baton,
                     apr_pool_t *pool)
{
  *ra_baton = pool;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_get_ra_library (svn_ra_plugin_t **library,
                       void *ra_baton,
                       const char *url,
                       apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  apr_pool_t *load_pool = ra_baton;
  apr_hash_t *ht = apr_hash_make (pool);

  /* Figure out which RA library key matches URL. */
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      const char *scheme;
      if ((scheme = has_scheme_of (defn, url)))
        {
          const svn_version_t *my_version = svn_ra_version();
          const svn_version_t *ra_version;
          svn_ra_init_func_t initfunc;

          initfunc = defn->compat_initfunc;
          if (! initfunc)
            SVN_ERR (load_ra_module (NULL, &initfunc, defn->ra_name,
                                     load_pool));
          if (! initfunc)
            break;

          SVN_ERR (initfunc (SVN_RA_ABI_VERSION, load_pool, ht));

          *library = apr_hash_get (ht, scheme, APR_HASH_KEY_STRING);

          /* The library may support just a subset of the schemes listed,
             so we have to check here too. */
          if (! *library)
            break;

          ra_version = (*library)->get_version();
          if (!svn_ver_equal (my_version, ra_version))
            return svn_error_createf (SVN_ERR_VERSION_MISMATCH, NULL,
                                      _("Mismatched RA plugin version"
                                        " for '%s':"
                                        " found %d.%d.%d%s,"
                                        " expected %d.%d.%d%s"),
                                      scheme,
                                      my_version->major, my_version->minor,
                                      my_version->patch, my_version->tag,
                                      ra_version->major, ra_version->minor,
                                      ra_version->patch, ra_version->tag);
          return SVN_NO_ERROR;
        }
    }
    
  /* Couldn't find a match... */
  *library = NULL;
  return svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, NULL,
                            _("Unrecognized URL scheme '%s'"), url);
}

