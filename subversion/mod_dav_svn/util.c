/*
 * util.c: some handy utilities functions
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <apr_uri.h>
#include <mod_dav.h>

#include "svn_error.h"
#include "svn_fs.h"
#include "svn_dav.h"

#include "dav_svn.h"



dav_error * dav_svn_convert_err(const svn_error_t *serr, int status,
                                const char *message)
{
    dav_error *derr;

    /* ### someday mod_dav_svn will send back 'rich' error tags, much
       finer grained than plain old svn_error_t's.  But for now, all
       svn_error_t's are marshalled to the client via the single
       generic <svn:error/> tag nestled within a <D:error> block. */

    /* Even though the caller passed in some HTTP status code, we
       should look at the actual subversion error code and use the
       -best- HTTP mapping we can. */
    switch (serr->apr_err)
      {
      case SVN_ERR_FS_NOT_FOUND:
        status = HTTP_NOT_FOUND;
        break;
      case SVN_ERR_UNSUPPORTED_FEATURE:
        status = HTTP_NOT_IMPLEMENTED;
        break;
        /* add other mappings here */
      }

    derr = dav_new_error_tag(serr->pool, status,
                             serr->apr_err, serr->message,
                             SVN_DAV_ERROR_NAMESPACE,
                             SVN_DAV_ERROR_TAG);
    if (message != NULL)
        derr = dav_push_error(serr->pool, status, serr->apr_err,
                              message, derr);
    return derr;
}


svn_revnum_t dav_svn_get_safe_cr(svn_fs_root_t *root,
                                 const char *path,
                                 apr_pool_t *pool)
{
  svn_revnum_t revision = svn_fs_revision_root_revision(root);    
  svn_revnum_t created_rev;
  svn_fs_root_t *other_root;
  const svn_fs_id_t *id, *other_id;

  if (svn_fs_node_id(&id, root, path, pool))
    return revision;   /* couldn't get id of root/path */

  if (svn_fs_node_created_rev(&created_rev, root, path, pool))
    return revision;   /* couldn't find created_rev */
  
  if (svn_fs_revision_root(&other_root, svn_fs_root_fs(root),
                           created_rev, pool))
    return revision;   /* couldn't open the created rev */

  if (svn_fs_node_id(&other_id, other_root, path, pool))
    return revision;   /* couldn't get id of other_root/path */

  if (svn_fs_compare_ids(id, other_id) == 0)
    return created_rev;  /* the created_rev is safe!  the same node
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
  const char *path_uri = path ? svn_path_uri_encode (path, pool) : NULL;
  const char *href1 = add_href ? "<D:href>" : "";
  const char *href2 = add_href ? "</D:href>" : "";

  switch (what)
    {
    case DAV_SVN_BUILD_URI_ACT_COLLECTION:
      return apr_psprintf(pool, "%s%s/%s/act/%s",
                          href1, root_path, special_uri, href2);

    case DAV_SVN_BUILD_URI_BASELINE:
      return apr_psprintf(pool, "%s%s/%s/bln/%" SVN_REVNUM_T_FMT "%s",
                          href1, root_path, special_uri, revision, href2);

    case DAV_SVN_BUILD_URI_BC:
      return apr_psprintf(pool, "%s%s/%s/bc/%" SVN_REVNUM_T_FMT "/%s",
                          href1, root_path, special_uri, revision, href2);

    case DAV_SVN_BUILD_URI_PUBLIC:
      return apr_psprintf(pool, "%s%s%s%s",
                          href1, root_path, path_uri, href2);

    case DAV_SVN_BUILD_URI_VERSION:
      return apr_psprintf(pool, "%s%s/%s/ver/%" SVN_REVNUM_T_FMT "%s%s",
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
  char *path;
  apr_size_t len1;
  apr_size_t len2;
  const char *slash;
  const char *created_rev_str;

  /* parse the input URI, in case it is more than just a path */
  if (apr_uri_parse(pool, uri, &comp) != APR_SUCCESS)
    goto malformed_uri;

  /* ### ignore all URI parts but the path (for now) */

  path = comp.path;

  /* clean up the URI */
  ap_getparents(path);
  ap_no2slash(path);

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
  if (len1 < len2
      || (len1 > len2 && path[len2] != '/')
      || memcmp(path, relative->info->repos->root_path, len2) != 0)
    {
      return svn_error_create(SVN_ERR_APMOD_MALFORMED_URI, 0, NULL, pool,
                              "The specified URI does not refer to this "
                              "repository, so it is unusable.");
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
      info->repos_path = path - 1;
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
          info->repos_path = slash;
        }
      if (info->rev == SVN_INVALID_REVNUM)
        goto malformed_uri;
    }
  else
    goto unhandled_form;

  return NULL;

 malformed_uri:
    return svn_error_create(SVN_ERR_APMOD_MALFORMED_URI, 0, NULL, pool,
                            "The specified URI could not be parsed.");

 unhandled_form:
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, 0, NULL, pool,
                          "dav_svn_simple_parse_uri does not support that "
                          "URI form yet.");
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



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
