/*
 * ra_loader.c:  logic for loading different RA library implementations
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

/* ==================================================================== */

/*** Includes. ***/
#include <assert.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_uri.h>

#include "svn_compat.h"
#include "svn_version.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_dso.h"
#include "svn_config.h"
#include "ra_loader.h"

#include "private/svn_ra_private.h"
#include "svn_private_config.h"


/* ### This file maps URL schemes to particular RA libraries.
   ### Currently, the only pair of RA libraries which support the same
   ### protocols are neon and serf.  svn_ra_open3 makes the assumption
   ### that this is the case; that their 'schemes' fields are both
   ### dav_schemes; and that "neon" is listed first.

   ### Users can choose which dav library to use with the http-library
   ### preference in .subversion/servers; however, it is ignored by
   ### any code which uses the pre-1.2 API svn_ra_get_ra_library
   ### instead of svn_ra_open. */

#if defined(SVN_HAVE_NEON) && defined(SVN_HAVE_SERF)
#define MUST_CHOOSE_DAV
#endif


/* These are the URI schemes that the respective libraries *may* support.
 * The schemes actually supported may be a subset of the schemes listed below.
 * This can't be determine until the library is loaded.
 * (Currently, this applies to the https scheme, which is only
 * available if SSL is supported.) */
static const char * const dav_schemes[] = { "http", "https", NULL };
static const char * const svn_schemes[] = { "svn", NULL };
static const char * const local_schemes[] = { "file", NULL };

static const struct ra_lib_defn {
  /* the name of this RA library (e.g. "neon" or "local") */
  const char *ra_name;

  const char * const *schemes;
  /* the initialization function if linked in; otherwise, NULL */
  svn_ra__init_func_t initfunc;
  svn_ra_init_func_t compat_initfunc;
} ra_libraries[] = {
  {
    "neon",
    dav_schemes,
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_NEON
    svn_ra_neon__init,
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

  {
    "serf",
    dav_schemes,
#ifdef SVN_LIBSVN_CLIENT_LINKS_RA_SERF
    svn_ra_serf__init,
    svn_ra_serf_init
#endif
  },

  /* ADD NEW RA IMPLEMENTATIONS HERE (as they're written) */

  /* sentinel */
  { NULL }
};

/* Ensure that the RA library NAME is loaded.
 *
 * If FUNC is non-NULL, set *FUNC to the address of the svn_ra_NAME__init
 * function of the library.
 *
 * If COMPAT_FUNC is non-NULL, set *COMPAT_FUNC to the address of the
 * svn_ra_NAME_init compatibility init function of the library.
 *
 * ### todo: Any RA libraries implemented from this point forward
 * ### don't really need an svn_ra_NAME_init compatibility function.
 * ### Currently, load_ra_module() will error if no such function is
 * ### found, but it might be more friendly to simply set *COMPAT_FUNC
 * ### to null (assuming COMPAT_FUNC itself is non-null).
 */
static svn_error_t *
load_ra_module(svn_ra__init_func_t *func,
               svn_ra_init_func_t *compat_func,
               const char *ra_name, apr_pool_t *pool)
{
  if (func)
    *func = NULL;
  if (compat_func)
    *compat_func = NULL;

#if defined(SVN_USE_DSO) && APR_HAS_DSO
  {
    apr_dso_handle_t *dso;
    apr_dso_handle_sym_t symbol;
    const char *libname;
    const char *funcname;
    const char *compat_funcname;
    apr_status_t status;

    libname = apr_psprintf(pool, "libsvn_ra_%s-%d.so.0",
                           ra_name, SVN_VER_MAJOR);
    funcname = apr_psprintf(pool, "svn_ra_%s__init", ra_name);
    compat_funcname = apr_psprintf(pool, "svn_ra_%s_init", ra_name);

    /* find/load the specified library */
    SVN_ERR(svn_dso_load(&dso, libname));
    if (! dso)
      return SVN_NO_ERROR;

    /* find the initialization routines */
    if (func)
      {
        status = apr_dso_sym(&symbol, dso, funcname);
        if (status)
          {
            return svn_error_wrap_apr(status,
                                      _("'%s' does not define '%s()'"),
                                      libname, funcname);
          }

        *func = (svn_ra__init_func_t) symbol;
      }

    if (compat_func)
      {
        status = apr_dso_sym(&symbol, dso, compat_funcname);
        if (status)
          {
            return svn_error_wrap_apr(status,
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
has_scheme_of(const struct ra_lib_defn *defn, const char *url)
{
  const char * const *schemes;
  apr_size_t len;

  for (schemes = defn->schemes; *schemes != NULL; ++schemes)
    {
      const char *scheme = *schemes;
      len = strlen(scheme);
      /* Case-insensitive comparison, per RFC 2396 section 3.1.  Allow
         URL to contain a trailing "+foo" section in the scheme, since
         that's how we specify tunnel schemes in ra_svn. */
      if (strncasecmp(scheme, url, len) == 0 &&
          (url[len] == ':' || url[len] == '+'))
        return scheme;
    }

  return NULL;
}

/* Return an error if RA_VERSION doesn't match the version of this library.
   Use SCHEME in the error message to describe the library that was loaded. */
static svn_error_t *
check_ra_version(const svn_version_t *ra_version, const char *scheme)
{
  const svn_version_t *my_version = svn_ra_version();
  if (!svn_ver_equal(my_version, ra_version))
    return svn_error_createf(SVN_ERR_VERSION_MISMATCH, NULL,
                             _("Mismatched RA version for '%s':"
                               " found %d.%d.%d%s,"
                               " expected %d.%d.%d%s"),
                             scheme,
                             my_version->major, my_version->minor,
                             my_version->patch, my_version->tag,
                             ra_version->major, ra_version->minor,
                             ra_version->patch, ra_version->tag);

  return SVN_NO_ERROR;
}

/* -------------------------------------------------------------- */

/*** Compatibility Wrappers ***/

/* Wrap @c svn_ra_reporter3_t in an interface that looks like
   @c svn_ra_reporter2_t, for compatibility with functions that take
   the latter.  This shields the ra-specific implementations from
   worrying about what kind of reporter they're dealing with.

   This code does not live in wrapper_template.h because that file is
   about the big changeover from a vtable-style to function-style
   interface, and does not contain the post-changeover interfaces
   that we are compatiblizing here.

   This code looks like it duplicates code in libsvn_wc/adm_crawler.c,
   but in fact it does not.  That code makes old things look like new
   things; this code makes a new thing look like an old thing. */

/* Baton for abovementioned wrapping. */
struct reporter_3in2_baton {
  const svn_ra_reporter3_t *reporter3;
  void *reporter3_baton;
};

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->set_path(b->reporter3_baton,
                                path, revision, svn_depth_infinity,
                                start_empty, lock_token, pool);
}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->delete_path(b->reporter3_baton, path, pool);
}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->link_path(b->reporter3_baton,
                                 path, url, revision, svn_depth_infinity,
                                 start_empty, lock_token, pool);

}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->finish_report(b->reporter3_baton, pool);
}

/* Wrap the corresponding svn_ra_reporter3_t field in an
   svn_ra_reporter2_t interface.  @a report_baton is a
   @c reporter_3in2_baton_t *. */
static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = report_baton;
  return b->reporter3->abort_report(b->reporter3_baton, pool);
}

/* Wrap svn_ra_reporter3_t calls in an svn_ra_reporter2_t interface.

   Note: For calls where the prototypes are exactly the same, we could
   avoid the pass-through overhead by using the function in the
   reporter returned from session->vtable->do_foo.  But the code would
   get a lot less readable, and the only benefit would be to shave a
   few instructions in a network-bound operation anyway.  So in
   delete_path(), finish_report(), and abort_report(), we cheerfully
   pass through to identical functions. */
static svn_ra_reporter2_t reporter_3in2_wrapper = {
  set_path,
  delete_path,
  link_path,
  finish_report,
  abort_report
};


/* -------------------------------------------------------------- */

/*** Public Interfaces ***/

svn_error_t *svn_ra_initialize(apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

/* Please note: the implementation of svn_ra_create_callbacks is
 * duplicated in libsvn_ra/wrapper_template.h:compat_open() .  This
 * duplication is intentional, is there to avoid a circular
 * dependancy, and is justified in great length in the code of
 * compat_open() in libsvn_ra/wrapper_template.h.  If you modify the
 * implementation of svn_ra_create_callbacks(), be sure to keep the
 * code in wrapper_template.h:compat_open() in sync with your
 * changes. */
svn_error_t *
svn_ra_create_callbacks(svn_ra_callbacks2_t **callbacks,
                        apr_pool_t *pool)
{
  *callbacks = apr_pcalloc(pool, sizeof(**callbacks));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_open3(svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const char *uuid,
                          const svn_ra_callbacks2_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  svn_ra_session_t *session;
  const struct ra_lib_defn *defn;
  const svn_ra__vtable_t *vtable = NULL;
  svn_config_t *servers = NULL;
  const char *server_group;
  apr_uri_t repos_URI;
  apr_status_t apr_err;
#ifdef MUST_CHOOSE_DAV
  const char *http_library = "neon";
#endif
  /* Auth caching parameters. */
  svn_boolean_t store_passwords = SVN_CONFIG_DEFAULT_OPTION_STORE_PASSWORDS;
  svn_boolean_t store_auth_creds = SVN_CONFIG_DEFAULT_OPTION_STORE_AUTH_CREDS;
  const char *store_plaintext_passwords
    = SVN_CONFIG_DEFAULT_OPTION_STORE_PLAINTEXT_PASSWORDS;

  if (callbacks->auth_baton)
    {
      /* The 'store-passwords' and 'store-auth-creds' parameters used to
       * live in SVN_CONFIG_CATEGORY_CONFIG. For backward compatibility,
       * if values for these parameters have already been set by our
       * callers, we use those values as defaults.
       *
       * Note that we can only catch the case where users explicitly set
       * "store-passwords = no" or 'store-auth-creds = no".
       *
       * However, since the default value for both these options is
       * currently (and has always been) "yes", users won't know
       * the difference if they set "store-passwords = yes" or
       * "store-auth-creds = yes" -- they'll get the expected behaviour.
       */

      if (svn_auth_get_parameter(callbacks->auth_baton,
                                 SVN_AUTH_PARAM_DONT_STORE_PASSWORDS) != NULL)
        store_passwords = FALSE;

      if (svn_auth_get_parameter(callbacks->auth_baton,
                                 SVN_AUTH_PARAM_NO_AUTH_CACHE) != NULL)
        store_auth_creds = FALSE;
    }

  if (config)
    {
      /* Grab the 'servers' config. */
      servers = apr_hash_get(config, SVN_CONFIG_CATEGORY_SERVERS,
                             APR_HASH_KEY_STRING);
      if (servers)
        {
          /* First, look in the global section. */

          SVN_ERR(svn_config_get_bool
            (servers, &store_passwords, SVN_CONFIG_SECTION_GLOBAL,
             SVN_CONFIG_OPTION_STORE_PASSWORDS,
             store_passwords));

          SVN_ERR(svn_config_get_yes_no_ask
            (servers, &store_plaintext_passwords, SVN_CONFIG_SECTION_GLOBAL,
             SVN_CONFIG_OPTION_STORE_PLAINTEXT_PASSWORDS,
             SVN_CONFIG_DEFAULT_OPTION_STORE_PLAINTEXT_PASSWORDS));

          SVN_ERR(svn_config_get_bool
            (servers, &store_auth_creds, SVN_CONFIG_SECTION_GLOBAL,
              SVN_CONFIG_OPTION_STORE_AUTH_CREDS,
              store_auth_creds));

          /* Find out where we're about to connect to, and
           * try to pick a server group based on the destination. */
          apr_err = apr_uri_parse(pool, repos_URL, &repos_URI);
          /* ### Should apr_uri_parse leave hostname NULL?  It doesn't
           * for "file:///" URLs, only for bogus URLs like "bogus".
           * If this is the right behavior for apr_uri_parse, maybe we
           * should have a svn_uri_parse wrapper. */
          if (apr_err != APR_SUCCESS || repos_URI.hostname == NULL)
            return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                     _("Illegal repository URL '%s'"),
                                     repos_URL);
          server_group = svn_config_find_group(servers, repos_URI.hostname,
                                               SVN_CONFIG_SECTION_GROUPS, pool);

          if (server_group)
            {
              /* Override global auth caching parameters with the ones
               * for the server group, if any. */
              SVN_ERR(svn_config_get_bool(servers, &store_auth_creds,
                                          server_group,
                                          SVN_CONFIG_OPTION_STORE_AUTH_CREDS,
                                          store_auth_creds));

              SVN_ERR(svn_config_get_bool(servers, &store_passwords,
                                          server_group,
                                          SVN_CONFIG_OPTION_STORE_PASSWORDS,
                                          store_passwords));

              SVN_ERR(svn_config_get_yes_no_ask
                (servers, &store_plaintext_passwords, server_group,
                 SVN_CONFIG_OPTION_STORE_PLAINTEXT_PASSWORDS,
                 store_plaintext_passwords));
            }
#ifdef MUST_CHOOSE_DAV
          /* Now, which DAV-based RA method do we want to use today? */
          http_library
            = svn_config_get_server_setting(servers,
                                            server_group, /* NULL is OK */
                                            SVN_CONFIG_OPTION_HTTP_LIBRARY,
                                            "neon");

          if (strcmp(http_library, "neon") != 0 &&
              strcmp(http_library, "serf") != 0)
            return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                                     _("Invalid config: unknown HTTP library "
                                       "'%s'"),
                                     http_library);
#endif
        }
    }

  if (callbacks->auth_baton)
    {
      /* Save auth caching parameters in the auth parameter hash. */
      if (! store_passwords)
        svn_auth_set_parameter(callbacks->auth_baton,
                               SVN_AUTH_PARAM_DONT_STORE_PASSWORDS, "");

      svn_auth_set_parameter(callbacks->auth_baton,
                             SVN_AUTH_PARAM_STORE_PLAINTEXT_PASSWORDS,
                             store_plaintext_passwords);

      if (! store_auth_creds)
        svn_auth_set_parameter(callbacks->auth_baton,
                               SVN_AUTH_PARAM_NO_AUTH_CACHE, "");
    }

  /* Find the library. */
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      const char *scheme;

      if ((scheme = has_scheme_of(defn, repos_URL)))
        {
          svn_ra__init_func_t initfunc = defn->initfunc;

#ifdef MUST_CHOOSE_DAV
          if (defn->schemes == dav_schemes
              && strcmp(defn->ra_name, http_library) != 0)
            continue;
#endif

          if (! initfunc)
            SVN_ERR(load_ra_module(&initfunc, NULL, defn->ra_name,
                                   pool));
          if (! initfunc)
            /* Library not found. */
            continue;

          SVN_ERR(initfunc(svn_ra_version(), &vtable, pool));

          SVN_ERR(check_ra_version(vtable->get_version(), scheme));

          break;
        }
    }

  if (vtable == NULL)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Unrecognized URL scheme for '%s'"),
                             repos_URL);

  /* Create the session object. */
  session = apr_pcalloc(pool, sizeof(*session));
  session->vtable = vtable;
  session->pool = pool;

  /* Ask the library to open the session. */
  SVN_ERR(vtable->open_session(session, repos_URL, callbacks, callback_baton,
                               config, pool));

  /* Check the UUID. */
  if (uuid)
    {
      const char *repository_uuid;

      SVN_ERR(vtable->get_uuid(session, &repository_uuid, pool));

      if (strcmp(uuid, repository_uuid) != 0)
        {
          return svn_error_createf(SVN_ERR_RA_UUID_MISMATCH, NULL,
                                   _("Repository UUID '%s' doesn't match "
                                     "expected UUID '%s'"),
                                   repository_uuid, uuid);
        }
    }

  *session_p = session;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_open2(svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const svn_ra_callbacks2_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool)
{
  return svn_ra_open3(session_p, repos_URL, NULL,
                      callbacks, callback_baton, config, pool);
}

svn_error_t *svn_ra_open(svn_ra_session_t **session_p,
                         const char *repos_URL,
                         const svn_ra_callbacks_t *callbacks,
                         void *callback_baton,
                         apr_hash_t *config,
                         apr_pool_t *pool)
{
  /* Deprecated function. Copy the contents of the svn_ra_callbacks_t
     to a new svn_ra_callbacks2_t and call svn_ra_open2(). */
  svn_ra_callbacks2_t *callbacks2;
  SVN_ERR(svn_ra_create_callbacks(&callbacks2, pool));
  callbacks2->open_tmp_file = callbacks->open_tmp_file;
  callbacks2->auth_baton = callbacks->auth_baton;
  callbacks2->get_wc_prop = callbacks->get_wc_prop;
  callbacks2->set_wc_prop = callbacks->set_wc_prop;
  callbacks2->push_wc_prop = callbacks->push_wc_prop;
  callbacks2->invalidate_wc_props = callbacks->invalidate_wc_props;
  callbacks2->progress_func = NULL;
  callbacks2->progress_baton = NULL;
  return svn_ra_open2(session_p, repos_URL,
                      callbacks2, callback_baton,
                      config, pool);
}

svn_error_t *svn_ra_reparent(svn_ra_session_t *session,
                             const char *url,
                             apr_pool_t *pool)
{
  const char *repos_root;

  /* Make sure the new URL is in the same repository, so that the
     implementations don't have to do it. */
  SVN_ERR(svn_ra_get_repos_root2(session, &repos_root, pool));
  if (! svn_path_is_ancestor(repos_root, url))
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s' isn't in the same repository as '%s'"),
                             url, repos_root);

  return session->vtable->reparent(session, url, pool);
}

svn_error_t *svn_ra_get_session_url(svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool)
{
  return session->vtable->get_session_url(session, url, pool);
}

svn_error_t *svn_ra_get_latest_revnum(svn_ra_session_t *session,
                                      svn_revnum_t *latest_revnum,
                                      apr_pool_t *pool)
{
  return session->vtable->get_latest_revnum(session, latest_revnum, pool);
}

svn_error_t *svn_ra_get_dated_revision(svn_ra_session_t *session,
                                       svn_revnum_t *revision,
                                       apr_time_t tm,
                                       apr_pool_t *pool)
{
  return session->vtable->get_dated_revision(session, revision, tm, pool);
}

svn_error_t *svn_ra_change_rev_prop(svn_ra_session_t *session,
                                    svn_revnum_t rev,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool)
{
  return session->vtable->change_rev_prop(session, rev, name, value, pool);
}

svn_error_t *svn_ra_rev_proplist(svn_ra_session_t *session,
                                 svn_revnum_t rev,
                                 apr_hash_t **props,
                                 apr_pool_t *pool)
{
  return session->vtable->rev_proplist(session, rev, props, pool);
}

svn_error_t *svn_ra_rev_prop(svn_ra_session_t *session,
                             svn_revnum_t rev,
                             const char *name,
                             svn_string_t **value,
                             apr_pool_t *pool)
{
  return session->vtable->rev_prop(session, rev, name, value, pool);
}

svn_error_t *svn_ra_get_commit_editor3(svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       apr_hash_t *revprop_table,
                                       svn_commit_callback2_t callback,
                                       void *callback_baton,
                                       apr_hash_t *lock_tokens,
                                       svn_boolean_t keep_locks,
                                       apr_pool_t *pool)
{
  return session->vtable->get_commit_editor(session, editor, edit_baton,
                                            revprop_table, callback,
                                            callback_baton, lock_tokens,
                                            keep_locks, pool);
}

svn_error_t *svn_ra_get_commit_editor2(svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *log_msg,
                                       svn_commit_callback2_t callback,
                                       void *callback_baton,
                                       apr_hash_t *lock_tokens,
                                       svn_boolean_t keep_locks,
                                       apr_pool_t *pool)
{
  apr_hash_t *revprop_table = apr_hash_make(pool);
  if (log_msg)
    apr_hash_set(revprop_table, SVN_PROP_REVISION_LOG,
                 APR_HASH_KEY_STRING,
                 svn_string_create(log_msg, pool));
  return svn_ra_get_commit_editor3(session, editor, edit_baton, revprop_table,
                                   callback, callback_baton,
                                   lock_tokens, keep_locks, pool);
}

svn_error_t *svn_ra_get_commit_editor(svn_ra_session_t *session,
                                      const svn_delta_editor_t **editor,
                                      void **edit_baton,
                                      const char *log_msg,
                                      svn_commit_callback_t callback,
                                      void *callback_baton,
                                      apr_hash_t *lock_tokens,
                                      svn_boolean_t keep_locks,
                                      apr_pool_t *pool)
{
  svn_commit_callback2_t callback2;
  void *callback2_baton;

  svn_compat_wrap_commit_callback(&callback2, &callback2_baton,
                                  callback, callback_baton,
                                  pool);

  return svn_ra_get_commit_editor2(session, editor, edit_baton,
                                   log_msg, callback2,
                                   callback2_baton, lock_tokens,
                                   keep_locks, pool);
}

svn_error_t *svn_ra_get_file(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t revision,
                             svn_stream_t *stream,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->get_file(session, path, revision, stream,
                                   fetched_rev, props, pool);
}

svn_error_t *svn_ra_get_dir(svn_ra_session_t *session,
                            const char *path,
                            svn_revnum_t revision,
                            apr_hash_t **dirents,
                            svn_revnum_t *fetched_rev,
                            apr_hash_t **props,
                            apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->get_dir(session, dirents, fetched_rev, props,
                                  path, revision, SVN_DIRENT_ALL, pool);
}

svn_error_t *svn_ra_get_dir2(svn_ra_session_t *session,
                             apr_hash_t **dirents,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             const char *path,
                             svn_revnum_t revision,
                             apr_uint32_t dirent_fields,
                             apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->get_dir(session, dirents, fetched_rev, props,
                                  path, revision, dirent_fields, pool);
}

svn_error_t *svn_ra_get_mergeinfo(svn_ra_session_t *session,
                                  svn_mergeinfo_catalog_t *catalog,
                                  const apr_array_header_t *paths,
                                  svn_revnum_t revision,
                                  svn_mergeinfo_inheritance_t inherit,
                                  svn_boolean_t include_descendants,
                                  apr_pool_t *pool)
{
  svn_error_t *err;
  int i;

  /* Validate path format. */
  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      assert(*path != '/');
    }

  /* Check server Merge Tracking capability. */
  err = svn_ra__assert_mergeinfo_capable_server(session, NULL, pool);
  if (err)
    {
      *catalog = NULL;
      return err;
    }

  return session->vtable->get_mergeinfo(session, catalog, paths,
                                        revision, inherit,
                                        include_descendants, pool);
}

svn_error_t *svn_ra_do_update2(svn_ra_session_t *session,
                               const svn_ra_reporter3_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_update_to,
                               const char *update_target,
                               svn_depth_t depth,
                               svn_boolean_t send_copyfrom_args,
                               const svn_delta_editor_t *update_editor,
                               void *update_baton,
                               apr_pool_t *pool)
{
  assert(svn_path_is_empty(update_target)
         || svn_path_is_single_path_component(update_target));
  return session->vtable->do_update(session,
                                    reporter, report_baton,
                                    revision_to_update_to, update_target,
                                    depth, send_copyfrom_args,
                                    update_editor, update_baton,
                                    pool);
}

svn_error_t *svn_ra_do_update(svn_ra_session_t *session,
                              const svn_ra_reporter2_t **reporter,
                              void **report_baton,
                              svn_revnum_t revision_to_update_to,
                              const char *update_target,
                              svn_boolean_t recurse,
                              const svn_delta_editor_t *update_editor,
                              void *update_baton,
                              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  assert(svn_path_is_empty(update_target)
         || svn_path_is_single_path_component(update_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_update(session,
                                    &(b->reporter3), &(b->reporter3_baton),
                                    revision_to_update_to, update_target,
                                    SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                    FALSE, /* no copyfrom args */
                                    update_editor, update_baton,
                                    pool);
}

svn_error_t *svn_ra_do_switch2(svn_ra_session_t *session,
                               const svn_ra_reporter3_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_switch_to,
                               const char *switch_target,
                               svn_depth_t depth,
                               const char *switch_url,
                               const svn_delta_editor_t *switch_editor,
                               void *switch_baton,
                               apr_pool_t *pool)
{
  assert(svn_path_is_empty(switch_target)
         || svn_path_is_single_path_component(switch_target));
  return session->vtable->do_switch(session,
                                    reporter, report_baton,
                                    revision_to_switch_to, switch_target,
                                    depth, switch_url, switch_editor,
                                    switch_baton, pool);
}

svn_error_t *svn_ra_do_switch(svn_ra_session_t *session,
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
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  assert(svn_path_is_empty(switch_target)
         || svn_path_is_single_path_component(switch_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_switch(session,
                                    &(b->reporter3), &(b->reporter3_baton),
                                    revision_to_switch_to, switch_target,
                                    SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                    switch_url, switch_editor, switch_baton,
                                    pool);
}

svn_error_t *svn_ra_do_status2(svn_ra_session_t *session,
                               const svn_ra_reporter3_t **reporter,
                               void **report_baton,
                               const char *status_target,
                               svn_revnum_t revision,
                               svn_depth_t depth,
                               const svn_delta_editor_t *status_editor,
                               void *status_baton,
                               apr_pool_t *pool)
{
  assert(svn_path_is_empty(status_target)
         || svn_path_is_single_path_component(status_target));
  return session->vtable->do_status(session,
                                    reporter, report_baton,
                                    status_target, revision, depth,
                                    status_editor, status_baton, pool);
}

svn_error_t *svn_ra_do_status(svn_ra_session_t *session,
                              const svn_ra_reporter2_t **reporter,
                              void **report_baton,
                              const char *status_target,
                              svn_revnum_t revision,
                              svn_boolean_t recurse,
                              const svn_delta_editor_t *status_editor,
                              void *status_baton,
                              apr_pool_t *pool)
{
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  assert(svn_path_is_empty(status_target)
         || svn_path_is_single_path_component(status_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_status(session,
                                    &(b->reporter3), &(b->reporter3_baton),
                                    status_target, revision,
                                    SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                                    status_editor, status_baton, pool);
}

svn_error_t *svn_ra_do_diff3(svn_ra_session_t *session,
                             const svn_ra_reporter3_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision,
                             const char *diff_target,
                             svn_depth_t depth,
                             svn_boolean_t ignore_ancestry,
                             svn_boolean_t text_deltas,
                             const char *versus_url,
                             const svn_delta_editor_t *diff_editor,
                             void *diff_baton,
                             apr_pool_t *pool)
{
  assert(svn_path_is_empty(diff_target)
         || svn_path_is_single_path_component(diff_target));
  return session->vtable->do_diff(session,
                                  reporter, report_baton,
                                  revision, diff_target,
                                  depth, ignore_ancestry,
                                  text_deltas, versus_url, diff_editor,
                                  diff_baton, pool);
}

svn_error_t *svn_ra_do_diff2(svn_ra_session_t *session,
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
  struct reporter_3in2_baton *b = apr_palloc(pool, sizeof(*b));
  assert(svn_path_is_empty(diff_target)
         || svn_path_is_single_path_component(diff_target));
  *reporter = &reporter_3in2_wrapper;
  *report_baton = b;
  return session->vtable->do_diff(session,
                                  &(b->reporter3), &(b->reporter3_baton),
                                  revision, diff_target,
                                  SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                  ignore_ancestry, text_deltas, versus_url,
                                  diff_editor, diff_baton, pool);
}

svn_error_t *svn_ra_do_diff(svn_ra_session_t *session,
                            const svn_ra_reporter2_t **reporter,
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
  assert(svn_path_is_empty(diff_target)
         || svn_path_is_single_path_component(diff_target));
  return svn_ra_do_diff2(session, reporter, report_baton, revision,
                         diff_target, recurse, ignore_ancestry, TRUE,
                         versus_url, diff_editor, diff_baton, pool);
}

svn_error_t *svn_ra_get_log2(svn_ra_session_t *session,
                             const apr_array_header_t *paths,
                             svn_revnum_t start,
                             svn_revnum_t end,
                             int limit,
                             svn_boolean_t discover_changed_paths,
                             svn_boolean_t strict_node_history,
                             svn_boolean_t include_merged_revisions,
                             const apr_array_header_t *revprops,
                             svn_log_entry_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  if (paths)
    {
      int i;
      for (i = 0; i < paths->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(paths, i, const char *);
          assert(*path != '/');
        }
    }

  if (include_merged_revisions)
    SVN_ERR(svn_ra__assert_mergeinfo_capable_server(session, NULL, pool));

  return session->vtable->get_log(session, paths, start, end, limit,
                                  discover_changed_paths, strict_node_history,
                                  include_merged_revisions, revprops,
                                  receiver, receiver_baton, pool);
}

svn_error_t *svn_ra_get_log(svn_ra_session_t *session,
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
  svn_log_entry_receiver_t receiver2;
  void *receiver2_baton;

  if (paths)
    {
      int i;
      for (i = 0; i < paths->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX(paths, i, const char *);
          assert(*path != '/');
        }
    }

  svn_compat_wrap_log_receiver(&receiver2, &receiver2_baton,
                               receiver, receiver_baton,
                               pool);

  return svn_ra_get_log2(session, paths, start, end, limit,
                         discover_changed_paths, strict_node_history,
                         FALSE, svn_compat_log_revprops_in(pool),
                         receiver2, receiver2_baton, pool);
}

svn_error_t *svn_ra_check_path(svn_ra_session_t *session,
                               const char *path,
                               svn_revnum_t revision,
                               svn_node_kind_t *kind,
                               apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->check_path(session, path, revision, kind, pool);
}

svn_error_t *svn_ra_stat(svn_ra_session_t *session,
                         const char *path,
                         svn_revnum_t revision,
                         svn_dirent_t **dirent,
                         apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->stat(session, path, revision, dirent, pool);
}

svn_error_t *svn_ra_get_uuid2(svn_ra_session_t *session,
                              const char **uuid,
                              apr_pool_t *pool)
{
  SVN_ERR(session->vtable->get_uuid(session, uuid, pool));
  *uuid = *uuid ? apr_pstrdup(pool, *uuid) : NULL;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_uuid(svn_ra_session_t *session,
                             const char **uuid,
                             apr_pool_t *pool)
{
  return session->vtable->get_uuid(session, uuid, pool);
}

svn_error_t *svn_ra_get_repos_root2(svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool)
{
  SVN_ERR(session->vtable->get_repos_root(session, url, pool));
  *url = *url ? apr_pstrdup(pool, *url) : NULL;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_get_repos_root(svn_ra_session_t *session,
                                   const char **url,
                                   apr_pool_t *pool)
{
  return session->vtable->get_repos_root(session, url, pool);
}

svn_error_t *svn_ra_get_locations(svn_ra_session_t *session,
                                  apr_hash_t **locations,
                                  const char *path,
                                  svn_revnum_t peg_revision,
                                  apr_array_header_t *location_revisions,
                                  apr_pool_t *pool)
{
  svn_error_t *err;

  assert(*path != '/');
  err = session->vtable->get_locations(session, locations, path,
                                       peg_revision, location_revisions, pool);
  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED))
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__locations_from_log(session, locations, path,
                                       peg_revision, location_revisions,
                                       pool);
    }
  return err;
}

svn_error_t *
svn_ra_get_location_segments(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t peg_revision,
                             svn_revnum_t start_rev,
                             svn_revnum_t end_rev,
                             svn_location_segment_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  svn_error_t *err;

  assert(*path != '/');
  err = session->vtable->get_location_segments(session, path, peg_revision,
                                               start_rev, end_rev,
                                               receiver, receiver_baton, pool);
  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED))
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__location_segments_from_log(session, path,
                                               peg_revision, start_rev,
                                               end_rev, receiver,
                                               receiver_baton, pool);
    }
  return err;
}

svn_error_t *svn_ra_get_file_revs(svn_ra_session_t *session,
                                  const char *path,
                                  svn_revnum_t start,
                                  svn_revnum_t end,
                                  svn_ra_file_rev_handler_t handler,
                                  void *handler_baton,
                                  apr_pool_t *pool)
{
  svn_file_rev_handler_t handler2;
  void *handler2_baton;

  assert(*path != '/');

  svn_compat_wrap_file_rev_handler(&handler2, &handler2_baton,
                                   handler, handler_baton,
                                   pool);

  return svn_ra_get_file_revs2(session, path, start, end, FALSE, handler2,
                               handler2_baton, pool);
}

svn_error_t *svn_ra_get_file_revs2(svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t start,
                                   svn_revnum_t end,
                                   svn_boolean_t include_merged_revisions,
                                   svn_file_rev_handler_t handler,
                                   void *handler_baton,
                                   apr_pool_t *pool)
{
  svn_error_t *err;

  assert(*path != '/');

  if (include_merged_revisions)
    SVN_ERR(svn_ra__assert_mergeinfo_capable_server(session, NULL, pool));

  err = session->vtable->get_file_revs(session, path, start, end,
                                       include_merged_revisions,
                                       handler, handler_baton, pool);
  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED))
    {
      svn_error_clear(err);

      /* Do it the slow way, using get-logs, for older servers. */
      err = svn_ra__file_revs_from_log(session, path, start, end,
                                       handler, handler_baton, pool);
    }
  return err;
}

svn_error_t *svn_ra_lock(svn_ra_session_t *session,
                         apr_hash_t *path_revs,
                         const char *comment,
                         svn_boolean_t steal_lock,
                         svn_ra_lock_callback_t lock_func,
                         void *lock_baton,
                         apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(NULL, path_revs); hi; hi = apr_hash_next(hi))
    {
      const void *path;
      apr_hash_this(hi, &path, NULL, NULL);
      assert(*((const char *)path) != '/');
    }

  if (comment && ! svn_xml_is_xml_safe(comment, strlen(comment)))
    return svn_error_create
      (SVN_ERR_XML_UNESCAPABLE_DATA, NULL,
       _("Lock comment contains illegal characters"));

  return session->vtable->lock(session, path_revs, comment, steal_lock,
                               lock_func, lock_baton, pool);
}

svn_error_t *svn_ra_unlock(svn_ra_session_t *session,
                           apr_hash_t *path_tokens,
                           svn_boolean_t break_lock,
                           svn_ra_lock_callback_t lock_func,
                           void *lock_baton,
                           apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(NULL, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *path;
      apr_hash_this(hi, &path, NULL, NULL);
      assert(*((const char *)path) != '/');
    }

  return session->vtable->unlock(session, path_tokens, break_lock,
                                 lock_func, lock_baton, pool);
}

svn_error_t *svn_ra_get_lock(svn_ra_session_t *session,
                             svn_lock_t **lock,
                             const char *path,
                             apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->get_lock(session, lock, path, pool);
}

svn_error_t *svn_ra_get_locks(svn_ra_session_t *session,
                              apr_hash_t **locks,
                              const char *path,
                              apr_pool_t *pool)
{
  assert(*path != '/');
  return session->vtable->get_locks(session, locks, path, pool);
}

svn_error_t *svn_ra_replay(svn_ra_session_t *session,
                           svn_revnum_t revision,
                           svn_revnum_t low_water_mark,
                           svn_boolean_t text_deltas,
                           const svn_delta_editor_t *editor,
                           void *edit_baton,
                           apr_pool_t *pool)
{
  return session->vtable->replay(session, revision, low_water_mark,
                                 text_deltas, editor, edit_baton, pool);
}

svn_error_t *
svn_ra_replay_range(svn_ra_session_t *session,
                    svn_revnum_t start_revision,
                    svn_revnum_t end_revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t text_deltas,
                    svn_ra_replay_revstart_callback_t revstart_func,
                    svn_ra_replay_revfinish_callback_t revfinish_func,
                    void *replay_baton,
                    apr_pool_t *pool)
{
  svn_error_t *err =
    session->vtable->replay_range(session, start_revision, end_revision,
                                  low_water_mark, text_deltas,
                                  revstart_func, revfinish_func,
                                  replay_baton, pool);

  if (err && (err->apr_err == SVN_ERR_RA_NOT_IMPLEMENTED))
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      svn_revnum_t rev;

      svn_error_clear(err);
      err = SVN_NO_ERROR;

      for (rev = start_revision ; rev <= end_revision ; rev++)
        {
          const svn_delta_editor_t *editor;
          void *edit_baton;
          apr_hash_t *rev_props;

          svn_pool_clear(subpool);

          SVN_ERR(svn_ra_rev_proplist(session, rev, &rev_props, subpool));

          SVN_ERR(revstart_func(rev, replay_baton,
                                &editor, &edit_baton,
                                rev_props,
                                subpool));
          SVN_ERR(svn_ra_replay(session, rev, low_water_mark,
                                text_deltas, editor, edit_baton,
                                subpool));
          SVN_ERR(revfinish_func(rev, replay_baton,
                                 editor, edit_baton,
                                 rev_props,
                                 subpool));
        }
      svn_pool_destroy(subpool);
    }

  SVN_ERR(err);

  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_has_capability(svn_ra_session_t *session,
                                   svn_boolean_t *has,
                                   const char *capability,
                                   apr_pool_t *pool)
{
  return session->vtable->has_capability(session, has, capability, pool);
}



svn_error_t *
svn_ra_print_modules(svn_stringbuf_t *output,
                     apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  const char * const *schemes;
  svn_ra__init_func_t initfunc;
  const svn_ra__vtable_t *vtable;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      char *line;

      svn_pool_clear(iterpool);

      initfunc = defn->initfunc;
      if (! initfunc)
        SVN_ERR(load_ra_module(&initfunc, NULL, defn->ra_name,
                               iterpool));

      if (initfunc)
        {
          SVN_ERR(initfunc(svn_ra_version(), &vtable, iterpool));

          SVN_ERR(check_ra_version(vtable->get_version(), defn->ra_name));

          /* Note: if you change the formatting of the description,
             bear in mind that ra_svn's description has multiple lines when
             built with SASL. */
          line = apr_psprintf(iterpool, "* ra_%s : %s\n",
                              defn->ra_name,
                              vtable->get_description());
          svn_stringbuf_appendcstr(output, line);

          for (schemes = vtable->get_schemes(iterpool); *schemes != NULL;
               ++schemes)
            {
              line = apr_psprintf(iterpool, _("  - handles '%s' scheme\n"),
                                  *schemes);
              svn_stringbuf_appendcstr(output, line);
            }
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_print_ra_libraries(svn_stringbuf_t **descriptions,
                          void *ra_baton,
                          apr_pool_t *pool)
{
  *descriptions = svn_stringbuf_create("", pool);
  return svn_ra_print_modules(*descriptions, pool);
}


/* Return the library version number. */
const svn_version_t *
svn_ra_version(void)
{
  SVN_VERSION_BODY;
}


/*** Compatibility Interfaces **/
svn_error_t *
svn_ra_init_ra_libs(void **ra_baton,
                    apr_pool_t *pool)
{
  *ra_baton = pool;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_get_ra_library(svn_ra_plugin_t **library,
                      void *ra_baton,
                      const char *url,
                      apr_pool_t *pool)
{
  const struct ra_lib_defn *defn;
  apr_pool_t *load_pool = ra_baton;
  apr_hash_t *ht = apr_hash_make(pool);

  /* Figure out which RA library key matches URL. */
  for (defn = ra_libraries; defn->ra_name != NULL; ++defn)
    {
      const char *scheme;
      if ((scheme = has_scheme_of(defn, url)))
        {
          svn_ra_init_func_t compat_initfunc = defn->compat_initfunc;

          if (! compat_initfunc)
            {
              SVN_ERR(load_ra_module
                      (NULL, &compat_initfunc, defn->ra_name, load_pool));
            }
          if (! compat_initfunc)
            {
              continue;
            }

          SVN_ERR(compat_initfunc(SVN_RA_ABI_VERSION, load_pool, ht));

          *library = apr_hash_get(ht, scheme, APR_HASH_KEY_STRING);

          /* The library may support just a subset of the schemes listed,
             so we have to check here too. */
          if (! *library)
            break;

          SVN_ERR(check_ra_version((*library)->get_version(), scheme));

          return SVN_NO_ERROR;
        }
    }

  /* Couldn't find a match... */
  *library = NULL;
  return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                           _("Unrecognized URL scheme '%s'"), url);
}

/* For each libsvn_ra_foo library that is not linked in, provide a default
   implementation for svn_ra_foo_init which returns a "not implemented"
   error. */

#ifndef SVN_LIBSVN_CLIENT_LINKS_RA_NEON
svn_error_t *
svn_ra_dav_init(int abi_version,
                apr_pool_t *pool,
                apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_CLIENT_LINKS_RA_NEON */

#ifndef SVN_LIBSVN_CLIENT_LINKS_RA_SVN
svn_error_t *
svn_ra_svn_init(int abi_version,
                apr_pool_t *pool,
                apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_CLIENT_LINKS_RA_SVN */

#ifndef SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL
svn_error_t *
svn_ra_local_init(int abi_version,
                  apr_pool_t *pool,
                  apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_CLIENT_LINKS_RA_LOCAL */

#ifndef SVN_LIBSVN_CLIENT_LINKS_RA_SERF
svn_error_t *
svn_ra_serf_init(int abi_version,
                 apr_pool_t *pool,
                 apr_hash_t *hash)
{
  return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);
}
#endif /* ! SVN_LIBSVN_CLIENT_LINKS_RA_SERF */
