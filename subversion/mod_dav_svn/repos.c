/*
 * repos.c: mod_dav_svn repository provider functions for Subversion
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



#include <httpd.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_core.h>  /* for ap_construct_url */
#include <mod_dav.h>

#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_dav.h"
#include "svn_sorts.h"
#include "svn_version.h"

#include "dav_svn.h"


struct dav_stream {
  const dav_resource *res;

  /* for reading from the FS */
  svn_stream_t *rstream;

  /* for writing to the FS. we use wstream OR the handler/baton. */
  svn_stream_t *wstream;
  svn_txdelta_window_handler_t delta_handler;
  void *delta_baton;
};

typedef struct {
  ap_filter_t *output;
  apr_pool_t *pool;
} dav_svn_diff_ctx_t;

typedef struct {
  dav_resource res;
  dav_resource_private priv;
} dav_resource_combined;

/* private context for doing a walk */
typedef struct {
  /* the input walk parameters */
  const dav_walk_params *params;

  /* reused as we walk */
  dav_walk_resource wres;

  /* the current resource */
  dav_resource res;             /* wres.resource refers here */
  dav_resource_private info;    /* the info in res */
  svn_stringbuf_t *uri;            /* the uri within res */
  svn_stringbuf_t *repos_path;     /* the repos_path within res */

} dav_svn_walker_context;


static int dav_svn_parse_version_uri(dav_resource_combined *comb,
                                     const char *path,
                                     const char *label,
                                     int use_checked_in)
{
  const char *slash;
  const char *created_rev_str;

  /* format: CREATED_REV/REPOS_PATH */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  comb->res.type = DAV_RESOURCE_TYPE_VERSION;
  comb->res.versioned = TRUE;

  slash = ap_strchr_c(path, '/');
  if (slash == NULL)
    {
      /* http://host.name/repos/$svn/ver/0

         This URL form refers to the root path of the repository.
      */
      created_rev_str = apr_pstrndup(comb->res.pool, path, strlen(path));
      comb->priv.root.rev = SVN_STR_TO_REV(created_rev_str);
      comb->priv.repos_path = "/";
    }
  else if (slash == path)
    {
      /* the CREATED_REV was missing(?)

         ### not sure this can happen, though, because it would imply two
         ### slashes, yet those are cleaned out within get_resource
      */
      return TRUE;
    }
  else
    {
      apr_size_t len = slash - path;

      created_rev_str = apr_pstrndup(comb->res.pool, path, len);
      comb->priv.root.rev = SVN_STR_TO_REV(created_rev_str);
      comb->priv.repos_path = slash;
    }

  /* if the CREATED_REV parsing blew, then propagate it. */
  if (comb->priv.root.rev == SVN_INVALID_REVNUM)
    return TRUE;

  return FALSE;
}

static int dav_svn_parse_history_uri(dav_resource_combined *comb,
                                     const char *path,
                                     const char *label,
                                     int use_checked_in)
{
  /* format: ??? */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  comb->res.type = DAV_RESOURCE_TYPE_HISTORY;

  /* ### parse path */
  comb->priv.repos_path = path;

  return FALSE;
}

static int dav_svn_parse_working_uri(dav_resource_combined *comb,
                                     const char *path,
                                     const char *label,
                                     int use_checked_in)
{
  const char *slash;

  /* format: ACTIVITY_ID/REPOS_PATH */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  comb->res.type = DAV_RESOURCE_TYPE_WORKING;
  comb->res.working = TRUE;
  comb->res.versioned = TRUE;

  slash = ap_strchr_c(path, '/');

  /* This sucker starts with a slash.  That's bogus. */
  if (slash == path)
    return TRUE;

  if (slash == NULL)
    {
      /* There's no slash character in our path.  Assume it's just an
         ACTIVITY_ID pointing to the root path.  That should be cool.
         We'll just drop through to the normal case handling below. */
      comb->priv.root.activity_id = apr_pstrdup(comb->res.pool, path);
      comb->priv.repos_path = "/";
    }
  else
    {
      comb->priv.root.activity_id = apr_pstrndup(comb->res.pool, path,
                                                 slash - path);
      comb->priv.repos_path = slash;
    }

  return FALSE;
}

static int dav_svn_parse_activity_uri(dav_resource_combined *comb,
                                      const char *path,
                                      const char *label,
                                      int use_checked_in)
{
  /* format: ACTIVITY_ID */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  comb->res.type = DAV_RESOURCE_TYPE_ACTIVITY;

  comb->priv.root.activity_id = path;

  return FALSE;
}

static int dav_svn_parse_vcc_uri(dav_resource_combined *comb,
                                 const char *path,
                                 const char *label,
                                 int use_checked_in)
{
  /* format: "default" (a singleton) */

  if (strcmp(path, DAV_SVN_DEFAULT_VCC_NAME) != 0)
    return TRUE;

  if (label == NULL && !use_checked_in)
    {
      /* Version Controlled Configuration (baseline selector) */

      /* ### mod_dav has a proper model for these. technically, they are
         ### version-controlled resources (REGULAR), but that just monkeys
         ### up a lot of stuff for us. use a PRIVATE for now. */

      comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;   /* _REGULAR */
      comb->priv.restype = DAV_SVN_RESTYPE_VCC;

      comb->res.exists = TRUE;
      comb->res.versioned = TRUE;
      comb->res.baselined = TRUE;

      /* NOTE: comb->priv.repos_path == NULL */
    }
  else
    {
      /* a specific Version Resource; in this case, a Baseline */

      int revnum;

      if (label != NULL)
        {
          revnum = SVN_STR_TO_REV(label); /* assume slash terminates */
          if (!SVN_IS_VALID_REVNUM(revnum))
            return TRUE;        /* ### be nice to get better feedback */
        }
      else /* use_checked_in */
        {
          /* use the DAV:checked-in value of the VCC. this is always the
             "latest" (or "youngest") revision. */

          /* signal dav_svn_prep_version to look it up */
          revnum = SVN_INVALID_REVNUM;
        }

      comb->res.type = DAV_RESOURCE_TYPE_VERSION;

      /* exists? need to wait for now */
      comb->res.versioned = TRUE;
      comb->res.baselined = TRUE;

      /* which baseline (revision tree) to access */
      comb->priv.root.rev = revnum;

      /* NOTE: comb->priv.repos_path == NULL */
      /* NOTE: comb->priv.created_rev == SVN_INVALID_REVNUM */
    }

  return FALSE;
}

static int dav_svn_parse_baseline_coll_uri(dav_resource_combined *comb,
                                           const char *path,
                                           const char *label,
                                           int use_checked_in)
{
  const char *slash;
  int revnum;

  /* format: REVISION/REPOS_PATH */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  slash = ap_strchr_c(path, '/');
  if (slash == NULL)
    slash = "/";        /* they are referring to the root of the BC */
  else if (slash == path)
    return TRUE;        /* the REVISION was missing(?)
                           ### not sure this can happen, though, because
                           ### it would imply two slashes, yet those are
                           ### cleaned out within get_resource */

  revnum = SVN_STR_TO_REV(path);  /* assume slash terminates conversion */
  if (!SVN_IS_VALID_REVNUM(revnum))
    return TRUE;        /* ### be nice to get better feedback */

  /* ### mod_dav doesn't have a proper model for these. they are standard
     ### VCRs, but we need some additional semantics attached to them.
     ### need to figure out a way to label them as special. */

  comb->res.type = DAV_RESOURCE_TYPE_REGULAR;
  comb->res.versioned = TRUE;
  comb->priv.root.rev = revnum;
  comb->priv.repos_path = slash;

  return FALSE;
}

static int dav_svn_parse_baseline_uri(dav_resource_combined *comb,
                                      const char *path,
                                      const char *label,
                                      int use_checked_in)
{
  int revnum;

  /* format: REVISION */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  revnum = SVN_STR_TO_REV(path);
  if (!SVN_IS_VALID_REVNUM(revnum))
    return TRUE;        /* ### be nice to get better feedback */

  /* create a Baseline resource (a special Version Resource) */

  comb->res.type = DAV_RESOURCE_TYPE_VERSION;

  /* exists? need to wait for now */
  comb->res.versioned = TRUE;
  comb->res.baselined = TRUE;

  /* which baseline (revision tree) to access */
  comb->priv.root.rev = revnum;

  /* NOTE: comb->priv.repos_path == NULL */
  /* NOTE: comb->priv.created_rev == SVN_INVALID_REVNUM */

  return FALSE;
}

static int dav_svn_parse_wrk_baseline_uri(dav_resource_combined *comb,
                                          const char *path,
                                          const char *label,
                                          int use_checked_in)
{
  const char *slash;

  /* format: ACTIVITY_ID/REVISION */

  /* ### what to do with LABEL and USE_CHECKED_IN ?? */

  comb->res.type = DAV_RESOURCE_TYPE_WORKING;
  comb->res.working = TRUE;
  comb->res.versioned = TRUE;
  comb->res.baselined = TRUE;

  if ((slash = ap_strchr_c(path, '/')) == NULL
      || slash == path
      || slash[1] == '\0')
    return TRUE;

  comb->priv.root.activity_id = apr_pstrndup(comb->res.pool, path,
                                             slash - path);
  comb->priv.root.rev = SVN_STR_TO_REV(slash + 1);

  /* NOTE: comb->priv.repos_path == NULL */

  return FALSE;
}

static const struct special_defn
{
  const char *name;

  /*
   * COMB is the resource that we are constructing. Any elements that
   * can be determined from the PATH may be set in COMB. However, further
   * operations are not allowed (we don't want anything besides a parse
   * error to occur).
   *
   * At a minimum, the parse function must set COMB->res.type and
   * COMB->priv.repos_path.
   *
   * PATH does not contain a leading slash. Given "/root/$svn/xxx/the/path"
   * as the request URI, the PATH variable will be "the/path"
   */
  int (*parse)(dav_resource_combined *comb, const char *path,
               const char *label, int use_checked_in);

  /* The private resource type for the /$svn/xxx/ collection. */
  enum dav_svn_private_restype restype;

} special_subdirs[] =
{
  { "ver", dav_svn_parse_version_uri, DAV_SVN_RESTYPE_VER_COLLECTION },
  { "his", dav_svn_parse_history_uri, DAV_SVN_RESTYPE_HIS_COLLECTION },
  { "wrk", dav_svn_parse_working_uri, DAV_SVN_RESTYPE_WRK_COLLECTION },
  { "act", dav_svn_parse_activity_uri, DAV_SVN_RESTYPE_ACT_COLLECTION },
  { "vcc", dav_svn_parse_vcc_uri, DAV_SVN_RESTYPE_VCC_COLLECTION },
  { "bc", dav_svn_parse_baseline_coll_uri, DAV_SVN_RESTYPE_BC_COLLECTION },
  { "bln", dav_svn_parse_baseline_uri, DAV_SVN_RESTYPE_BLN_COLLECTION },
  { "wbl", dav_svn_parse_wrk_baseline_uri, DAV_SVN_RESTYPE_WBL_COLLECTION },

  { NULL } /* sentinel */
};

/*
 * dav_svn_parse_uri: parse the provided URI into its various bits
 *
 * URI will contain a path relative to our configured root URI. It should
 * not have a leading "/". The root is identified by "".
 *
 * SPECIAL_URI is the component of the URI path configured by the
 * SVNSpecialPath directive (defaults to "$svn").
 *
 * On output: *COMB will contain all of the information parsed out of
 * the URI -- the resource type, activity ID, path, etc.
 *
 * Note: this function will only parse the URI. Validation of the pieces,
 * opening data stores, etc, are not part of this function.
 *
 * TRUE is returned if a parsing error occurred. FALSE for success.
 */
static int dav_svn_parse_uri(dav_resource_combined *comb,
                             const char *uri,
                             const char *label,
                             int use_checked_in)
{
  const char *special_uri = comb->priv.repos->special_uri;
  apr_size_t len1;
  apr_size_t len2;
  char ch;

  len1 = strlen(uri);
  len2 = strlen(special_uri);
  if (len1 > len2
      && ((ch = uri[len2]) == '/' || ch == '\0')
      && memcmp(uri, special_uri, len2) == 0)
    {
      if (ch == '\0')
        {
          /* URI was "/root/$svn". It exists, but has restricted usage. */
          comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;
          comb->priv.restype = DAV_SVN_RESTYPE_ROOT_COLLECTION;
        }
      else
        {
          const struct special_defn *defn;

          /* skip past the "$svn/" prefix */
          uri += len2 + 1;
          len1 -= len2 + 1;

          for (defn = special_subdirs ; defn->name != NULL; ++defn)
            {
              apr_size_t len3 = strlen(defn->name);

              if (len1 >= len3 && memcmp(uri, defn->name, len3) == 0)
                {
                  if (uri[len3] == '\0')
                    {
                      /* URI was "/root/$svn/XXX". The location exists, but
                         has restricted usage. */
                      comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;

                      /* store the resource type so that we can PROPFIND
                         on this collection. */
                      comb->priv.restype = defn->restype;
                    }
                  else if (uri[len3] == '/')
                    {
                      if ((*defn->parse)(comb, uri + len3 + 1, label,
                                         use_checked_in))
                        return TRUE;
                    }
                  else
                    {
                      /* e.g. "/root/$svn/activity" (we just know "act") */
                      return TRUE;
                    }

                  break;
                }
            }

          /* if completed the loop, then it is an unrecognized subdir */
          if (defn->name == NULL)
            return TRUE;
        }
    }
  else
    {
      /* Anything under the root, but not under "$svn". These are all
         version-controlled resources. */
      comb->res.type = DAV_RESOURCE_TYPE_REGULAR;
      comb->res.versioned = TRUE;

      /* The location of these resources corresponds directly to the URI,
         and we keep the leading "/". */
      comb->priv.repos_path = comb->priv.uri_path->data;
    }

  return FALSE;
}

static dav_error * dav_svn_prep_regular(dav_resource_combined *comb)
{
  apr_pool_t *pool = comb->res.pool;
  dav_svn_repos *repos = comb->priv.repos;
  svn_error_t *serr;

  /* A REGULAR resource might have a specific revision already (e.g. if it
     is part of a baseline collection). However, if it doesn't, then we
     will assume that we need the youngest revision.
     ### other cases besides a BC? */
  if (comb->priv.root.rev == SVN_INVALID_REVNUM)
    {
      serr = svn_fs_youngest_rev(&comb->priv.root.rev, repos->fs, pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not determine the proper "
                                     "revision to access");
        }
    }

  /* get the root of the tree */
  serr = svn_fs_revision_root(&comb->priv.root.root, repos->fs,
                              comb->priv.root.rev, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the root of the "
                                 "repository");
    }

  /* ### how should we test for the existence of the path? call is_dir
     ### and look for SVN_ERR_FS_NOT_FOUND? */
  serr = NULL;
  if (serr != NULL)
    {
      const char *msg;

      msg = apr_psprintf(pool, "Could not open the resource '%s'",
                         ap_escape_html(pool, comb->res.uri));
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR, msg);
    }

  /* is this resource a collection? */
  serr = svn_fs_is_dir(&comb->res.collection,
                       comb->priv.root.root, comb->priv.repos_path,
                       pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not determine resource kind");

  /* if we are here, then the resource exists */
  comb->res.exists = TRUE;

  return NULL;
}

static dav_error * dav_svn_prep_version(dav_resource_combined *comb)
{
  svn_error_t *serr;

  /* we are accessing the Version Resource by REV/PATH */
  
  /* ### assert: .baselined = TRUE */
  
  /* if we don't have a revision, then assume the youngest */
  if (!SVN_IS_VALID_REVNUM(comb->priv.root.rev))
    {
      serr = svn_fs_youngest_rev(&comb->priv.root.rev,
                                 comb->priv.repos->fs,
                                 comb->res.pool);
      if (serr != NULL)
        {
          /* ### might not be a baseline */
          
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not fetch 'youngest' revision "
                                     "to enable accessing the latest "
                                     "baseline resource.");
        }
    }
  
  /* ### baselines have no repos_path, and we don't need to open
     ### a root (yet). we just needed to ensure that we have the proper
     ### revision number. */

  if (!comb->priv.root.root)
    {
      serr = svn_fs_revision_root(&comb->priv.root.root, 
                                  comb->priv.repos->fs,
                                  comb->priv.root.rev,
                                  comb->res.pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not open a revision root.");
        }
    }

  /* ### we should probably check that the revision is valid */
  comb->res.exists = TRUE;
  
  /* Set up the proper URI. Most likely, we arrived here via a VCC,
     so the URI will be incorrect. Set the canonical form. */
  /* ### assuming a baseline */
  comb->res.uri = dav_svn_build_uri(comb->priv.repos,
                                    DAV_SVN_BUILD_URI_BASELINE,
                                    comb->priv.root.rev, NULL,
                                    0 /* add_href */,
                                    comb->res.pool);

  return NULL;
}

static dav_error * dav_svn_prep_history(dav_resource_combined *comb)
{
  return NULL;
}

static dav_error * dav_svn_prep_working(dav_resource_combined *comb)
{
  const char *txn_name = dav_svn_get_txn(comb->priv.repos,
                                         comb->priv.root.activity_id);
  apr_pool_t *pool = comb->res.pool;
  svn_error_t *serr;

  if (txn_name == NULL)
    {
      /* ### HTTP_BAD_REQUEST is probably wrong */
      return dav_new_error(pool, HTTP_BAD_REQUEST, 0,
                           "An unknown activity was specified in the URL. "
                           "This is generally caused by a problem in the "
                           "client software.");
    }
  comb->priv.root.txn_name = txn_name;

  /* get the FS transaction, given its name */
  serr = svn_fs_open_txn(&comb->priv.root.txn, comb->priv.repos->fs, txn_name,
                         pool);
  if (serr != NULL)
    {
      if (serr->apr_err == SVN_ERR_FS_NO_SUCH_TRANSACTION)
        return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "An activity was specified and found, but the "
                             "corresponding SVN FS transaction was not "
                             "found.");
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the SVN FS transaction "
                                 "corresponding to the specified activity.");
    }

  if (comb->res.baselined)
    {
      /* a Working Baseline */

      /* if the transaction exists, then the working resource exists */
      comb->res.exists = TRUE;

      return NULL;
    }

  /* get the root of the tree */
  serr = svn_fs_txn_root(&comb->priv.root.root, comb->priv.root.txn, pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the (txn) root of the "
                                 "repository");
    }

  serr = svn_fs_is_dir(&comb->res.collection,
                       comb->priv.root.root, comb->priv.repos_path,
                       pool);
  if (serr != NULL)
    {
      if (serr->apr_err != SVN_ERR_FS_NOT_FOUND)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not determine resource type");
        }

      /* ### verify that the parent exists. needed for PUT, MKCOL, COPY. */
      /* ### actually, mod_dav validates that (via get_parent_resource).
         ### so are we done? */
      comb->res.exists = FALSE;
    }
  else
    {
      comb->res.exists = TRUE;
    }

  return NULL;
}

static dav_error * dav_svn_prep_activity(dav_resource_combined *comb)
{
  const char *txn_name = dav_svn_get_txn(comb->priv.repos,
                                         comb->priv.root.activity_id);

  comb->priv.root.txn_name = txn_name;
  comb->res.exists = txn_name != NULL;

  return NULL;
}

static dav_error * dav_svn_prep_private(dav_resource_combined *comb)
{
  if (comb->priv.restype == DAV_SVN_RESTYPE_VCC)
    {
      /* ### what to do */
    }
  /* else nothing to do (### for now) */

  return NULL;
}

static const struct res_type_handler
{
  dav_resource_type type;
  dav_error * (*prep)(dav_resource_combined *comb);

} res_type_handlers[] =
{
  /* skip UNKNOWN */
  { DAV_RESOURCE_TYPE_REGULAR, dav_svn_prep_regular },
  { DAV_RESOURCE_TYPE_VERSION, dav_svn_prep_version },
  { DAV_RESOURCE_TYPE_HISTORY, dav_svn_prep_history },
  { DAV_RESOURCE_TYPE_WORKING, dav_svn_prep_working },
  /* skip WORKSPACE */
  { DAV_RESOURCE_TYPE_ACTIVITY, dav_svn_prep_activity },
  { DAV_RESOURCE_TYPE_PRIVATE, dav_svn_prep_private },

  { 0, NULL }   /* sentinel */
};

/*
** ### docco...
**
** Set .exists and .collection
** open other, internal bits...
*/
static dav_error * dav_svn_prep_resource(dav_resource_combined *comb)
{
  const struct res_type_handler *scan;

  for (scan = res_type_handlers; scan->prep != NULL; ++scan)
    {
      if (comb->res.type == scan->type)
        return (*scan->prep)(comb);
    }

  return dav_new_error(comb->res.pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                       "DESIGN FAILURE: unknown resource type");
}

static dav_resource *dav_svn_create_private_resource(
    const dav_resource *base,
    enum dav_svn_private_restype restype)
{
  dav_resource_combined *comb;
  svn_stringbuf_t *path;
  const struct special_defn *defn;

  for (defn = special_subdirs; defn->name != NULL; ++defn)
    if (defn->restype == restype)
      break;
  /* assert: defn->name != NULL */

  path = svn_stringbuf_createf(base->pool, "/%s/%s",
                            base->info->repos->special_uri, defn->name);

  comb = apr_pcalloc(base->pool, sizeof(*comb));

  /* ### can/should we leverage dav_svn_prep_resource */

  comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;

  comb->res.exists = TRUE;
  comb->res.collection = TRUE;                  /* ### always true? */
  /* versioned = baselined = working = FALSE */

  comb->res.uri = apr_pstrcat(base->pool, base->info->repos->root_path,
                              path->data, NULL);
  comb->res.info = &comb->priv;
  comb->res.hooks = &dav_svn_hooks_repos;
  comb->res.pool = base->pool;

  comb->priv.uri_path = path;
  comb->priv.repos = base->info->repos;
  comb->priv.root.rev = SVN_INVALID_REVNUM;

  return &comb->res;
}

static void log_warning(void *baton, const char *fmt, ...)
{
  request_rec *r = baton;
  va_list va;
  const char *s;

  va_start(va, fmt);
  s = apr_pvsprintf(r->pool, fmt, va);
  va_end(va);

  ap_log_rerror(APLOG_MARK, APLOG_ERR, APR_EGENERAL, r, "%s", s);
}

static dav_error * dav_svn_get_resource(request_rec *r,
                                        const char *root_path,
                                        const char *label,
                                        int use_checked_in,
                                        dav_resource **resource)
{
  const char *fs_path;
  const char *repo_name;
  const char *xslt_uri;
  dav_resource_combined *comb;
  dav_svn_repos *repos;
  apr_size_t len1;
  char *uri;
  const char *relative;
  svn_error_t *serr;
  dav_error *err;
  int had_slash;

  /* this is usually the first entry into mod_dav_svn, so let's initialize
     the error pool, as a subpool of the request pool. */
  (void) svn_error_init_pool(r->pool);

  if ((fs_path = dav_svn_get_fs_path(r)) == NULL)
    {
      /* ### are SVN_ERR_APMOD codes within the right numeric space? */
      return dav_new_error(r->pool, HTTP_INTERNAL_SERVER_ERROR,
                           SVN_ERR_APMOD_MISSING_PATH_TO_FS,
                           "The server is misconfigured: an SVNPath "
                           "directive is required to specify the location "
                           "of this resource's repository.");
    }

  repo_name = dav_svn_get_repo_name(r);
  xslt_uri = dav_svn_get_xslt_uri(r);

  comb = apr_pcalloc(r->pool, sizeof(*comb));
  comb->res.info = &comb->priv;
  comb->res.hooks = &dav_svn_hooks_repos;
  comb->res.pool = r->pool;

  /* ### ugly hack to carry over Content-Type data to the open_stream, which
     ### does not have access to the request headers. */
  {
    const char *ct = apr_table_get(r->headers_in, "content-type");

    comb->priv.is_svndiff =
      ct != NULL
      && strcmp(ct, SVN_SVNDIFF_MIME_TYPE) == 0;
  }

  /* ### and another hack for computing diffs to send to the client */
  comb->priv.delta_base = apr_table_get(r->headers_in,
                                        SVN_DAV_DELTA_BASE_HEADER);

  /* make a copy so that we can do some work on it */
  uri = apr_pstrdup(r->pool, r->uri);

  /* remove duplicate slashes */
  ap_no2slash(uri);

  /* make sure the URI does not have a trailing "/" */
  len1 = strlen(uri);
  if (len1 > 1 && uri[len1 - 1] == '/')
    {
      had_slash = 1;
      uri[len1 - 1] = '\0';
    }
  else
    had_slash = 0;

  comb->res.uri = uri;

  /* The URL space defined by the SVN provider is always a virtual
     space. Construct the path relative to the configured Location
     (root_path). So... the relative location is simply the URL used,
     skipping the root_path.

     Note: mod_dav has canonialized root_path. It will not have a trailing
           slash (unless it is "/").

     Note: given a URI of /something and a root of /some, then it is
           impossible to be here (and end up with "thing"). This is simply
           because we control /some and are dispatched to here for its
           URIs. We do not control /something, so we don't get here. Or,
           if we *do* control /something, then it is for THAT root.
  */
  relative = ap_stripprefix(uri, root_path);

  /* We want a leading slash on the path specified by <relative>. This
     will almost always be the case since root_path does not have a trailing
     slash. However, if the root is "/", then the slash will be removed
     from <relative>. Backing up a character will put the leading slash
     back.

     Watch out for the empty string! This can happen when URI == ROOT_PATH.
     We simply turn the path into "/" for this case. */
  if (*relative == '\0')
    relative = "/";
  else if (*relative != '/')
    --relative;
  /* ### need a better name... it isn't "relative" because of the leading
     ### slash. something about SVN-private-path */

  /* "relative" is part of the "uri" string, so it has the proper
     lifetime to store here. */
  /* ### that comment no longer applies. we're creating a string with its
     ### own lifetime now. so WHY are we using a string? hmm... */
  comb->priv.uri_path = svn_stringbuf_create(relative, r->pool);

  /* initialize this until we put something real here */
  comb->priv.root.rev = SVN_INVALID_REVNUM;

  /* create the repository structure and stash it away */
  repos = apr_pcalloc(r->pool, sizeof(*repos));
  repos->pool = r->pool;

  comb->priv.repos = repos;

  /* We are assuming the root_path will live at least as long as this
     resource. Considering that it typically comes from the per-dir
     config in mod_dav, this is valid for now. */
  repos->root_path = root_path;

  /* where is the SVN FS for this resource? */
  repos->fs_path = fs_path;

  /* A name for the repository */
  repos->repo_name = repo_name;

  /* An XSL transformation */
  repos->xslt_uri = xslt_uri;

  /* Remember various bits for later URL construction */
  repos->base_url = ap_construct_url(r->pool, "", r);
  repos->special_uri = dav_svn_get_special_uri(r);

  /* Remember who is making this request */
  if ((repos->username = r->user) == NULL)
    repos->username = "anonymous";

  /* open the SVN FS */
  serr = svn_repos_open(&(repos->repos), fs_path, r->pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 apr_psprintf(r->pool,
                                              "Could not open the SVN "
                                              "filesystem at %s", fs_path));
    }

  /* cache the filesystem object */
  repos->fs = svn_repos_fs (repos->repos);

  /* capture warnings during cleanup of the FS */
  svn_fs_set_warning_func(repos->fs, log_warning, r);

  /* Figure out the type of the resource. Note that we have a PARSE step
     which is separate from a PREP step. This is because the PARSE can
     map multiple URLs to the same resource type. The PREP operates on
     the type of the resource. */

  /* skip over the leading "/" in the relative URI */
  if (dav_svn_parse_uri(comb, relative + 1, label, use_checked_in))
    goto malformed_URI;

#ifdef SVN_DEBUG
  if (comb->res.type == DAV_RESOURCE_TYPE_UNKNOWN)
    {
      /* Unknown URI. Return NULL to indicate "no resource" */
      DBG0("DESIGN FAILURE: should not be UNKNOWN at this point");
      *resource = NULL;
      return NULL;
    }
#endif

  /* prepare the resource for operation */
  if ((err = dav_svn_prep_resource(comb)) != NULL)
    return err;

  /* a GET request for a REGULAR collection resource MUST have a trailing
     slash. Redirect to include one if it does not. */
  if (comb->res.collection && comb->res.type == DAV_RESOURCE_TYPE_REGULAR
      && !had_slash && r->method_number == M_GET)
    {
      /* note that we drop r->args. we don't deal with them anyways */
      const char *new_path = apr_pstrcat(r->pool,
                                         ap_escape_uri(r->pool, r->uri),
                                         "/",
                                         NULL);
      apr_table_setn(r->headers_out, "Location",
                     ap_construct_url(r->pool, new_path, r));
      return dav_new_error(r->pool, HTTP_MOVED_PERMANENTLY, 0,
                           "Requests for a collection must have a "
                           "trailing slash on the URI.");
    }

  *resource = &comb->res;
  return NULL;

 malformed_URI:
  /* A malformed URI error occurs when a URI indicates the "special" area,
     yet it has an improper construction. Generally, this is because some
     doofus typed it in manually or has a buggy client. */
  /* ### pick something other than HTTP_INTERNAL_SERVER_ERROR */
  /* ### are SVN_ERR_APMOD codes within the right numeric space? */
  return dav_new_error(r->pool, HTTP_INTERNAL_SERVER_ERROR,
                       SVN_ERR_APMOD_MALFORMED_URI,
                       "The URI indicated a resource within Subversion's "
                       "special resource area, but does not exist. This is "
                       "generally caused by a problem in the client "
                       "software.");
}

static dav_error * dav_svn_get_parent_resource(const dav_resource *resource,
                                               dav_resource **parent_resource)
{
  svn_stringbuf_t *path = resource->info->uri_path;

  /* the root of the repository has no parent */
  if (path->len == 1 && *path->data == '/')
    {
      *parent_resource = NULL;
      return NULL;
    }

  switch (resource->type)
    {
    case DAV_RESOURCE_TYPE_WORKING:
      /* The "/" occurring within the URL of working resources is part of
         its identifier; it does not establish parent resource relationships.
         All working resources have the same parent, which is:
         http://host.name/path2repos/$svn/wrk/
      */
      *parent_resource =
        dav_svn_create_private_resource(resource,
                                        DAV_SVN_RESTYPE_WRK_COLLECTION);
      break;

    default:
      /* ### needs more work. need parents for other resource types
         ###
         ### return an error so we can easily identify the cases where
         ### we've called this function unexpectedly. */
      return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                           apr_psprintf(resource->pool,
                                        "get_parent_resource was called for "
                                        "%s (type %d)",
                                        resource->uri, resource->type));
      break;
    }

  return NULL;
}

/* does RES2 live in the same repository as RES1? */
static int is_our_resource(const dav_resource *res1,
                           const dav_resource *res2)
{
  if (res1->hooks != res2->hooks
      || strcmp(res1->info->repos->fs_path, res2->info->repos->fs_path) != 0)
    {
      /* a different provider, or a different FS repository */
      return 0;
    }

  /* coalesce the repository */
  if (res1->info->repos != res2->info->repos)
    {      
      /* close the old, redundant filesystem */
      (void) svn_repos_close(res2->info->repos->repos);

      /* have res2 point to res1's filesystem */
      res2->info->repos = res1->info->repos;

      /* res2's fs_root object is now invalid.  regenerate it using
         the now-shared filesystem. */
      if (res2->info->root.txn_name)
        {
          /* reopen the txn by name */
          (void) svn_fs_open_txn(&(res2->info->root.txn),
                                 res2->info->repos->fs,
                                 res2->info->root.txn_name,
                                 res2->info->repos->pool);

          /* regenerate the txn "root" object */
          (void) svn_fs_txn_root(&(res2->info->root.root),
                                 res2->info->root.txn,
                                 res2->info->repos->pool);
        }
      else if (res2->info->root.rev)
        {
          /* default:  regenerate the revision "root" object */
          (void) svn_fs_revision_root(&(res2->info->root.root),
                                      res2->info->repos->fs,
                                      res2->info->root.rev,
                                      res2->info->repos->pool);
        }
    }

  return 1;
}

static int dav_svn_is_same_resource(const dav_resource *res1,
                                    const dav_resource *res2)
{
  if (!is_our_resource(res1, res2))
    return 0;

  /* ### what if the same resource were reached via two URIs? */

  return svn_stringbuf_compare(res1->info->uri_path, res2->info->uri_path);
}

static int dav_svn_is_parent_resource(const dav_resource *res1,
                                      const dav_resource *res2)
{
  apr_size_t len1 = strlen(res1->info->uri_path->data);
  apr_size_t len2;

  if (!is_our_resource(res1, res2))
    return 0;

  /* ### what if a resource were reached via two URIs? we ought to define
     ### parent/child relations for resources independent of URIs.
     ### i.e. define a "canonical" location for each resource, then return
     ### the parent based on that location. */

  /* res2 is one of our resources, we can use its ->info ptr */
  len2 = strlen(res2->info->uri_path->data);

  return (len2 > len1
          && memcmp(res1->info->uri_path->data, res2->info->uri_path->data,
                    len1) == 0
          && res2->info->uri_path->data[len1] == '/');
}

static dav_error * dav_svn_open_stream(const dav_resource *resource,
                                       dav_stream_mode mode,
                                       dav_stream **stream)
{
  svn_error_t *serr;

  if (mode == DAV_MODE_WRITE_TRUNC || mode == DAV_MODE_WRITE_SEEKABLE)
    {
      if (resource->type != DAV_RESOURCE_TYPE_WORKING)
        {
          return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                               "Resource body changes may only be made to "
                               "working resources [at this time].");
        }
    }

#if 1
  if (mode == DAV_MODE_WRITE_SEEKABLE)
    {
      return dav_new_error(resource->pool, HTTP_NOT_IMPLEMENTED, 0,
                           "Resource body writes cannot use ranges "
                           "[at this time].");
    }
#endif

  /* start building the stream structure */
  *stream = apr_pcalloc(resource->pool, sizeof(**stream));
  (*stream)->res = resource;

  /* note: when writing, we don't need to use DAV_SVN_REPOS_PATH since
     we cannot write into an "id root". Partly because the FS may not
     let us, but mostly that we have an id root only to deal with Version
     Resources, and those are read only. */

  serr = svn_fs_apply_textdelta(&(*stream)->delta_handler,
                                &(*stream)->delta_baton,
                                resource->info->root.root,
                                resource->info->repos_path,
                                resource->pool);
  if (serr != NULL && serr->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear_all(serr);
      serr = svn_fs_make_file(resource->info->root.root,
                              resource->info->repos_path,
                              resource->pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "Could not create file within the "
                                     "repository.");
        }
      serr = svn_fs_apply_textdelta(&(*stream)->delta_handler,
                                    &(*stream)->delta_baton,
                                    resource->info->root.root,
                                    resource->info->repos_path,
                                    resource->pool);
    }
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not prepare to write the file");
    }

  /* if the incoming data is an SVNDIFF, then create a stream that
     will process the data into windows and invoke the FS window handler
     when a window is ready. */
  /* ### we need a better way to check the content-type! this is bogus
     ### because we're effectively looking at the request_rec. doubly
     ### bogus because this means you cannot open arbitrary streams and
     ### feed them content (the type is always tied to a request_rec).
     ### probably ought to pass the type to open_stream */
  if (resource->info->is_svndiff)
    {
      (*stream)->wstream =
        svn_txdelta_parse_svndiff((*stream)->delta_handler,
                                  (*stream)->delta_baton,
                                  TRUE,
                                  resource->pool);
    }

  return NULL;
}

static dav_error * dav_svn_close_stream(dav_stream *stream, int commit)
{
  if (stream->rstream != NULL)
    svn_stream_close(stream->rstream);

  /* if we have a write-stream, then closing it also takes care of the
     handler (so make sure not to send a NULL to it, too) */
  if (stream->wstream != NULL)
    svn_stream_close(stream->wstream);
  else if (stream->delta_handler != NULL)
    (*stream->delta_handler)(NULL, stream->delta_baton);

  return NULL;
}

static dav_error * dav_svn_write_stream(dav_stream *stream, const void *buf,
                                        apr_size_t bufsize)
{
  svn_error_t *serr;

  if (stream->wstream != NULL)
    {
      serr = svn_stream_write(stream->wstream, buf, &bufsize);
      /* ### would the returned bufsize ever not match the requested amt? */
    }
  else
    {
      svn_txdelta_window_t window = { 0 };
      svn_txdelta_op_t op;
      svn_string_t data;

      data.data = buf;
      data.len = bufsize;

      op.action_code = svn_txdelta_new;
      op.offset = 0;
      op.length = bufsize;

      window.tview_len = bufsize;   /* result will be this long */
      window.num_ops = 1;
      window.ops = &op;
      window.new_data = &data;

      serr = (*stream->delta_handler)(&window, stream->delta_baton);
    }

  if (serr)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not write the file contents");
    }
  return NULL;
}

static dav_error * dav_svn_seek_stream(dav_stream *stream,
                                       apr_off_t abs_position)
{
  /* ### fill this in */

  return dav_new_error(stream->res->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "Resource body read/write cannot use ranges "
                       "[at this time].");
}

const char * dav_svn_getetag(const dav_resource *resource)
{
  svn_error_t *serr;
  svn_revnum_t created_rev;

  /* if the resource doesn't exist, isn't a simple REGULAR or VERSION
     resource, or it is a Baseline, then it has no etag. */
  /* ### we should assign etags to all resources at some point */
  if (!resource->exists
      || (resource->type != DAV_RESOURCE_TYPE_REGULAR
          && resource->type != DAV_RESOURCE_TYPE_VERSION)
      || (resource->type == DAV_RESOURCE_TYPE_VERSION && resource->baselined))
    return "";

  /* ### what kind of etag to return for collections, activities, etc? */

  if ((serr = svn_fs_node_created_rev(&created_rev, resource->info->root.root,
                                      resource->info->repos_path,
                                      resource->pool)))
    {
      /* ### what to do? */
      return "";
    }
  
  return apr_psprintf(resource->pool, "\"%" SVN_REVNUM_T_FMT "/%s\"",
                      created_rev, resource->info->repos_path);
}

static dav_error * dav_svn_set_headers(request_rec *r,
                                       const dav_resource *resource)
{
  svn_error_t *serr;
  apr_off_t length;
  const char *mimetype = NULL;
  
  if (!resource->exists)
    return NULL;

  /* ### what to do for collections, activities, etc */

  /* make sure the proper mtime is in the request record */
#if 0
  ap_update_mtime(r, resource->info->finfo.mtime);
#endif

  /* ### note that these use r->filename rather than <resource> */
#if 0
  ap_set_last_modified(r);
#endif

  /* generate our etag and place it into the output */
  apr_table_setn(r->headers_out, "ETag", dav_svn_getetag(resource));

  /* we accept byte-ranges */
  apr_table_setn(r->headers_out, "Accept-Ranges", "bytes");

  /* For a directory, we will send text/html or text/xml. If we have a delta
     base, then we will always be generating an svndiff.  Otherwise,
     we need to fetch the appropriate MIME type from the resource's
     properties (and use text/plain if it isn't there). */
  if (resource->collection)
    {
      if (resource->info->repos->xslt_uri)
        mimetype = "text/xml";
      else
        mimetype = "text/html";
    }
  else if (resource->info->delta_base != NULL)
    {
      dav_svn_uri_info info;

      /* First order of business is to parse it. */
      serr = dav_svn_simple_parse_uri(&info, resource,
                                      resource->info->delta_base,
                                      resource->pool);

      /* If we successfully parse the base URL, then send an svndiff. */
      if ((serr == NULL) && (info.rev != SVN_INVALID_REVNUM))
        {
          mimetype = SVN_SVNDIFF_MIME_TYPE;
        }
    }

  if (mimetype == NULL)
    {
      svn_string_t *value;

      serr = svn_fs_node_prop(&value,
                              resource->info->root.root,
                              resource->info->repos_path,
                              SVN_PROP_MIME_TYPE,
                              resource->pool);
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "could not fetch the resource's MIME type");

      mimetype = value ? value->data : "text/plain";

      /* if we aren't sending a diff, then we know the length of the file,
         so set up the Content-Length header */
      serr = svn_fs_file_length(&length,
                                resource->info->root.root,
                                resource->info->repos_path,
                                resource->pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "could not fetch the resource length");
        }
      ap_set_content_length(r, length);
    }

  /* set the discovered MIME type */
  /* ### it would be best to do this during the findct phase... */
  r->content_type = mimetype;

  return NULL;
}

static svn_error_t *dav_svn_write_to_filter(void *baton,
                                            const char *buffer,
                                            apr_size_t *len)
{
  dav_svn_diff_ctx_t *dc = baton;
  apr_bucket_brigade *bb;
  apr_bucket *bkt;
  apr_status_t status;

  /* take the current data and shove it into the filter */
  bb = apr_brigade_create(dc->pool, dc->output->c->bucket_alloc);
  bkt = apr_bucket_transient_create(buffer, *len, dc->output->c->bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(bb, bkt);
  if ((status = ap_pass_brigade(dc->output, bb)) != APR_SUCCESS) {
    return svn_error_create(status, 0, NULL, dc->pool,
                            "Could not write data to filter.");
  }

  return SVN_NO_ERROR;
}

static svn_error_t *dav_svn_close_filter(void *baton)
{
  dav_svn_diff_ctx_t *dc = baton;
  apr_bucket_brigade *bb;
  apr_bucket *bkt;
  apr_status_t status;

  /* done with the file. write an EOS bucket now. */
  bb = apr_brigade_create(dc->pool, dc->output->c->bucket_alloc);
  bkt = apr_bucket_eos_create(dc->output->c->bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(bb, bkt);
  if ((status = ap_pass_brigade(dc->output, bb)) != APR_SUCCESS) {
    return svn_error_create(status, 0, NULL, dc->pool,
                            "Could not write EOS to filter.");
  }

  return SVN_NO_ERROR;
}

static dav_error * dav_svn_deliver(const dav_resource *resource,
                                   ap_filter_t *output)
{
  svn_error_t *serr;
  apr_bucket_brigade *bb;
  apr_bucket *bkt;
  apr_status_t status;

  /* Check resource type */
  if (resource->type != DAV_RESOURCE_TYPE_REGULAR
      && resource->type != DAV_RESOURCE_TYPE_VERSION
      && resource->type != DAV_RESOURCE_TYPE_WORKING) {
    return dav_new_error(resource->pool, HTTP_CONFLICT, 0,
                         "Cannot GET this type of resource.");
  }

  if (resource->collection) {
    const int gen_html = !resource->info->repos->xslt_uri;
    apr_hash_t *entries;
    apr_pool_t *entry_pool;
    apr_array_header_t *sorted;
    int i;

    /* XML schema for the directory index if xslt_uri is set:

       <?xml version="1.0"?>
       <?xml-stylesheet type="text/xsl" href="[info->repos->xslt_uri]"?> */
    static const char xml_index_dtd[] =
      "<!DOCTYPE svn [\n"
      "  <!ELEMENT svn   (index)>\n"
      "  <!ATTLIST svn   version CDATA #REQUIRED\n"
      "                  href    CDATA #REQUIRED>\n"
      "  <!ELEMENT index (updir?, (file | dir)*)>\n"
      "  <!ATTLIST index name    CDATA #IMPLIED\n"
      "                  path    CDATA #IMPLIED\n"
      "                  rev     CDATA #IMPLIED>\n"
      "  <!ELEMENT updir EMPTY>\n"
      "  <!ELEMENT file  (prop)*>\n"
      "  <!ATTLIST file  name    CDATA #REQUIRED\n"
      "                  href    CDATA #REQUIRED>\n"
      "  <!ELEMENT dir   (prop)*>\n"
      "  <!ATTLIST dir   name    CDATA #REQUIRED\n"
      "                  href    CDATA #REQUIRED>\n"
      "  <!ELEMENT prop  (#PCDATA)>\n"
      "  <!ATTLIST prop  name    CDATA #REQUIRED>\n"
      "]>\n";

    /* <svn version="0.13.1 (dev-build)"
            href="http://subversion.tigris.org">
         <index name="[info->repos->repo_name]"
                path="[info->repos_path]"
                rev="[info->root.rev]">
           <file name="foo">
             <prop name="mime-type">image/png</prop>
           </file>
           <dir name="bar"/>
         </index>
       </svn> */

    serr = svn_fs_dir_entries(&entries, resource->info->root.root,
                              resource->info->repos_path, resource->pool);
    if (serr != NULL)
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not fetch directory entries");

    bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);

    if (gen_html)
      {
        const char *title;
        if (resource->info->repos_path == NULL)
          title = "unknown location";
        else
          title = ap_escape_uri(resource->pool, resource->info->repos_path);

        if (SVN_IS_VALID_REVNUM(resource->info->root.rev))
          title = apr_psprintf(resource->pool,
                               "Revision %" SVN_REVNUM_T_FMT ": %s",
                               resource->info->root.rev, title);

        if (resource->info->repos->repo_name)
          title = apr_psprintf(resource->pool, "%s - %s",
                               resource->info->repos->repo_name,
                               title);

        ap_fprintf(output, bb, "<html><head><title>%s</title></head>\n"
                   "<body>\n <h2>%s</h2>\n <ul>\n", title, title);
      }
    else
      {
        ap_fputs(output, bb, "<?xml version=\"1.0\"?>\n");
        ap_fprintf(output, bb,
                   "<?xml-stylesheet type=\"text/xsl\" href=\"%s\"?>\n",
                   resource->info->repos->xslt_uri);
        ap_fputs(output, bb, xml_index_dtd);
        ap_fputs(output, bb,
                 "<svn version=\"" SVN_VERSION "\"\n"
                 "     href=\"http://subversion.tigris.org/\">\n");

        ap_fputs(output, bb, "  <index");
        if (resource->info->repos->repo_name)
            ap_fprintf(output, bb, " name=\"%s\"",
                       resource->info->repos->repo_name);
        if (SVN_IS_VALID_REVNUM(resource->info->root.rev))
          ap_fprintf(output, bb, " rev=\"%" SVN_REVNUM_T_FMT "\"",
                     resource->info->root.rev);
        if (resource->info->repos_path)
          ap_fprintf(output, bb, " path=\"%s\"",
                     ap_escape_uri(resource->pool,
                                   resource->info->repos_path));
        ap_fputs(output, bb, ">\n");
      }

    if (resource->info->repos_path && resource->info->repos_path[1] != '\0')
      {
        if (gen_html)
          ap_fprintf(output, bb, "  <li><a href=\"../\">..</a></li>\n");
        else
          ap_fprintf(output, bb, "    <updir />\n");
      }

    /* get a sorted list of the entries */
    sorted = apr_hash_sorted_keys(entries, svn_sort_compare_items_as_paths,
                                  resource->pool);

    entry_pool = svn_pool_create(resource->pool);

    for (i = 0; i < sorted->nelts; ++i)
      {
        const svn_item_t *item = &APR_ARRAY_IDX(sorted, i, const svn_item_t);
        /* unused: const svn_fs_dirent_t *entry = elem->value; */
        const char *entry_path;
        const char *name;
        const char *href;
        int is_dir;

        /* for a REGULAR resource, the root is going to be a normal root,
           which allows us to access it with a path. build a path for this
           entry so that we can get information for it. */
        entry_path = apr_pstrcat(entry_pool, resource->info->repos_path,
                                 "/", item->key, NULL);

        (void) svn_fs_is_dir(&is_dir, resource->info->root.root,
                             entry_path, entry_pool);

        name = ap_escape_uri(entry_pool, item->key);

        /* append a trailing slash onto the name for directories. we NEED
           this for the href portion so that the relative reference will
           descend properly. for the visible portion, it is just nice. */
        if (is_dir)
          href = apr_pstrcat(entry_pool, name, "/", NULL);
        else
          href = name;

        if (gen_html)
          ap_fprintf(output, bb,
                     "  <li><a href=\"%s\">%s</a></li>\n",
                     href, href);
        else
          {
            const char *const tag = (is_dir ? "dir" : "file");

            /* ### This is where the we could search for props */

            ap_fprintf(output, bb,
                       "    <%s name=\"%s\" href=\"%s\"></%s>\n",
                       tag, name, href, tag);
          }
        svn_pool_clear(entry_pool);
      }

    svn_pool_destroy(entry_pool);

    if (gen_html)
      ap_fputs(output, bb,
               " </ul>\n <hr noshade><em>Powered by "
               "<a href=\"http://subversion.tigris.org/\">Subversion</a> "
               "version " SVN_VERSION "."
               "</em>\n</body></html>");
    else
      ap_fputs(output, bb, "  </index>\n</svn>\n");

    bkt = apr_bucket_eos_create(output->c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, bkt);
    if ((status = ap_pass_brigade(output, bb)) != APR_SUCCESS) {
      return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                           "Could not write EOS to filter.");
    }

    return NULL;
  }


  /* If we have a base for a delta, then we want to compute an svndiff
     between the provided base and the requested resource. For a simple
     request, then we just grab the file contents. */
  if (resource->info->delta_base != NULL)
    {
      dav_svn_uri_info info;
      svn_fs_root_t *root;
      int is_file;
      svn_txdelta_stream_t *txd_stream;
      svn_stream_t *o_stream;
      svn_txdelta_window_handler_t handler;
      void * h_baton;
      dav_svn_diff_ctx_t dc = { 0 };

      /* First order of business is to parse it. */
      serr = dav_svn_simple_parse_uri(&info, resource,
                                      resource->info->delta_base,
                                      resource->pool);

      /* If we successfully parse the base URL, then send an svndiff. */
      if ((serr == NULL) && (info.rev != SVN_INVALID_REVNUM))
        {
          /* We are always accessing the base resource by ID, so open
             an ID root. */
          serr = svn_fs_revision_root(&root, resource->info->repos->fs,
                                      info.rev, resource->pool);
          if (serr != NULL)
            return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                       "could not open a root for the base");

          /* verify that it is a file */
          serr = svn_fs_is_file(&is_file, root, info.repos_path, 
                                resource->pool);
          if (serr != NULL)
            return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                       "could not determine if the base "
                                       "is really a file");
          if (!is_file)
            return dav_new_error(resource->pool, HTTP_BAD_REQUEST, 0,
                                 "the delta base does not refer to a file");

          /* Okay. Let's open up a delta stream for the client to read. */
          serr = svn_fs_get_file_delta_stream(&txd_stream,
                                              root, info.repos_path,
                                              resource->info->root.root,
                                              resource->info->repos_path,
                                              resource->pool);
          if (serr != NULL)
            return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                       "could not prepare to read a delta");

          /* create a stream that svndiff data will be written to,
             which will copy it to the network */
          dc.output = output;
          dc.pool = resource->pool;
          o_stream = svn_stream_create(&dc, resource->pool);
          svn_stream_set_write(o_stream, dav_svn_write_to_filter);
          svn_stream_set_close(o_stream, dav_svn_close_filter);

          /* get a handler/baton for writing into the output stream */
          svn_txdelta_to_svndiff(o_stream, resource->pool, &handler, &h_baton);

          /* got everything set up. read in delta windows and shove them into
             the handler, which pushes data into the output stream, which goes
             to the network. */
          serr = svn_txdelta_send_txstream(txd_stream, handler, h_baton,
                                           resource->pool);
          if (serr != NULL)
            return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                       "could not deliver the txdelta stream");


          return NULL;
        }
    }

  /* resource->info->delta_base is NULL, or we had an invalid base URL */
    {
      svn_stream_t *stream;
      char *block;

      serr = svn_fs_file_contents(&stream,
                                  resource->info->root.root,
                                  resource->info->repos_path,
                                  resource->pool);
      if (serr != NULL)
        {
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                     "could not prepare to read the file");
        }

      /* ### one day in the future, we can create a custom bucket type
         ### which will read from the FS stream on demand */

      block = apr_palloc(resource->pool, SVN_STREAM_CHUNK_SIZE);
      while (1) {
        apr_size_t bufsize = SVN_STREAM_CHUNK_SIZE;

        /* read from the FS ... */
        serr = svn_stream_read(stream, block, &bufsize);
        if (serr != NULL)
          {
            return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                       "could not read the file contents");
          }
        if (bufsize == 0)
          break;

        /* build a brigade and write to the filter ... */
        bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
        bkt = apr_bucket_transient_create(block, bufsize, 
                                          output->c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, bkt);
        if ((status = ap_pass_brigade(output, bb)) != APR_SUCCESS) {
          /* ### what to do with status; and that HTTP code... */
          return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                               "Could not write data to filter.");
        }
      }

      /* done with the file. write an EOS bucket now. */
      bb = apr_brigade_create(resource->pool, output->c->bucket_alloc);
      bkt = apr_bucket_eos_create(output->c->bucket_alloc);
      APR_BRIGADE_INSERT_TAIL(bb, bkt);
      if ((status = ap_pass_brigade(output, bb)) != APR_SUCCESS) {
        /* ### what to do with status; and that HTTP code... */
        return dav_new_error(resource->pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                             "Could not write EOS to filter.");
      }

      return NULL;
    }
}

static dav_error * dav_svn_create_collection(dav_resource *resource)
{
  svn_error_t *serr;

  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    {
      return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "Collections can only be created within a working "
                           "collection [at this time].");
    }

  /* ### note that the parent was checked out at some point, and this
     ### is being preformed relative to the working rsrc for that parent */

  /* note: when writing, we don't need to use DAV_SVN_REPOS_PATH since
     we cannot write into an "id root". Partly because the FS may not
     let us, but mostly that we have an id root only to deal with Version
     Resources, and those are read only. */

  if ((serr = svn_fs_make_dir(resource->info->root.root,
                              resource->info->repos_path,
                              resource->pool)) != NULL)
    {
      /* ### need a better error */
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not create the collection.");
    }

  return NULL;
}

static dav_error * dav_svn_copy_resource(const dav_resource *src,
                                         dav_resource *dst,
                                         int depth,
                                         dav_response **response)
{
  /* ### source must be from a collection under baseline control. the
     ### baseline will (implicitly) indicate the source revision, and the
     ### path will be derived simply from the URL path */

  /* ### the destination's parent must be a working collection */

  /* ### ben goofing around: */
  /*  char *msg;
      apr_psprintf
      (src->pool, "Got a COPY request with src arg '%s' and dst arg '%s'",
      src->uri, dst->uri);
      
      return dav_new_error(src->pool, HTTP_NOT_IMPLEMENTED, 0, msg);
  */

  svn_error_t *serr;
  
  serr = svn_fs_copy (src->info->root.root,  /* the root object of src rev*/
                      src->info->repos_path, /* the relative path of src */
                      dst->info->root.root,  /* the root object of dst txn*/ 
                      dst->info->repos_path, /* the relative path of dst */
                      src->pool);
  if (serr)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "Unable to make a filesystem copy.");

  return NULL;
}

static dav_error * dav_svn_move_resource(dav_resource *src,
                                         dav_resource *dst,
                                         dav_response **response)
{
  /* NOTE: Subversion does not use the MOVE method. Strictly speaking,
     we do not need to implement this repository function. */

  /* ### fill this in */

  return dav_new_error(src->pool, HTTP_NOT_IMPLEMENTED, 0,
                       "MOVE is not available "
                       "[at this time].");
}

static dav_error * dav_svn_remove_resource(dav_resource *resource,
                                           dav_response **response)
{
  svn_error_t *serr;

  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    {
      return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "Resources can only be deleted from within a "
                           "working collection [at this time].");
    }

  /* ### note that the parent was checked out at some point, and this
     ### is being preformed relative to the working rsrc for that parent */

  /* NOTE: strictly speaking, we cannot determine whether the parent was
     ever checked out, and that this working resource is relative to that
     checked out parent. It is entirely possible the client checked out
     the target resource and just deleted it. Subversion doesn't mind, but
     this does imply we are not enforcing the "checkout the parent, then
     delete from within" semantic. */

  /* note: when writing, we don't need to use DAV_SVN_REPOS_PATH since
     we cannot write into an "id root". Partly because the FS may not
     let us, but mostly that we have an id root only to deal with Version
     Resources, and those are read only. */

  if ((serr = svn_fs_delete_tree(resource->info->root.root,
                                 resource->info->repos_path,
                                 resource->pool)) != NULL)
    {
      /* ### need a better error */
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not delete the resource.");
    }

  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_do_walk(dav_svn_walker_context *ctx, int depth)
{
  const dav_walk_params *params = ctx->params;
  int isdir = ctx->res.collection;
  dav_error *err;
  svn_error_t *serr;
  apr_hash_index_t *hi;
  apr_size_t path_len;
  apr_size_t uri_len;
  apr_size_t repos_len;
  apr_hash_t *children;
  apr_pool_t *params_subpool;

  /* The current resource is a collection (possibly here thru recursion)
     and this is the invocation for the collection. Alternatively, this is
     the first [and only] entry to do_walk() for a member resource, so
     this will be the invocation for the member. */
  err = (*params->func)(&ctx->wres,
                        isdir ? DAV_CALLTYPE_COLLECTION : DAV_CALLTYPE_MEMBER);
  if (err != NULL)
    return err;

  /* if we are not to recurse, or this is a member, then we're done */
  if (depth == 0 || !isdir)
    return NULL;

  /* ### for now, let's say that working resources have no children. of
     ### course, this isn't true (or "right") for working collections, but
     ### we don't actually need to do a walk right now. */
  if (params->root->type == DAV_RESOURCE_TYPE_WORKING)
    return NULL;

  /* ### need to allow more walking in the future */
  if (params->root->type != DAV_RESOURCE_TYPE_REGULAR)
    {
      return dav_new_error(params->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "Walking the resource hierarchy can only be done "
                           "on 'regular' resources [at this time].");
    }

  /* assert: collection resource. isdir == TRUE. repos_path != NULL. */

  /* append "/" to the paths, in preparation for appending child names.
     don't add "/" if the paths are simply "/" */
  if (ctx->info.uri_path->data[ctx->info.uri_path->len - 1] != '/')
    svn_stringbuf_appendcstr(ctx->info.uri_path, "/");
  if (ctx->repos_path->data[ctx->repos_path->len - 1] != '/')
    svn_stringbuf_appendcstr(ctx->repos_path, "/");

  /* NOTE: the URI should already have a trailing "/" */

  /* fix up the dependent pointers */
  ctx->info.repos_path = ctx->repos_path->data;

  /* all of the children exist. also initialize the collection flag. */
  ctx->res.exists = TRUE;
  ctx->res.collection = FALSE;

  /* remember these values so we can chop back to them after each time
     we append a child name to the path/uri/repos */
  path_len = ctx->info.uri_path->len;
  uri_len = ctx->uri->len;
  repos_len = ctx->repos_path->len;

  /* fetch this collection's children */
  params_subpool = svn_pool_create(params->pool);

  serr = svn_fs_dir_entries(&children, ctx->info.root.root,
                            ctx->info.repos_path, params->pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not fetch collection members");

  /* iterate over the children in this collection */
  for (hi = apr_hash_first(params->pool, children); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_fs_dirent_t *dirent;
      int is_file;

      /* fetch one of the children */
      apr_hash_this(hi, &key, &klen, &val);
      dirent = val;

      /* authorize access to this resource, if applicable */
      if (params->walk_type & DAV_WALKTYPE_AUTH)
        {
          /* ### how/what to do? */
        }

      /* append this child to our buffers */
      svn_stringbuf_appendbytes(ctx->info.uri_path, key, klen);
      svn_stringbuf_appendbytes(ctx->uri, key, klen);
      svn_stringbuf_appendbytes(ctx->repos_path, key, klen);

      /* reset the pointers since the above may have changed them */
      ctx->res.uri = ctx->uri->data;
      ctx->info.repos_path = ctx->repos_path->data;

      serr = svn_fs_is_file(&is_file,
                            ctx->info.root.root, ctx->info.repos_path,
                            params_subpool);
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "could not determine resource kind");

      if ( is_file )
        {
          err = (*params->func)(&ctx->wres, DAV_CALLTYPE_MEMBER);
          if (err != NULL)
            return err;
        }
      else
        {
          /* this resource is a collection */
          ctx->res.collection = TRUE;

          /* append a slash to the URI (the path doesn't need it yet) */
          svn_stringbuf_appendcstr(ctx->uri, "/");
          ctx->res.uri = ctx->uri->data;

          /* recurse on this collection */
          err = dav_svn_do_walk(ctx, depth - 1);
          if (err != NULL)
            return err;

          /* restore the data */
          ctx->res.collection = 0;
        }

      /* chop the child off the paths and uri. NOTE: no null-term. */
      ctx->info.uri_path->len = path_len;
      ctx->uri->len = uri_len;
      ctx->repos_path->len = repos_len;

      svn_pool_clear(params_subpool);
    }

  svn_pool_destroy(params_subpool);

  return NULL;
}

static dav_error * dav_svn_walk(const dav_walk_params *params, int depth,
				dav_response **response)
{
  dav_svn_walker_context ctx = { 0 };
  dav_error *err;

  ctx.params = params;

  ctx.wres.walk_ctx = params->walk_ctx;
  ctx.wres.pool = params->pool;
  ctx.wres.resource = &ctx.res;

  /* copy the resource over and adjust the "info" reference */
  ctx.res = *params->root;
  ctx.info = *ctx.res.info;

  ctx.res.info = &ctx.info;

  /* operate within the proper pool */
  ctx.res.pool = params->pool;

  /* Don't monkey with the path from params->root. Create a new one.
     This path will then be extended/shortened as necessary. */
  ctx.info.uri_path = svn_stringbuf_dup(ctx.info.uri_path, params->pool);

  /* prep the URI buffer */
  ctx.uri = svn_stringbuf_create(params->root->uri, params->pool);

  /* same for repos_path */
  if (ctx.info.repos_path == NULL)
    ctx.repos_path = NULL;
  else
    ctx.repos_path = svn_stringbuf_create(ctx.info.repos_path, params->pool);

  /* if we have a collection, then ensure the URI has a trailing "/" */
  /* ### get_resource always kills the trailing slash... */
  if (ctx.res.collection && ctx.uri->data[ctx.uri->len - 1] != '/') {
    svn_stringbuf_appendcstr(ctx.uri, "/");
  }

  /* the current resource's URI is stored in the (telescoping) ctx.uri */
  ctx.res.uri = ctx.uri->data;

  /* the current resource's repos_path is stored in ctx.repos_path */
  if (ctx.repos_path != NULL)
    ctx.info.repos_path = ctx.repos_path->data;

  /* Create a pool usable by the response. */
  ctx.info.pool = svn_pool_create(params->pool);

  /* ### is the root already/always open? need to verify */

  /* always return the error, and any/all multistatus responses */
  err = dav_svn_do_walk(&ctx, depth);
  *response = ctx.wres.response;

  return err;
}


/*** Utility functions for resource management ***/

dav_resource *dav_svn_create_working_resource(const dav_resource *base,
                                              const char *activity_id,
                                              const char *txn_name)
{
  dav_resource_combined *comb;
  svn_stringbuf_t *path;

  if (base->baselined)
    path = svn_stringbuf_createf(base->pool,
                                 "/%s/wbl/%s/%" SVN_REVNUM_T_FMT,
                                 base->info->repos->special_uri,
                                 activity_id, base->info->root.rev);
  else
    path = svn_stringbuf_createf(base->pool, "/%s/wrk/%s%s",
                              base->info->repos->special_uri,
                              activity_id, base->info->repos_path);
  

  comb = apr_pcalloc(base->pool, sizeof(*comb));

  comb->res.type = DAV_RESOURCE_TYPE_WORKING;
  comb->res.exists = TRUE;      /* ### not necessarily correct */
  comb->res.versioned = TRUE;
  comb->res.working = TRUE;
  comb->res.baselined = base->baselined;
  /* collection = FALSE.   ### not necessarily correct */

  comb->res.uri = apr_pstrcat(base->pool, base->info->repos->root_path,
                              path->data, NULL);
  comb->res.info = &comb->priv;
  comb->res.hooks = &dav_svn_hooks_repos;
  comb->res.pool = base->pool;

  comb->priv.uri_path = path;
  comb->priv.repos = base->info->repos;
  comb->priv.repos_path = base->info->repos_path;
  comb->priv.root.rev = base->info->root.rev;
  comb->priv.root.activity_id = activity_id;
  comb->priv.root.txn_name = txn_name;

  return &comb->res;
}


const dav_hooks_repository dav_svn_hooks_repos =
{
  1,                            /* special GET handling */
  dav_svn_get_resource,
  dav_svn_get_parent_resource,
  dav_svn_is_same_resource,
  dav_svn_is_parent_resource,
  dav_svn_open_stream,
  dav_svn_close_stream,
  dav_svn_write_stream,
  dav_svn_seek_stream,
  dav_svn_set_headers,
  dav_svn_deliver,
  dav_svn_create_collection,
  dav_svn_copy_resource,
  dav_svn_move_resource,
  dav_svn_remove_resource,
  dav_svn_walk,
  dav_svn_getetag,
};


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
