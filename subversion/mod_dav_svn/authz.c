/*
 * authz.c: authorization related code
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

#include <http_request.h>
#include <http_log.h>
#include <http_protocol.h>

#include "svn_pools.h"
#include "svn_path.h"

#include "svn_dav.h"
#include "dav_svn.h"


/* Convert incoming REV and PATH from request R into a version-resource URI
   for REPOS and perform a GET subrequest on it.  This will invoke any authz
   modules loaded into apache.  Return TRUE if the subrequest succeeds, FALSE
   otherwise. If REV is SVN_INVALID_REVNUM, then we look at HEAD.
*/
static svn_boolean_t
allow_read(request_rec *r,
           const dav_svn_repos *repos,
           const char *path,
           svn_revnum_t rev,
           apr_pool_t *pool)
{
  const char *uri;
  request_rec *subreq;
  enum dav_svn__build_what uri_type;
  svn_boolean_t allowed = FALSE;

  /* Easy out:  if the admin has explicitly set 'SVNPathAuthz Off',
     then this whole callback does nothing. */
  if (! dav_svn__get_pathauthz_flag(r))
    {
      return TRUE;
    }

  /* If no revnum is specified, assume HEAD. */
  if (SVN_IS_VALID_REVNUM(rev))
    uri_type = DAV_SVN__BUILD_URI_VERSION;
  else
    uri_type = DAV_SVN__BUILD_URI_PUBLIC;

  /* Build a Version Resource uri representing (rev, path). */
  uri = dav_svn__build_uri(repos, uri_type, rev, path, FALSE, pool);

  if (dav_svn__get_native_authz_file(r))
    {
      /* Do native auhorization lookup - read access */
      dav_error *err = dav_svn__check_access(repos->repo_name,
                                             path,
                                             r,
                                             svn_authz_read);

      if (! err)
        allowed = TRUE;

      /* XXX: need to cleanup dav_error? */
    }
  else
    {
      /* Check if GET would work against this uri. */
      subreq = ap_sub_req_method_uri("GET", uri, r, r->output_filters);

      if (subreq)
        {
          if (subreq->status == HTTP_OK)
            allowed = TRUE;

          ap_destroy_sub_req(subreq);
        }
    }

  return allowed;
}


/* This function implements 'svn_repos_authz_func_t', specifically
   for read authorization.

   Convert incoming ROOT and PATH into a version-resource URI and
   perform a GET subrequest on it.  This will invoke any authz modules
   loaded into apache.  Set *ALLOWED to TRUE if the subrequest
   succeeds, FALSE otherwise.

   BATON must be a pointer to a dav_svn__authz_read_baton.
   Use POOL for for any temporary allocation.
*/
static svn_error_t *
authz_read(svn_boolean_t *allowed,
           svn_fs_root_t *root,
           const char *path,
           void *baton,
           apr_pool_t *pool)
{
  dav_svn__authz_read_baton *arb = baton;
  svn_revnum_t rev = SVN_INVALID_REVNUM;
  const char *revpath = NULL;

  /* Our ultimate goal here is to create a Version Resource (VR) url,
     which is a url that represents a path within a revision.  We then
     send a subrequest to apache, so that any installed authz modules
     can allow/disallow the path.

     ### That means that we're assuming that any installed authz
     module is *only* paying attention to revision-paths, not paths in
     uncommitted transactions.  Someday we need to widen our horizons. */

  if (svn_fs_is_txn_root(root))
    {
      /* This means svn_repos_dir_delta is comparing two txn trees,
         rather than a txn and revision.  It's probably updating a
         working copy that contains 'disjoint urls'.  

         Because the 2nd transaction is likely to have all sorts of
         paths linked in from random places, we need to find the
         original (rev,path) of each txn path.  That's what needs
         authorization.  */

      svn_stringbuf_t *path_s = svn_stringbuf_create(path, pool);
      const char *lopped_path = "";
      
      /* The path might be copied implicitly, because it's down in a
         copied tree.  So we start at path and walk up its parents
         asking if anyone was copied, and if so where from.  */
      while (! (svn_path_is_empty(path_s->data)
                || ((path_s->len == 1) && (path_s->data[0] == '/'))))
        {
          SVN_ERR(svn_fs_copied_from(&rev, &revpath, root,
                                     path_s->data, pool));

          if (SVN_IS_VALID_REVNUM(rev) && revpath)
            {
              revpath = svn_path_join(revpath, lopped_path, pool);
              break;
            }
          
          /* Lop off the basename and try again. */
          lopped_path = svn_path_join(svn_path_basename
                                      (path_s->data, pool), lopped_path, pool);
          svn_path_remove_component(path_s);
        }

      /* If no copy produced this path, its path in the original
         revision is the same as its path in this txn. */
      if ((rev == SVN_INVALID_REVNUM) && (revpath == NULL))
        {
          rev = svn_fs_txn_root_base_revision(root);
          revpath = path;
        }
    }
  else  /* revision root */
    {
      rev = svn_fs_revision_root_revision(root);
      revpath = path;
    }

  /* We have a (rev, path) pair to check authorization on. */
  *allowed = allow_read(arb->r, arb->repos, revpath, rev, pool);
  
  return SVN_NO_ERROR;
}


svn_repos_authz_func_t
dav_svn__authz_read_func(dav_svn__authz_read_baton *baton)
{
  /* Easy out: If the admin has explicitly set 'SVNPathAuthz Off',
     then we don't need to do any authorization checks. */
  if (! dav_svn__get_pathauthz_flag(baton->r))
    return NULL;

  return authz_read;
}


svn_boolean_t
dav_svn__allow_read(const dav_resource *resource,
                   svn_revnum_t rev,
                   apr_pool_t *pool)
{
  return allow_read(resource->info->r, resource->info->repos,
                    resource->info->repos_path, rev, pool);
}


/* Native path-based authorization */

static int
check_access(const char *repos_name,
             const char *repos_path,
             request_rec* r,
             svn_repos_authz_access_t required_access)
{
  const char *authz_file = NULL;
  svn_authz_t *access_conf = NULL;
  svn_error_t *svn_err;
  const char *cache_key;
  void *user_data;
  svn_boolean_t access_granted;
  char errbuf[128];

  /* If native authz is off, there's nothing to do. Return DONE
   * instead of OK to indicate that no checks have really been done.
   */
  if (! dav_svn__get_native_authz_file(r))
    return DONE;

  authz_file = dav_svn__get_native_authz_file(r);
  /* If access file had not been specified, the default
     behavior is to allow access. 
     XXX: is this right? */
  if (authz_file == NULL)
    return OK;

  /* Retrieve/cache authorization file */
  cache_key = apr_pstrcat(r->pool, "mod_dav_svn:", authz_file, NULL);
  apr_pool_userdata_get(&user_data, cache_key, r->connection->pool);
  access_conf = user_data;
  if (access_conf == NULL)
    {
      svn_err = svn_repos_authz_read(&access_conf, authz_file,
                                     TRUE, r->connection->pool);
      if (svn_err)
        {
          ap_log_rerror(APLOG_MARK, APLOG_ERR,
                        /* If it is an error code that APR can make sense
                           of, then show it, otherwise, pass zero to avoid
                           putting "APR does not understand this error code"
                           in the error log. */
                        ((svn_err->apr_err >= APR_OS_START_USERERR &&
                          svn_err->apr_err < APR_OS_START_CANONERR) ?
                         0 : svn_err->apr_err),
                        r, "Failed to load the SVNNativeAuthzFile: %s",
                        svn_err_best_message(svn_err,
                                             errbuf, sizeof(errbuf)));
          svn_error_clear(svn_err);

          return DECLINED;
        }

      /* Cache the open repos for the next request on this connection */
      apr_pool_userdata_set(access_conf, cache_key,
                            NULL, r->connection->pool);
  }

  /* Perform authz access control. */
  svn_err = svn_repos_authz_check_access(access_conf, repos_name,
                                         repos_path, r->user,
                                         required_access,
                                         &access_granted,
                                         r->pool);

  if (svn_err)
    {
      ap_log_rerror(APLOG_MARK, APLOG_ERR,
                    /* If it is an error code that APR can make
                       sense of, then show it, otherwise, pass
                       zero to avoid putting "APR does not
                       understand this error code" in the error
                       log. */
                    ((svn_err->apr_err >= APR_OS_START_USERERR &&
                      svn_err->apr_err < APR_OS_START_CANONERR) ?
                     0 : svn_err->apr_err),
                    r, "Failed to perform access control: %s",
                    svn_err_best_message(svn_err, errbuf, sizeof(errbuf)));
      svn_error_clear(svn_err);

      return DECLINED;
    }

  if (! access_granted)
    return DECLINED;

  return OK;
}


/* Log a message indicating the access control decision made about a
   request.  FILE and LINE should be supplied via the APLOG_MARK macro.
   ALLOWED is boolean.  REPOS_PATH and DEST_REPOS_PATH are information
   about the request.  DEST_REPOS_PATH may be NULL. */
static void
log_access_verdict(const char *file, int line,
                   const request_rec *r,
                   int allowed,
                   const char *repos_path,
                   svn_repos_authz_access_t required_access)
{
  int level = allowed ? APLOG_INFO : APLOG_ERR;
  const char *verdict = allowed ? "granted" : "denied";

  char access_str[4] = { 0, 0, 0, 0 };
  int access_idx = 0;

  if (required_access & svn_authz_read)
    access_str[access_idx++] = 'r';

  if (required_access & svn_authz_write)
    access_str[access_idx++] = 'w';

  if (required_access & svn_authz_recursive)
    access_str[access_idx++] = 'R';

  if (repos_path == NULL)
    repos_path = "<global>";

  if (r->user)
  {
    ap_log_rerror(file, line, level, 0, r,
                  "[native] Access %s: '%s' %s %s %s", verdict, r->user,
                  r->method, repos_path, access_str);
  }
  else
  {
    ap_log_rerror(file, line, level, 0, r,
                  "[native] Access %s: - %s %s %s", verdict,
                  r->method, repos_path, access_str);
  }
}


dav_error *
dav_svn__check_access(const char *repos_name,
                      const char *repos_path,
                      request_rec *r,
                      svn_repos_authz_access_t required_access)
{
  int status;

  status = check_access(repos_name, repos_path, r, required_access);

  /* If no checks had been done, native authz is off, so don't log
   * a possibly misleading authorization verdict.
   */
  if (status == DONE)
    return NULL;

  if(status == DECLINED)
  {
    log_access_verdict(APLOG_MARK, r, 0, repos_path, required_access);
    ap_note_auth_failure(r); // XXX: need this?

    // XXX: need better error message
    return dav_svn__new_error_tag(r->pool, HTTP_FORBIDDEN, 0,
                                  "Insufficient rights to access resource.",
                                  SVN_DAV_ERROR_NAMESPACE,
                                  SVN_DAV_ERROR_TAG);
  }

  log_access_verdict(APLOG_MARK, r, 1, repos_path, required_access);

  return NULL;
}


dav_error *
dav_svn__check_resource_access(const dav_resource *resource,
                               const svn_repos_authz_access_t required_access)
{
  return dav_svn__check_access(resource->info->repos->repo_name,
                               resource->info->repos_path,
                               resource->info->r,
                               required_access);
}


dav_error *
dav_svn__check_global_access(const dav_resource *resource,
                             const svn_repos_authz_access_t required_access)
{
  return dav_svn__check_access(resource->info->repos->repo_name,
                               NULL, /* global access */
                               resource->info->r,
                               required_access);
}
