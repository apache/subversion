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



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <apr_uri.h>

#include <expat.h>

#include <serf.h>

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_version.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "ra_serf.h"


static const svn_version_t *
ra_serf_version(void)
{
  SVN_VERSION_BODY;
}

#define RA_SERF_DESCRIPTION \
    N_("Access repository via WebDAV protocol through serf.")

static const char *
ra_serf_get_description(void)
{
  return _(RA_SERF_DESCRIPTION);
}

static const char * const *
ra_serf_get_schemes(apr_pool_t *pool)
{
  static const char *serf_ssl[] = { "http", "https", NULL };
  static const char *serf_no_ssl[] = { "http", NULL };

  /* TODO: Runtime detection. */
  return serf_ssl;
}

static svn_error_t *
svn_ra_serf__open(svn_ra_session_t *session,
                  const char *repos_URL,
                  const svn_ra_callbacks2_t *callbacks,
                  void *callback_baton,
                  apr_hash_t *config,
                  apr_pool_t *pool)
{
  apr_status_t status;
  ra_serf_session_t *serf_sess;
  apr_uri_t url;

  serf_sess = apr_pcalloc(pool, sizeof(*serf_sess));
  apr_pool_create(&serf_sess->pool, pool);
  serf_sess->bkt_alloc = serf_bucket_allocator_create(serf_sess->pool, NULL,
                                                      NULL);
  serf_sess->cached_props = apr_hash_make(pool);
  serf_sess->wc_callbacks = callbacks;
  serf_sess->wc_callback_baton = callback_baton;

  /* todo: reuse serf context across sessions */
  serf_sess->context = serf_context_create(pool);

  apr_uri_parse(serf_sess->pool, repos_URL, &url);
  serf_sess->repos_url = url;
  serf_sess->repos_url_str = apr_pstrdup(serf_sess->pool, repos_URL);

  if (!url.port)
    {
      url.port = apr_uri_port_of_scheme(url.scheme);
    }
  serf_sess->using_ssl = (strcasecmp(url.scheme, "https") == 0);

  /* register cleanups */
  apr_pool_cleanup_register(serf_sess->pool, serf_sess, cleanup_serf_session,
                            apr_pool_cleanup_null);

  serf_sess->conns = apr_palloc(pool, sizeof(*serf_sess->conns) * 4);

  serf_sess->conns[0] = apr_pcalloc(pool, sizeof(*serf_sess->conns[0]));
  serf_sess->conns[0]->bkt_alloc =
          serf_bucket_allocator_create(serf_sess->pool, NULL, NULL);

  /* fetch the DNS record for this host */
  status = apr_sockaddr_info_get(&serf_sess->conns[0]->address, url.hostname,
                                 APR_UNSPEC, url.port, 0, pool);
  if (status)
    {
      return svn_error_createf(status, NULL,
                               _("Could not lookup hostname: %s://%s"),
                               url.scheme, url.hostname);
    }

  serf_sess->conns[0]->using_ssl = (strcasecmp(url.scheme, "https") == 0);
  serf_sess->conns[0]->hostinfo = url.hostinfo;

  /* go ahead and tell serf about the connection. */
  serf_sess->conns[0]->conn =
      serf_connection_create(serf_sess->context, serf_sess->conns[0]->address,
                             conn_setup, serf_sess->conns[0],
                             conn_closed, serf_sess->conns[0],
                             serf_sess->pool);

  serf_sess->num_conns = 1;

  session->priv = serf_sess;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__reparent(svn_ra_session_t *ra_session,
                      const char *url,
                      apr_pool_t *pool)
{
  ra_serf_session_t *session = ra_session->priv;
  apr_uri_t new_url;

  /* If it's the URL we already have, wave our hands and do nothing. */
  if (strcmp(session->repos_url_str, url) == 0)
    {
      return SVN_NO_ERROR;
    }

  /* Do we need to check that it's the same host and port? */
  apr_uri_parse(session->pool, url, &new_url);

  session->repos_url.path = new_url.path;
  session->repos_url_str = apr_pstrdup(pool, url);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_latest_revnum(svn_ra_session_t *ra_session,
                               svn_revnum_t *latest_revnum,
                               apr_pool_t *pool)
{
  apr_hash_t *props, *ns_props;
  ra_serf_session_t *session = ra_session->priv;
  const char *vcc_url, *baseline_url, *version_name;

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->conns[0],
                         session->repos_url.path,
                         SVN_INVALID_REVNUM, "0", base_props, pool));

  vcc_url = get_prop(props, session->repos_url.path, "DAV:",
                       "version-controlled-configuration");

  if (!vcc_url)
    {
      abort();
    }

  /* Using the version-controlled-configuration, fetch the checked-in prop. */
  SVN_ERR(retrieve_props(props, session, session->conns[0],
                         vcc_url, SVN_INVALID_REVNUM, "0",
                         checked_in_props, pool));

  baseline_url = get_prop(props, vcc_url,
                            "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  /* Using the checked-in property, fetch:
   *    baseline-connection *and* version-name
   */
  SVN_ERR(retrieve_props(props, session, session->conns[0],
                         baseline_url, SVN_INVALID_REVNUM,
                         "0", baseline_props, pool));

  version_name = get_prop(props, baseline_url, "DAV:", "version-name");

  if (!version_name)
    {
      abort();
    }

  *latest_revnum = SVN_STR_TO_REV(version_name);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_dated_revision(svn_ra_session_t *session,
                                svn_revnum_t *revision,
                                apr_time_t tm,
                                apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__change_rev_prop(svn_ra_session_t *session,
                             svn_revnum_t rev,
                             const char *name,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__rev_proplist(svn_ra_session_t *session,
                          svn_revnum_t rev,
                          apr_hash_t **props,
                          apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__rev_prop(svn_ra_session_t *session,
                      svn_revnum_t rev,
                      const char *name,
                      svn_string_t **value,
                      apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_file(svn_ra_session_t *session,
                      const char *path,
                      svn_revnum_t revision,
                      svn_stream_t *stream,
                      svn_revnum_t *fetched_rev,
                      apr_hash_t **props,
                      apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_dir(svn_ra_session_t *session,
                     const char *path,
                     svn_revnum_t revision,
                     apr_uint32_t dirent_fields,
                     apr_hash_t **dirents,
                     svn_revnum_t *fetched_rev,
                     apr_hash_t **props,
                     apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__do_switch(svn_ra_session_t *session,
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
  abort();
}

static svn_error_t *
svn_ra_serf__do_status(svn_ra_session_t *session,
                       const svn_ra_reporter2_t **reporter,
                       void **report_baton,
                       const char *status_target,
                       svn_revnum_t revision,
                       svn_boolean_t recurse,
                       const svn_delta_editor_t *status_editor,
                       void *status_baton,
                       apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__do_diff(svn_ra_session_t *session,
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
  abort();
}

static svn_error_t *
svn_ra_serf__check_path(svn_ra_session_t *ra_session,
                        const char *rel_path,
                        svn_revnum_t revision,
                        svn_node_kind_t *kind,
                        apr_pool_t *pool)
{
  ra_serf_session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *path, *res_type;

  path = session->repos_url.path;

  /* If we have a relative path, append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component(path, rel_path, pool);
    }

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->conns[0], path, revision, "0",
                         check_path_props, pool));
  res_type = get_ver_prop(props, path, revision, "DAV:", "resourcetype");

  if (!res_type)
    {
      /* if the file isn't there, return none; but let's abort for now. */
      abort();
      *kind = svn_node_none;
    }
  else if (strcmp(res_type, "collection") == 0)
    {
      *kind = svn_node_dir;
    }
  else
    {
      *kind = svn_node_file;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__stat(svn_ra_session_t *session,
                  const char *path,
                  svn_revnum_t revision,
                  svn_dirent_t **dirent,
                  apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_uuid(svn_ra_session_t *ra_session,
                      const char **uuid,
                      apr_pool_t *pool)
{
  ra_serf_session_t *session = ra_session->priv;
  apr_hash_t *props;

  props = apr_hash_make(pool);

  SVN_ERR(retrieve_props(props, session, session->conns[0],
                         session->repos_url.path, 
                         SVN_INVALID_REVNUM, "0", uuid_props, pool));
  *uuid = get_prop(props, session->repos_url.path,
                     SVN_DAV_PROP_NS_DAV, "repository-uuid");

  if (!*uuid)
    {
      abort();
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_repos_root(svn_ra_session_t *ra_session,
                            const char **url,
                            apr_pool_t *pool)
{
  ra_serf_session_t *session = ra_session->priv;

  if (!session->repos_root_str)
    {
      const char *baseline_url, *root_path;
      svn_stringbuf_t *url_buf;
      apr_hash_t *props;

      props = apr_hash_make(pool);

      SVN_ERR(retrieve_props(props, session, session->conns[0],
                             session->repos_url.path, 
                             SVN_INVALID_REVNUM, "0", repos_root_props, pool));
      baseline_url = get_prop(props, session->repos_url.path,
                                SVN_DAV_PROP_NS_DAV, "baseline-relative-path");

      if (!baseline_url)
        {
          abort();
        }

      /* If we see baseline_url as "", we're the root.  Otherwise... */
      if (*baseline_url == '\0')
        {
          root_path = session->repos_url.path;
          session->repos_root = session->repos_url;
          session->repos_root_str = session->repos_url_str;
        }
      else
        {
          url_buf = svn_stringbuf_create(session->repos_url.path, pool);
          svn_path_remove_components(url_buf,
                                     svn_path_component_count(baseline_url));
          root_path = apr_pstrdup(session->pool, url_buf->data);

          /* Now that we have the root_path, recreate the root_url. */
          session->repos_root = session->repos_url;
          session->repos_root.path = (char*)root_path;
          session->repos_root_str = apr_uri_unparse(session->pool,
                                                    &session->repos_root, 0);
        }
    }

  *url = session->repos_root_str;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__lock(svn_ra_session_t *session,
                  apr_hash_t *path_revs,
                  const char *comment,
                  svn_boolean_t force,
                  svn_ra_lock_callback_t lock_func,
                  void *lock_baton,
                  apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__unlock(svn_ra_session_t *session,
                    apr_hash_t *path_tokens,
                    svn_boolean_t force,
                    svn_ra_lock_callback_t lock_func,
                    void *lock_baton,
                    apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_lock(svn_ra_session_t *session,
                      svn_lock_t **lock,
                      const char *path,
                      apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__get_locks(svn_ra_session_t *session,
                       apr_hash_t **locks,
                       const char *path,
                       apr_pool_t *pool)
{
  abort();
}

static svn_error_t *
svn_ra_serf__replay(svn_ra_session_t *session,
                    svn_revnum_t revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t text_deltas,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    apr_pool_t *pool)
{
  abort();
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
svn_ra_serf__init(const svn_version_t *loader_version,
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
