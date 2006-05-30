/*
 * util.c: some handy utilities functions
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

#include <apr_xml.h>
#include <apr_errno.h>
#include <apr_uri.h>
#include <mod_dav.h>

#include "svn_error.h"
#include "svn_fs.h"
#include "svn_dav.h"

#include "dav_svn.h"

dav_error * dav_svn__new_error_tag(apr_pool_t *pool,
                                   int status,
                                   int error_id,
                                   const char *desc,
                                   const char *namespace,
                                   const char *tagname)
{
  /* dav_new_error_tag will record errno but Subversion makes no attempt
     to ensure that it is valid.  We reset it to avoid putting incorrect
     information into the error log, at the expense of possibly removing
     valid information. */
  errno = 0;

  return dav_new_error_tag(pool, status, error_id, desc, namespace, tagname);
}

/* Build up a chain of DAV errors that correspond to the underlying SVN
   errors that caused this problem. */
static dav_error *build_error_chain(apr_pool_t *pool, svn_error_t *err,
                                    int status)
{
  char *msg = err->message ? apr_pstrdup(pool, err->message) : NULL;

  dav_error *derr = dav_svn__new_error_tag(pool, status, err->apr_err, msg,
                                           SVN_DAV_ERROR_NAMESPACE,
                                           SVN_DAV_ERROR_TAG);

  if (err->child)
    derr->prev = build_error_chain(pool, err->child, status);

  return derr;
}

dav_error * dav_svn_convert_err(svn_error_t *serr, int status,
                                const char *message, apr_pool_t *pool)
{
    dav_error *derr;

    /* ### someday mod_dav_svn will send back 'rich' error tags, much
       finer grained than plain old svn_error_t's.  But for now, all
       svn_error_t's are marshalled to the client via the single
       generic <svn:error/> tag nestled within a <D:error> block. */

    /* Examine the Subverion error code, and select the most
       appropriate HTTP status code.  If no more appropriate HTTP
       status code maps to the Subversion error code, use the one
       suggested status provided by the caller. */
    switch (serr->apr_err)
      {
      case SVN_ERR_FS_NOT_FOUND:
        status = HTTP_NOT_FOUND;
        break;
      case SVN_ERR_UNSUPPORTED_FEATURE:
        status = HTTP_NOT_IMPLEMENTED;
        break;
      case SVN_ERR_FS_PATH_ALREADY_LOCKED:
        status = HTTP_LOCKED;
        break;
        /* add other mappings here */
      }

    derr = build_error_chain(pool, serr, status);
    if (message != NULL)
      derr = dav_push_error(pool, status, serr->apr_err, message, derr);

    /* Now, destroy the Subversion error. */
    svn_error_clear(serr);

    return derr;
}


/* Set *REVISION to the youngest revision in which an interesting
   history item (a modification, or a copy) occurred for PATH under
   ROOT.  Use POOL for scratchwork. */
static svn_error_t *
get_last_history_rev(svn_revnum_t *revision,
                     svn_fs_root_t *root,
                     const char *path,
                     apr_pool_t *pool)
{
  svn_fs_history_t *history;
  const char *ignored;

  /* Get an initial HISTORY baton. */
  SVN_ERR(svn_fs_node_history(&history, root, path, pool));

  /* Now get the first *real* point of interesting history. */
  SVN_ERR(svn_fs_history_prev(&history, history, FALSE, pool));

  /* Fetch the location information for this history step. */
  return svn_fs_history_location(&ignored, revision, history, pool);
}


svn_revnum_t dav_svn_get_safe_cr(svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool)
{
  svn_revnum_t revision = svn_fs_revision_root_revision(root);    
  svn_revnum_t history_rev;
  svn_fs_root_t *other_root;
  svn_fs_t *fs = svn_fs_root_fs(root);
  const svn_fs_id_t *id, *other_id;
  svn_error_t *err;

  if ((err = svn_fs_node_id(&id, root, path, pool)))
    {
      svn_error_clear(err);
      return revision;   /* couldn't get id of root/path */
    }

  if ((err = get_last_history_rev(&history_rev, root, path, pool)))
    {
      svn_error_clear(err);
      return revision;   /* couldn't find last history rev */
    }
  
  if ((err = svn_fs_revision_root(&other_root, fs, history_rev, pool)))
    {
      svn_error_clear(err);
      return revision;   /* couldn't open the history rev */
    }

  if ((err = svn_fs_node_id(&other_id, other_root, path, pool)))
    {
      svn_error_clear(err);
      return revision;   /* couldn't get id of other_root/path */
    }

  if (svn_fs_compare_ids(id, other_id) == 0)
    return history_rev;  /* the history rev is safe!  the same node
                            exists at the same path in both revisions. */    

  /* default */
  return revision;
}
                                   


const char *dav_svn_build_uri(const dav_svn_repos *repos,
                              enum dav_svn_build_what what,
                              svn_revnum_t revision,
                              const char *path,
                              int add_href,
                              apr_pool_t *pool)
{
  const char *root_path = repos->root_path;
  const char *special_uri = repos->special_uri;
  const char *path_uri = path ? svn_path_uri_encode(path, pool) : NULL;
  const char *href1 = add_href ? "<D:href>" : "";
  const char *href2 = add_href ? "</D:href>" : "";

  /* The first character of root_path is guaranteed to be "/".  If
     there's no component beyond that, then just use "", so that
     appending another "/" later does not result in "//". */
  if (root_path[1] == '\0')
    root_path = "";

  switch (what)
    {
    case DAV_SVN_BUILD_URI_ACT_COLLECTION:
      return apr_psprintf(pool, "%s%s/%s/act/%s",
                          href1, root_path, special_uri, href2);

    case DAV_SVN_BUILD_URI_BASELINE:
      return apr_psprintf(pool, "%s%s/%s/bln/%ld%s",
                          href1, root_path, special_uri, revision, href2);

    case DAV_SVN_BUILD_URI_BC:
      return apr_psprintf(pool, "%s%s/%s/bc/%ld/%s",
                          href1, root_path, special_uri, revision, href2);

    case DAV_SVN_BUILD_URI_PUBLIC:
      return apr_psprintf(pool, "%s%s%s%s",
                          href1, root_path, path_uri, href2);

    case DAV_SVN_BUILD_URI_VERSION:
      return apr_psprintf(pool, "%s%s/%s/ver/%ld%s%s",
                          href1, root_path, special_uri,
                          revision, path_uri, href2);

    case DAV_SVN_BUILD_URI_VCC:
      return apr_psprintf(pool, "%s%s/%s/vcc/" DAV_SVN_DEFAULT_VCC_NAME "%s",
                          href1, root_path, special_uri, href2);

    default:
      /* programmer error somewhere */
      abort();
      return NULL;
    }

  /* NOTREACHED */
}

svn_error_t *dav_svn_simple_parse_uri(dav_svn_uri_info *info,
                                      const dav_resource *relative,
                                      const char *uri,
                                      apr_pool_t *pool)
{
  apr_uri_t comp;
  const char *path;
  apr_size_t len1;
  apr_size_t len2;
  const char *slash;
  const char *created_rev_str;

  /* parse the input URI, in case it is more than just a path */
  if (apr_uri_parse(pool, uri, &comp) != APR_SUCCESS)
    goto malformed_uri;

  /* ### ignore all URI parts but the path (for now) */

  /* clean up the URI */
  if (comp.path == NULL)
    path = "/";
  else
    {
      ap_getparents(comp.path);
      ap_no2slash(comp.path);
      path = comp.path;
    }

  /*
   * Does the URI path specify the same repository? It does not if one of:
   *
   * 1) input is shorter than the path to our repository
   * 2) input is longer, but there is no separator
   *    [ http://host/repos vs http://host/repository ]
   * 3) the two paths do not match
   */
  len1 = strlen(path);
  len2 = strlen(relative->info->repos->root_path);
  if (len2 == 1 && relative->info->repos->root_path[0] == '/')
    len2 = 0;

  if (len1 < len2
      || (len1 > len2 && path[len2] != '/')
      || memcmp(path, relative->info->repos->root_path, len2) != 0)
    {
      return svn_error_create(SVN_ERR_APMOD_MALFORMED_URI, NULL,
                              "Unusable URI: it does not refer to this "
                              "repository");
    }

  /* prep the return value */
  memset(info, 0, sizeof(*info));
  info->rev = SVN_INVALID_REVNUM;

  path += len2; /* now points to "/" or "\0" */
  len1 -= len2;

  if (len1 <= 1)
    {
      info->repos_path = "/";
      return NULL;
    }

  /* skip over the leading "/" */
  ++path;
  --len1;

  /* is this a special URI? */
  len2 = strlen(relative->info->repos->special_uri);
  if (len1 < len2
      || (len1 > len2 && path[len2] != '/')
      || memcmp(path, relative->info->repos->special_uri, len2) != 0)
    {
      /* this is an ordinary "public" URI, so back up to include the
         leading '/' and just return... no need to parse further. */
      info->repos_path = svn_path_uri_decode(path - 1, pool);
      return NULL;
    }

  path += len2; /* now points to "/" or "\0" just past the special URI */
  len1 -= len2;

  /* ### we don't handle the root of the special area yet */
  if (len1 <= 1)
    goto unhandled_form;

  /* Find the next component, and ensure something is there. */
  slash = ap_strchr_c(path + 1, '/');
  if (slash == NULL || slash[1] == '\0')
    goto unhandled_form;
  len2 = slash - path;

  /* Figure out what we have here */
  if (len2 == 4 && memcmp(path, "/act/", 5) == 0)
    {
      /* an activity */
      info->activity_id = path + 5;
    }
  else if (len2 == 4 && memcmp(path, "/ver/", 5) == 0)
    {      
      /* a version resource */
      path += 5;
      len1 -= 5;
      slash = ap_strchr_c(path, '/');
      if (slash == NULL)
        {
          created_rev_str = apr_pstrndup(pool, path, len1);
          info->rev = SVN_STR_TO_REV(created_rev_str);
          info->repos_path = "/";
        }
      else
        {
          created_rev_str = apr_pstrndup(pool, path, slash - path);
          info->rev = SVN_STR_TO_REV(created_rev_str);
          info->repos_path = svn_path_uri_decode(slash, pool);
        }
      if (info->rev == SVN_INVALID_REVNUM)
        goto malformed_uri;
    }
  else
    goto unhandled_form;

  return NULL;

 malformed_uri:
    return svn_error_create(SVN_ERR_APMOD_MALFORMED_URI, NULL,
                            "The specified URI could not be parsed");

 unhandled_form:
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                          "Unsupported URI form");
}

/* ### move this into apr_xml */
int dav_svn_find_ns(apr_array_header_t *namespaces, const char *uri)
{
  int i;

  for (i = 0; i < namespaces->nelts; ++i)
    if (strcmp(APR_XML_GET_URI_ITEM(namespaces, i), uri) == 0)
      return i;
  return -1;
}

svn_error_t * dav_svn__send_xml(apr_bucket_brigade *bb, ap_filter_t *output,
                                const char *fmt, ...)
{
  apr_status_t apr_err;
  va_list ap;

  va_start(ap, fmt);
  apr_err = apr_brigade_vprintf(bb, ap_filter_flush, output, fmt, ap);
  va_end(ap);
  if (apr_err)
    return svn_error_create(apr_err, 0, NULL);
  /* ### check for an aborted connection, since the brigade functions
     don't appear to be return useful errors when the connection is
     dropped. */
  if (output->c->aborted)
    return svn_error_create(SVN_ERR_APMOD_CONNECTION_ABORTED, 0, NULL);
  return SVN_NO_ERROR;
}


dav_error * dav_svn__test_canonical(const char *path, apr_pool_t *pool)
{
  if (strcmp(path, svn_path_canonicalize(path, pool)) == 0)
    return NULL;

  /* Otherwise, generate a generic HTTP_BAD_REQUEST error. */
  return dav_svn__new_error_tag
    (pool, HTTP_BAD_REQUEST, 0, 
     apr_psprintf(pool, 
                  "Path '%s' is not canonicalized; "
                  "there is a problem with the client.", path),
     SVN_DAV_ERROR_NAMESPACE, SVN_DAV_ERROR_TAG);
}

dav_error * dav_svn__sanitize_error(svn_error_t *serr,
                                    const char *new_msg,
                                    int http_status,
                                    request_rec *r)
{
    svn_error_t *safe_err = serr;
    if (new_msg != NULL)
      {
        /* Sanitization is necessary.  Create a new, safe error and
           log the original error. */
        safe_err = svn_error_create(serr->apr_err, NULL, new_msg);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, r,
                      "%s", serr->message);
        svn_error_clear(serr);
      }
    return dav_svn_convert_err(safe_err, http_status,
                               apr_psprintf(r->pool, safe_err->message),
                               r->pool);
}
