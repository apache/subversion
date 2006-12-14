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
#include "svn_time.h"
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
#if 0
  /* ### Temporary: to shut up a warning. */
  static const char *serf_no_ssl[] = { "http", NULL };
#endif

  /* TODO: Runtime detection. */
  return serf_ssl;
}

static svn_error_t *
load_config(svn_ra_serf__session_t *session,
            apr_hash_t *config_hash,
            apr_pool_t *pool)
{
  svn_config_t *config;
  const char *server_group;

  config = apr_hash_get(config_hash, SVN_CONFIG_CATEGORY_SERVERS,
                        APR_HASH_KEY_STRING);

  SVN_ERR(svn_config_get_bool(config, &session->using_compression,
                              SVN_CONFIG_SECTION_GLOBAL,
                              SVN_CONFIG_OPTION_HTTP_COMPRESSION, TRUE));

  server_group = svn_config_find_group(config,
                                       session->repos_url.hostname,
                                       SVN_CONFIG_SECTION_GROUPS, pool);

  if (server_group)
    {
      SVN_ERR(svn_config_get_bool(config, &session->using_compression,
                                  server_group,
                                  SVN_CONFIG_OPTION_HTTP_COMPRESSION,
                                  session->using_compression));
    }

  return SVN_NO_ERROR;
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
  svn_ra_serf__session_t *serf_sess;
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

  SVN_ERR(load_config(serf_sess, config, pool));

  /* register cleanups */
  apr_pool_cleanup_register(serf_sess->pool, serf_sess,
                            svn_ra_serf__cleanup_serf_session,
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

  serf_sess->conns[0]->using_ssl = serf_sess->using_ssl;
  serf_sess->conns[0]->using_compression = serf_sess->using_compression;
  serf_sess->conns[0]->hostinfo = url.hostinfo;

  /* go ahead and tell serf about the connection. */
  serf_sess->conns[0]->conn =
      serf_connection_create(serf_sess->context, serf_sess->conns[0]->address,
                             svn_ra_serf__conn_setup, serf_sess->conns[0],
                             svn_ra_serf__conn_closed, serf_sess->conns[0],
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
  svn_ra_serf__session_t *session = ra_session->priv;
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
  apr_hash_t *props;
  svn_ra_serf__session_t *session = ra_session->priv;
  const char *vcc_url, *baseline_url, *version_name;

  props = apr_hash_make(pool);

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                     session, session->conns[0],
                                     session->repos_url.path, pool));

  if (!vcc_url)
    {
      abort();
    }

  /* Using the version-controlled-configuration, fetch the checked-in prop. */
  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      vcc_url, SVN_INVALID_REVNUM, "0",
                                      checked_in_props, pool));

  baseline_url = svn_ra_serf__get_prop(props, vcc_url,
                                       "DAV:", "checked-in");

  if (!baseline_url)
    {
      abort();
    }

  /* Using the checked-in property, fetch:
   *    baseline-connection *and* version-name
   */
  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      baseline_url, SVN_INVALID_REVNUM,
                                      "0", baseline_props, pool));

  version_name = svn_ra_serf__get_prop(props, baseline_url,
                                       "DAV:", "version-name");

  if (!version_name)
    {
      abort();
    }

  *latest_revnum = SVN_STR_TO_REV(version_name);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__rev_proplist(svn_ra_session_t *ra_session,
                          svn_revnum_t rev,
                          apr_hash_t **ret_props,
                          apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *vcc_url;

  props = apr_hash_make(pool);
  *ret_props = apr_hash_make(pool);

  SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                     session, session->conns[0],
                                     session->repos_url.path, pool));

  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      vcc_url, rev, "0", all_props, pool));

  svn_ra_serf__walk_all_props(props, vcc_url, rev, svn_ra_serf__set_bare_props,
                              *ret_props, pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__rev_prop(svn_ra_session_t *session,
                      svn_revnum_t rev,
                      const char *name,
                      svn_string_t **value,
                      apr_pool_t *pool)
{
  apr_hash_t *props;

  SVN_ERR(svn_ra_serf__rev_proplist(session, rev, &props, pool));

  *value = apr_hash_get(props, name, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_path_props(svn_ra_serf__propfind_context_t **ret_prop_ctx,
                 apr_hash_t **ret_props,
                 const char **ret_path,
                 svn_revnum_t *ret_revision,
                 svn_ra_serf__session_t *session,
                 const char *rel_path,
                 svn_revnum_t revision,
                 const svn_ra_serf__dav_props_t *desired_props,
                 apr_pool_t *pool)
{
  svn_ra_serf__propfind_context_t *prop_ctx;
  apr_hash_t *props;
  const char *path;

  path = session->repos_url.path;

  /* If we have a relative path, append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component(path, rel_path, pool);
    }

  props = apr_hash_make(pool);

  prop_ctx = NULL;

  /* If we were given a specific revision, we have to fetch the VCC and
   * do a PROPFIND off of that.
   */
  if (!SVN_IS_VALID_REVNUM(revision))
    {
      svn_ra_serf__deliver_props(&prop_ctx, props, session, session->conns[0],
                                 path, revision, "0", desired_props, TRUE,
                                 NULL, session->pool);
    }
  else
    {
      const char *vcc_url, *relative_url, *basecoll_url;

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &relative_url,
                                         session, session->conns[0],
                                         path, pool));
      
      SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                          vcc_url, revision,
                                          "0", baseline_props, pool));
      
      basecoll_url = svn_ra_serf__get_ver_prop(props, vcc_url, revision,
                                               "DAV:", "baseline-collection");
      
      if (!basecoll_url)
        {
          abort();
        }
    
      /* We will try again with our new path; however, we're now 
       * technically an unversioned resource because we are accessing
       * the revision's baseline-collection.
       */  
      prop_ctx = NULL;
      path = svn_path_url_add_component(basecoll_url, relative_url, pool);
      revision = SVN_INVALID_REVNUM;
      svn_ra_serf__deliver_props(&prop_ctx, props, session, session->conns[0],
                                 path, revision, "0",
                                 desired_props, TRUE,
                                 NULL, session->pool);
    }

  SVN_ERR(svn_ra_serf__wait_for_props(prop_ctx, session, pool));

  *ret_path = path;
  *ret_prop_ctx = prop_ctx;
  *ret_props = props;
  *ret_revision = revision;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__check_path(svn_ra_session_t *ra_session,
                        const char *rel_path,
                        svn_revnum_t revision,
                        svn_node_kind_t *kind,
                        apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  svn_ra_serf__propfind_context_t *prop_ctx;
  const char *path, *res_type;
  svn_revnum_t fetched_rev;

  SVN_ERR(fetch_path_props(&prop_ctx, &props, &path, &fetched_rev,
                           session, rel_path,
                           revision, check_path_props, pool));

  if (svn_ra_serf__propfind_status_code(prop_ctx) == 404)
    {
      *kind = svn_node_none;
    }
  else
    {
      res_type = svn_ra_serf__get_ver_prop(props, path, fetched_rev,
                                           "DAV:", "resourcetype");
      if (!res_type)
        {
          /* How did this happen? */
          abort();
        }
      else if (strcmp(res_type, "collection") == 0)
        {
          *kind = svn_node_dir;
        }
      else
        {
          *kind = svn_node_file;
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
dirent_walker(void *baton,
              const char *ns, apr_ssize_t ns_len,
              const char *name, apr_ssize_t name_len,
              const svn_string_t *val,
              apr_pool_t *pool)
{
  svn_dirent_t *entry = baton;

  if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    {
      entry->has_props = TRUE;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    {
      entry->has_props = TRUE;
    }
  else if (strcmp(ns, "DAV:") == 0)
    {
      if (strcmp(name, "version-name") == 0)
        {
          entry->created_rev = SVN_STR_TO_REV(val->data);
        }
      else if (strcmp(name, "creator-displayname") == 0)
        {
          entry->last_author = val->data;
        }
      else if (strcmp(name, "creationdate") == 0)
        {
          SVN_ERR(svn_time_from_cstring(&entry->time, val->data, pool));
        }
      else if (strcmp(name, "getcontentlength") == 0)
        {
          entry->size = apr_atoi64(val->data);
        }
      else if (strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(val->data, "collection") == 0)
            {
              entry->kind = svn_node_dir;
            }
          else
            {
              entry->kind = svn_node_file;
            }
        }
    }

  return SVN_NO_ERROR;
}

struct path_dirent_visitor_t {
  apr_hash_t *full_paths;
  apr_hash_t *base_paths;
  const char *orig_path;
};

static svn_error_t *
path_dirent_walker(void *baton,
                   const char *path, apr_ssize_t path_len,
                   const char *ns, apr_ssize_t ns_len,
                   const char *name, apr_ssize_t name_len,
                   const svn_string_t *val,
                   apr_pool_t *pool)
{
  struct path_dirent_visitor_t *dirents = baton;
  svn_dirent_t *entry;

  /* Skip our original path. */
  if (strcmp(path, dirents->orig_path) == 0)
    {
      return SVN_NO_ERROR;
    }

  entry = apr_hash_get(dirents->full_paths, path, path_len);

  if (!entry)
    {
      const char *base_name;

      entry = apr_pcalloc(pool, sizeof(*entry));

      apr_hash_set(dirents->full_paths, path, path_len, entry);

      base_name = svn_path_uri_decode(svn_path_basename(path, pool), pool);

      apr_hash_set(dirents->base_paths, base_name, APR_HASH_KEY_STRING, entry);
    }

  return dirent_walker(entry, ns, ns_len, name, name_len, val, pool);
}

static svn_error_t *
svn_ra_serf__stat(svn_ra_session_t *ra_session,
                  const char *rel_path,
                  svn_revnum_t revision,
                  svn_dirent_t **dirent,
                  apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  svn_ra_serf__propfind_context_t *prop_ctx;
  const char *path;
  svn_revnum_t fetched_rev;
  svn_dirent_t *entry;

  SVN_ERR(fetch_path_props(&prop_ctx, &props, &path, &fetched_rev,
                           session, rel_path, revision, all_props, pool));

  entry = apr_pcalloc(pool, sizeof(*entry));

  svn_ra_serf__walk_all_props(props, path, fetched_rev, dirent_walker, entry,
                              pool);

  *dirent = entry;

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_dir(svn_ra_session_t *ra_session,
                     apr_hash_t **dirents,
                     svn_revnum_t *fetched_rev,
                     apr_hash_t **ret_props,
                     const char *rel_path,
                     svn_revnum_t revision,
                     apr_uint32_t dirent_fields,
                     apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *path;

  path = session->repos_url.path;

  /* If we have a relative path, append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component(path, rel_path, pool);
    }

  props = apr_hash_make(pool);

  if (SVN_IS_VALID_REVNUM(revision) || fetched_rev)
    {
      const char *vcc_url, *relative_url, *basecoll_url;

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, &relative_url,
                                         session, session->conns[0],
                                         path, pool));

      SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                          vcc_url, revision,
                                          "0", baseline_props, pool));
      
      basecoll_url = svn_ra_serf__get_ver_prop(props, vcc_url, revision,
                                               "DAV:", "baseline-collection");
      
      if (!basecoll_url)
        {
          abort();
        }

      if (fetched_rev)
       {
         *fetched_rev = revision;
       }

      path = svn_path_url_add_component(basecoll_url, relative_url, pool);
      revision = SVN_INVALID_REVNUM;
    }

  /* If we're asked for children, fetch them now. */
  if (dirents)
    {
      svn_ra_serf__propfind_context_t *prop_ctx;
      struct path_dirent_visitor_t dirent_walk;

      prop_ctx = NULL;
      svn_ra_serf__deliver_props(&prop_ctx, props, session, session->conns[0],
                                 path, revision, "1", all_props, TRUE,
                                 NULL, session->pool);
      
      SVN_ERR(svn_ra_serf__wait_for_props(prop_ctx, session, pool));

      /* We're going to create two hashes to help the walker along.
       * We're going to return the 2nd one back to the caller as it
       * will have the basenames it expects.
       */
      dirent_walk.full_paths = apr_hash_make(pool);
      dirent_walk.base_paths = apr_hash_make(pool);
      dirent_walk.orig_path = svn_path_canonicalize(path, pool);

      svn_ra_serf__walk_all_paths(props, revision, path_dirent_walker,
                                  &dirent_walk, pool);

      *dirents = dirent_walk.base_paths;
    }

  /* If we're asked for the directory properties, fetch them too. */
  if (ret_props)
    {
      props = apr_hash_make(pool);
      *ret_props = apr_hash_make(pool);

      SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                          path, revision, "0", all_props,
                                          pool));
      svn_ra_serf__walk_all_props(props, path, revision,
                                  svn_ra_serf__set_flat_props,
                                  *ret_props, pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_repos_root(svn_ra_session_t *ra_session,
                            const char **url,
                            apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  if (!session->repos_root_str)
    {
      const char *vcc_url;

      SVN_ERR(svn_ra_serf__discover_root(&vcc_url, NULL,
                                         session, session->conns[0],
                                         session->repos_url.path, pool));
    }

  *url = session->repos_root_str;
  return SVN_NO_ERROR;
}

static svn_error_t *
svn_ra_serf__get_uuid(svn_ra_session_t *ra_session,
                      const char **uuid,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  const char *root_url;

  props = apr_hash_make(pool);

  svn_ra_serf__get_repos_root(ra_session, &root_url, pool);

  SVN_ERR(svn_ra_serf__retrieve_props(props, session, session->conns[0],
                                      root_url, SVN_INVALID_REVNUM, "0",
                                      uuid_props, pool));
  *uuid = svn_ra_serf__get_prop(props, root_url,
                                SVN_DAV_PROP_NS_DAV, "repository-uuid");

  if (!*uuid)
    {
      abort();
    }

  return SVN_NO_ERROR;
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
