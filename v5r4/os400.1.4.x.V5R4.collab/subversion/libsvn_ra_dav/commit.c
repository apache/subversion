/*
 * commit.c :  routines for committing changes to the server
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



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_uuid.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <stdlib.h>     /* for free() */
#include <assert.h>

#include <ne_socket.h>
#include <ne_request.h>
#include <ne_props.h>
#include <ne_basic.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_io.h"
#include "svn_ra.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_dav.h"
#include "svn_props.h"

#include "svn_private_config.h"

#include "ra_dav.h"


/*
** version_rsrc_t: identify the relevant pieces of a resource on the server
**
** REVISION is the resource's revision, or SVN_INVALID_REVNUM if it is
** new or is the HEAD.
**
** URL refers to the public/viewable/original resource.
** VSN_URL refers to the version resource that we stored locally
** WR_URL refers to a working resource for this resource
**
** Note that VSN_URL is NULL if this resource has just been added, and
** WR_URL can be NULL if the resource has not (yet) been checked out.
**
** LOCAL_PATH is relative to the root of the commit. It will be used
** for the get_func, push_func, and close_func callbacks.
*/
typedef struct
{
  svn_revnum_t revision;
  const char *url;
  const char *vsn_url;
  const char *wr_url;
  const char *local_path;
  apr_pool_t *pool; /* pool in which this resource is allocated. */

} version_rsrc_t;


/* Context for parsing <D:error> bodies, used when we call ne_copy(). */
struct copy_baton
{
  /* An indication of whether we're currently processing a COPY request
   * or not.
   */
  svn_boolean_t making_a_copy;
  
  /* A parser for handling <D:error> responses from mod_dav_svn.  This
   * will be NULL if we haven't processed a COPY request yet.
   */
  ne_xml_parser *error_parser;

  /* If <D:error> is returned, here's where the parsed result goes, or
   * NULL otherwise.
   */
  svn_error_t *err;

  /* A place for allocating fields in this structure. */
  apr_pool_t *pool;
};

typedef struct
{
  svn_ra_dav__session_t *ras;
  const char *activity_url;

  apr_hash_t *valid_targets;

  svn_ra_get_wc_prop_func_t get_func;
  svn_ra_push_wc_prop_func_t push_func;
  void *cb_baton;

  svn_boolean_t disable_merge_response;

  /* The (potential) author of this commit. */
  const char *user;

  /* Log message for the commit. */
  const char *log_msg;

  /* The commit callback and baton */
  svn_commit_callback2_t callback;
  void *callback_baton;

  /* The hash of lock-tokens owned by the working copy. */
  apr_hash_t *tokens;

  /* Whether or not to keep the locks after commit is done. */
  svn_boolean_t keep_locks;

  /* A context for neon COPY request callbacks. */
  struct copy_baton *cb;

} commit_ctx_t;

typedef struct
{
  apr_file_t *tmpfile;
  svn_stringbuf_t *fname;
  const char *base_checksum;    /* hex md5 of base text; may be null */
} put_baton_t;

typedef struct
{
  commit_ctx_t *cc;
  version_rsrc_t *rsrc;
  apr_hash_t *prop_changes; /* name/values pairs of new/changed properties. */
  apr_array_header_t *prop_deletes; /* names of properties to delete. */
  svn_boolean_t created; /* set if this is an add rather than an update */
  apr_pool_t *pool; /* the pool from open_foo() / add_foo() */
  put_baton_t *put_baton;  /* baton for this file's PUT request */
  const char *token;       /* file's lock token, if available */
} resource_baton_t;

/* this property will be fetched from the server when we don't find it
   cached in the WC property store. */
static const ne_propname fetch_props[] =
{
  { "DAV:", "checked-in" },
  { NULL }
};

static const ne_propname log_message_prop = { SVN_DAV_PROP_NS_SVN, "log" };

/* perform a deep copy of BASE into POOL, and return the result. */
static version_rsrc_t * dup_resource(version_rsrc_t *base, apr_pool_t *pool)
{
  version_rsrc_t *rsrc = apr_pcalloc(pool, sizeof(*rsrc));
  rsrc->pool = pool;
  rsrc->revision = base->revision;
  rsrc->url = base->url ?
    apr_pstrdup(pool, base->url) : NULL;
  rsrc->vsn_url = base->vsn_url ?
    apr_pstrdup(pool, base->vsn_url) : NULL;
  rsrc->wr_url = base->wr_url ?
    apr_pstrdup(pool, base->wr_url) : NULL;
  rsrc->local_path = base->local_path ? 
    apr_pstrdup(pool, base->local_path) : NULL;
  return rsrc;
}

static svn_error_t * simple_request(svn_ra_dav__session_t *ras, 
                                    const char *method,
                                    const char *url, 
                                    int *code,
                                    apr_hash_t *extra_headers,
                                    int okay_1, 
                                    int okay_2, 
                                    apr_pool_t *pool)
{
  ne_request *req;

  /* create/prep the request */
  req = ne_request_create(ras->sess, method, url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_DAV_CREATING_REQUEST, NULL,
                               _("Could not create a request (%s '%s')"),
                               method, url);
    }

  /* add any extra headers passed in by caller. */
  if (extra_headers != NULL)
    {
      apr_hash_index_t *hi;
      for (hi = apr_hash_first(pool, extra_headers);
           hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          apr_hash_this(hi, &key, NULL, &val);
          ne_add_request_header(req, (const char *) key, (const char *) val); 
        }
    }

  /* run the request and get the resulting status code (and svn_error_t) */
  return svn_ra_dav__request_dispatch(code, req, ras->sess,
                                      method, url, okay_1, okay_2,
#ifdef SVN_NEON_0_25
                                      NULL, NULL,
#endif /* SVN_NEON_0_25 */
                                      pool);
}


static svn_error_t * delete_activity(void *edit_baton,
                                     apr_pool_t *pool)
{
  commit_ctx_t *cc = edit_baton;
  return simple_request(cc->ras, "DELETE", cc->activity_url, NULL,
                        NULL, 204 /* No Content */, 404 /* Not Found */, pool);
}


/* Get the version resource URL for RSRC, storing it in
   RSRC->vsn_url.  Use POOL for all temporary allocations. */
static svn_error_t * get_version_url(commit_ctx_t *cc,
                                     version_rsrc_t *rsrc,
                                     svn_boolean_t force,
                                     apr_pool_t *pool)
{
  svn_ra_dav_resource_t *propres;
  const char *url;
  const svn_string_t *url_str;

  if (!force && cc->get_func != NULL)
    {
      const svn_string_t *vsn_url_value;

      SVN_ERR((*cc->get_func)(cc->cb_baton,
                              rsrc->local_path,
                              SVN_RA_DAV__LP_VSN_URL,
                              &vsn_url_value,
                              pool));
      if (vsn_url_value != NULL)
        {
          rsrc->vsn_url = apr_pstrdup(rsrc->pool, vsn_url_value->data);
          return SVN_NO_ERROR;
        }

      /* whoops. it wasn't there. go grab it from the server. */
    }

  if (rsrc->revision == SVN_INVALID_REVNUM)
    {
      /* We aren't trying to get a specific version -- use the HEAD. We
         fetch the version URL from the public URL. */
      url = rsrc->url;
    }
  else
    {
      svn_string_t bc_url;
      svn_string_t bc_relative;

      /* The version URL comes from a resource in the Baseline Collection. */
      SVN_ERR(svn_ra_dav__get_baseline_info(NULL,
                                            &bc_url, &bc_relative, NULL,
                                            cc->ras->sess,
                                            rsrc->url,
                                            rsrc->revision,
                                            pool));

      url = svn_path_url_add_component(bc_url.data, bc_relative.data, pool);
    }

  /* Get the DAV:checked-in property, which contains the URL of the
     Version Resource */
  SVN_ERR(svn_ra_dav__get_props_resource(&propres, cc->ras->sess, url,
                                         NULL, fetch_props, pool));
  url_str = apr_hash_get(propres->propset,
                         SVN_RA_DAV__PROP_CHECKED_IN,
                         APR_HASH_KEY_STRING);
  if (url_str == NULL)
    {
      /* ### need a proper SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              _("Could not fetch the Version Resource URL "
                                "(needed during an import or when it is "
                                "missing from the local, cached props)"));
    }

  /* ensure we get the proper lifetime for this URL since it is going into
     a resource object. */
  rsrc->vsn_url = apr_pstrdup(rsrc->pool, url_str->data);

  if (cc->push_func != NULL)
    {
      /* Now we can store the new version-url. */
      SVN_ERR((*cc->push_func)(cc->cb_baton,
                               rsrc->local_path,
                               SVN_RA_DAV__LP_VSN_URL,
                               url_str,
                               pool));
    }

  return SVN_NO_ERROR;
}

/* When FORCE is true, then we force a query to the server, ignoring any
   cached property. */
static svn_error_t * get_activity_collection(commit_ctx_t *cc,
                                             const svn_string_t **collection,
                                             svn_boolean_t force,
                                             apr_pool_t *pool)
{
  if (!force && cc->get_func != NULL)
    {
      /* with a get_func, we can just ask for the activity URL from the
         property store. */

      /* get the URL where we should create activities */
      SVN_ERR((*cc->get_func)(cc->cb_baton,
                              "",
                              SVN_RA_DAV__LP_ACTIVITY_COLL,
                              collection,
                              pool));

      if (*collection != NULL)
        {
          /* the property was there. return it. */
          return SVN_NO_ERROR;
        }

      /* property not found for some reason. get it from the server. */
    }

  /* use our utility function to fetch the activity URL */
  SVN_ERR(svn_ra_dav__get_activity_collection(collection,
                                              cc->ras,
                                              cc->ras->root.path,
                                              pool));

  if (cc->push_func != NULL)
    {
      /* save the (new) activity collection URL into the directory */
      SVN_ERR((*cc->push_func)(cc->cb_baton,
                               "",
                               SVN_RA_DAV__LP_ACTIVITY_COLL,
                               *collection,
                               pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t * create_activity(commit_ctx_t *cc,
                                     apr_pool_t *pool)
{
  const svn_string_t * activity_collection;
  const char *uuid_buf = svn_uuid_generate(pool);
  int code;
  const char *url;

  /* get the URL where we'll create activities, construct the URL for
     the activity, and create the activity.  The URL for our activity
     will be ACTIVITY_COLL/UUID */
  SVN_ERR(get_activity_collection(cc, &activity_collection, FALSE, pool));
  url = svn_path_url_add_component(activity_collection->data, 
                                   uuid_buf, pool);
  SVN_ERR(simple_request(cc->ras, "MKACTIVITY", url, &code, NULL,
                         201 /* Created */, 404 /* Not Found */, pool));

  /* if we get a 404, then it generally means that the cached activity
     collection no longer exists. Retry the sequence, but force a query
     to the server for the activity collection. */
  if (code == 404)
    {
      SVN_ERR(get_activity_collection(cc, &activity_collection, TRUE, pool));
      url = svn_path_url_add_component(activity_collection->data, 
                                       uuid_buf, pool);
      SVN_ERR(simple_request(cc->ras, "MKACTIVITY", url, &code,
                             NULL, 201, 0, pool));
    }

  cc->activity_url = url;

  return SVN_NO_ERROR;
}

/* Add a child resource.  POOL should be as "temporary" as possible,
   but probably not as far as requiring a new temp pool. */
static svn_error_t * add_child(version_rsrc_t **child,
                               commit_ctx_t *cc,
                               const version_rsrc_t *parent,
                               const char *name,
                               int created,
                               svn_revnum_t revision,
                               apr_pool_t *pool)
{
  version_rsrc_t *rsrc;

  /* ### todo:  This from Yoshiki Hayashi <yoshiki@xemacs.org>:

     Probably created flag in add_child can be removed because
        revision is valid => created is false 
        revision is invalid => created is true
  */

  rsrc = apr_pcalloc(pool, sizeof(*rsrc));
  rsrc->pool = pool;
  rsrc->revision = revision;
  rsrc->url = svn_path_url_add_component(parent->url, name, pool);
  rsrc->local_path = svn_path_join(parent->local_path, name, pool);

  /* Case 1:  the resource is truly "new".  Either it was added as a
     completely new object, or implicitly created via a COPY.  Either
     way, it has no VR URL anywhere.  However, we *can* derive its WR
     URL by the rules of deltaV:  "copy structure is preserved below
     the WR you COPY to."  */
  if (created || (parent->vsn_url == NULL))
    rsrc->wr_url = svn_path_url_add_component(parent->wr_url, name, pool);

  /* Case 2: the resource is already under version-control somewhere.
     This means it has a VR URL already, and the WR URL won't exist
     until it's "checked out". */
  else
    SVN_ERR(get_version_url(cc, rsrc, FALSE, pool));

  *child = rsrc;
  return SVN_NO_ERROR;
}

#ifdef SVN_NEON_0_25
/* This implements the svn_ra_dav__request_interrogator() interface.
   USERDATA is 'char **'. */
static svn_error_t *interrogate_for_location(ne_request *request,
                                             int dispatch_return_val,
                                             void *userdata)
{
  char **location = userdata;

  if (location)
    {
      const char *val = ne_get_response_header(request, "location");
      if (val)
        *location = strdup(val);
    }

  return SVN_NO_ERROR;
}
#endif /* SVN_NEON_0_25 */


static svn_error_t * do_checkout(commit_ctx_t *cc,
                                 const char *vsn_url,
                                 svn_boolean_t allow_404,
                                 const char *token,
                                 int *code,
                                 char **locn,
                                 apr_pool_t *pool)
{
  ne_request *req;
  const char *body;
  svn_error_t *err;

  /* assert: vsn_url != NULL */

  /* ### send a CHECKOUT resource on vsn_url; include cc->activity_url;
     ### place result into res->wr_url and return it */

  /* create/prep the request */
  req = ne_request_create(cc->ras->sess, "CHECKOUT", vsn_url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_DAV_CREATING_REQUEST, NULL,
                               _("Could not create a CHECKOUT request (%s)"),
                               vsn_url);
    }

  /* ### store this into cc to avoid pool growth */
  body = apr_psprintf(pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<D:checkout xmlns:D=\"DAV:\">"
                      "<D:activity-set>"
                      "<D:href>%s</D:href>"
                      "</D:activity-set></D:checkout>", cc->activity_url);
  ne_set_request_body_buffer(req, body, strlen(body));

#ifndef SVN_NEON_0_25
  /* 
   * We have different const qualifiers here. locn is const char **,
   * but the prototype is void * (as opposed to const void *).
   */
  ne_add_response_header_handler(req, "location",
                                 ne_duplicate_header, (void *)locn);
#endif /* ! SVN_NEON_0_25 */

  if (token)
    {
      const char *token_header_val;
      token_header_val = apr_psprintf(pool, "(<%s>)", token);
      ne_add_request_header(req, "If", token_header_val);
    }

  /* run the request and get the resulting status code (and svn_error_t) */
  err = svn_ra_dav__request_dispatch(code, req, cc->ras->sess,
                                     "CHECKOUT", vsn_url,
                                     201 /* Created */,
                                     allow_404 ? 404 /* Not Found */ : 0,
#ifdef SVN_NEON_0_25
                                     interrogate_for_location, locn,
#endif /* SVN_NEON_0_25 */
                                     pool);

  return err;
}


static svn_error_t * checkout_resource(commit_ctx_t *cc,
                                       version_rsrc_t *rsrc,
                                       svn_boolean_t allow_404,
                                       const char *token,
                                       apr_pool_t *pool)
{
  int code;
  char *locn = NULL;
  ne_uri parse;
  svn_error_t *err;

  if (rsrc->wr_url != NULL)
    {
      /* already checked out! */
      return NULL;
    }

  /* check out the Version Resource */
  err = do_checkout(cc, rsrc->vsn_url, allow_404, token, &code, &locn, pool);

  /* possibly run the request again, with a re-fetched Version Resource */
  if (err == NULL && allow_404 && code == 404)
    {
      /* free the LOCN if it got assigned. */
      if (locn)
        free(locn);

      /* re-fetch, forcing a query to the server */
      SVN_ERR(get_version_url(cc, rsrc, TRUE, pool));

      /* do it again, but don't allow a 404 this time */
      err = do_checkout(cc, rsrc->vsn_url, FALSE, token, &code, &locn, pool);
    }

  /* special-case when conflicts occur */
  if (err)
    {
      /* free the LOCN if it got assigned. */
      if (locn)
        free(locn);
      
      if (err->apr_err == SVN_ERR_FS_CONFLICT)
        return svn_error_createf
          (err->apr_err, err,
           _("Your file or directory '%s' is probably out-of-date"),
           svn_path_local_style(rsrc->local_path, pool));
      return err;
    }

  /* we got the header, right? */
  if (locn == NULL)
    return svn_error_create(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                            _("The CHECKOUT response did not contain a "
                              "'Location:' header"));

  /* The location is an absolute URI. We want just the path portion. */
  /* ### what to do with the rest? what if it points somewhere other
     ### than the current session? */
  ne_uri_parse(locn, &parse);
  rsrc->wr_url = apr_pstrdup(rsrc->pool, parse.path);
  ne_uri_free(&parse);
  free(locn);

  return SVN_NO_ERROR;
}

static void record_prop_change(apr_pool_t *pool,
                               resource_baton_t *r,
                               const char *name,
                               const svn_string_t *value)
{
  /* copy the name into the pool so we get the right lifetime (who knows
     what the caller will do with it) */
  name = apr_pstrdup(pool, name);

  if (value)
    {
      /* changed/new property */
      if (r->prop_changes == NULL)
        r->prop_changes = apr_hash_make(pool);
      apr_hash_set(r->prop_changes, name, APR_HASH_KEY_STRING,
                   svn_string_dup(value, pool));
    }
  else
    {
      /* deleted property. */
      if (r->prop_deletes == NULL)
        r->prop_deletes = apr_array_make(pool, 5, sizeof(char *));
      *(const char **)apr_array_push(r->prop_deletes) = name;
    }
}

/* 
A very long note about enforcing directory-up-to-dateness when
proppatching, writ by Ben: 

Once upon a time, I thought it would be necessary to attach the
X-SVN-Version-Name header to every PROPPATCH request we send.  This
would allow mod_dav_svn to verify that a directory is up-to-date.

But it turns out that mod_dav_svn screams and errors if you *ever* try
to CHECKOUT an out-of-date VR.  And furthermore, a directory is never
a 'committable' (according to svn_client_commit) unless it has a
propchange.  Therefore:

1. when ra_dav's commit editor attempts to CHECKOUT a parent directory
   because some child is being added or deleted, it's *unable* to get
   the VR cache, and thus just gets the HEAD one instead.  So it ends
   up always doing a CHECKOUT of the latest version of the directory.
   This is actually fine; Subversion's semantics allow us to
   add/delete children on out-of-date directories.  If, in dav terms,
   this means always checking out the latest directory, so be it.  Any
   namespace conflicts will be detected with the actual PUT or DELETE
   of the child.

2. when ra_dav's commit editor receives a directory propchange, it
   *is* able to get the VR cache (because the dir is a "committable"),
   and thus it does a CHECKOUT of the older directory.  And mod_dav_svn
   will scream if the VR is out-of-date, which is exactly what we want in
   the directory propchange scenario.

The only potential badness here is the case of committing a directory
with a propchange, and an add/rm of its child.  This commit should
fail, due to the out-of-date propchange.  However, it's *possible*
that it will fail for a different reason:  we might attempt the add/rm
first, which means checking out the parent VR, which *would* be
available from the cache, and thus we get an early error.  Instead of
seeing an error about 'cannot proppatch out-of-date dir', the user
will see an error about 'cannot checkout out-of-date parent'.  Not
really a big deal I guess.

*/
static svn_error_t * do_proppatch(svn_ra_dav__session_t *ras,
                                  const version_rsrc_t *rsrc,
                                  resource_baton_t *rb,
                                  apr_pool_t *pool)
{
  const char *url = rsrc->wr_url;
  apr_hash_t *extra_headers = NULL;
  
  if (rb->token)
    {
      const char *token_header_val;
      token_header_val = apr_psprintf(pool, "(<%s>)", rb->token);
      
      extra_headers = apr_hash_make(pool);
      apr_hash_set(extra_headers, "If", APR_HASH_KEY_STRING,
                   token_header_val);
    }

  return svn_ra_dav__do_proppatch(ras, url, rb->prop_changes, 
                                  rb->prop_deletes, extra_headers, pool);
}


static void
add_valid_target(commit_ctx_t *cc,
                 const char *path,
                 enum svn_recurse_kind kind)
{
  apr_hash_t *hash = cc->valid_targets;
  svn_string_t *path_str = svn_string_create(path, apr_hash_pool_get(hash));
  apr_hash_set(hash, path_str->data, path_str->len, (void*)kind);
}



static svn_error_t * commit_open_root(void *edit_baton,
                                      svn_revnum_t base_revision,
                                      apr_pool_t *dir_pool,
                                      void **root_baton)
{
  commit_ctx_t *cc = edit_baton;
  resource_baton_t *root;
  version_rsrc_t *rsrc;

  /* create the root resource. no wr_url (yet). */
  rsrc = apr_pcalloc(dir_pool, sizeof(*rsrc));
  rsrc->pool = dir_pool;

  /* ### should this be 'base_revision' here? we might not always be
     ### working against the head! (think "properties"). */
  rsrc->revision = SVN_INVALID_REVNUM;

  rsrc->url = cc->ras->root.path;
  rsrc->local_path = "";

  SVN_ERR(get_version_url(cc, rsrc, FALSE, dir_pool));

  root = apr_pcalloc(dir_pool, sizeof(*root));
  root->pool = dir_pool;
  root->cc = cc;
  root->rsrc = rsrc;
  root->created = FALSE;

  *root_baton = root;

  return SVN_NO_ERROR;
}

 
/* Helper func for commit_delete_entry.  Find all keys in LOCK_TOKENS
   which are children of DIR.  Returns the keys (and their vals) in
   CHILD_TOKENS.   No keys or values are reallocated or dup'd.  If no
   keys are children, then return an empty hash.  Use POOL to allocate
   new hash. */
static apr_hash_t *get_child_tokens(apr_hash_t *lock_tokens,
                                    const char *dir,
                                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *tokens = apr_hash_make(pool);
  apr_pool_t *subpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, lock_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, &klen, &val);

      if (svn_path_is_child(dir, key, subpool))
        apr_hash_set(tokens, key, klen, val);
    }

  svn_pool_destroy(subpool);
  return tokens;
}


static svn_error_t * commit_delete_entry(const char *path,
                                         svn_revnum_t revision,
                                         void *parent_baton,
                                         apr_pool_t *pool)
{
  resource_baton_t *parent = parent_baton;
  const char *name = svn_path_basename(path, pool);
  apr_hash_t *extra_headers = NULL;
  const char *child;
  int code;
  svn_error_t *serr;

  if (SVN_IS_VALID_REVNUM(revision))
    {
      const char *revstr = apr_psprintf(pool, "%ld", revision);

      if (! extra_headers)
        extra_headers = apr_hash_make(pool);

      apr_hash_set(extra_headers, SVN_DAV_VERSION_NAME_HEADER,
                   APR_HASH_KEY_STRING, revstr);
    }

  /* get the URL to the working collection */
  SVN_ERR(checkout_resource(parent->cc, parent->rsrc, TRUE, NULL, pool));

  /* create the URL for the child resource */
  child = svn_path_url_add_component(parent->rsrc->wr_url, name, pool);

  /* Start out assuming that we're deleting a file;  try to lookup the
     path itself in the token-hash, and if found, attach it to the If:
     header. */
  if (parent->cc->tokens)
    {
      const char *token = 
        apr_hash_get(parent->cc->tokens, path, APR_HASH_KEY_STRING);

      if (token)
        {
          const char *token_header_val;
          const char *token_uri;

          token_uri = svn_path_url_add_component(parent->cc->ras->url->data,
                                                 path, pool);
          token_header_val = apr_psprintf(pool, "<%s> (<%s>)",
                                          token_uri, token);
          extra_headers = apr_hash_make(pool);
          apr_hash_set(extra_headers, "If", APR_HASH_KEY_STRING,
                       token_header_val);
        }
    }

  /* dav_method_delete() always calls dav_unlock(), but if the svn
     client passed --no-unlock to 'svn commit', then we need to send a
     header which prevents mod_dav_svn from actually doing the unlock. */
  if (parent->cc->keep_locks)
    {
      if (! extra_headers)
        extra_headers = apr_hash_make(pool);

      apr_hash_set(extra_headers, SVN_DAV_OPTIONS_HEADER,
                   APR_HASH_KEY_STRING, SVN_DAV_OPTION_KEEP_LOCKS);
    }

  /* 404 is ignored, because mod_dav_svn is effectively merging
     against the HEAD revision on-the-fly.  In such a universe, a
     failed deletion (because it's already missing) is OK;  deletion
     is an idempotent merge operation. */
  serr =  simple_request(parent->cc->ras, "DELETE", child, &code,
                         extra_headers,
                         204 /* Created */, 404 /* Not Found */, pool);

  /* A locking-related error most likely means we were deleting a
     directory rather than a file, and didn't send all of the
     necessary lock-tokens within the directory. */
  if (serr && ((serr->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)
               || (serr->apr_err == SVN_ERR_FS_NO_LOCK_TOKEN)
               || (serr->apr_err == SVN_ERR_FS_LOCK_OWNER_MISMATCH)
               || (serr->apr_err == SVN_ERR_FS_PATH_ALREADY_LOCKED)))
    {
      /* Re-attempt the DELETE request as if the path were a directory.
         Discover all lock-tokens within the directory, and send them in
         the body of the request (which is normally empty).  Of course,
         if we don't *find* any additional lock-tokens, don't bother to
         retry (it ain't gonna do any good).
         
         Note that we're not sending the locks in the If: header, for
         the same reason we're not sending in MERGE's headers: httpd has
       limits on the amount of data it's willing to receive in headers. */
      
      apr_hash_t *child_tokens = NULL;
      ne_request *req;
      const char *body;
      const char *token;
      svn_stringbuf_t *locks_list;
      
      if (parent->cc->tokens)
        child_tokens = get_child_tokens(parent->cc->tokens, path, pool);
      
      /* No kiddos?  Return the original error.  Else, clear it so it
         doesn't get leaked.  */
      if ((! child_tokens) 
          || (apr_hash_count(child_tokens) == 0))
        return serr;
      else
        svn_error_clear(serr);
      
      /* In preparation of directory locks, go ahead and add the actual
         target's lock token to those of its children. */
      if ((token = apr_hash_get(parent->cc->tokens, path, 
                                APR_HASH_KEY_STRING)))
        apr_hash_set(child_tokens, path, APR_HASH_KEY_STRING, token);
      
      SVN_ERR(svn_ra_dav__assemble_locktoken_body(&locks_list,
                                                  child_tokens, pool));
      
      req = ne_request_create(parent->cc->ras->sess, "DELETE", child);
      if (req == NULL)
        return svn_error_createf(SVN_ERR_RA_DAV_CREATING_REQUEST, NULL,
                                 _("Could not create a DELETE request (%s)"),
                                 child);
      
      body = apr_psprintf(pool, 
                          "<?xml version=\"1.0\" encoding=\"utf-8\"?> %s",
                          locks_list->data);
      ne_set_request_body_buffer(req, body, strlen(body));
      
      /* Don't use SVN_ERR() here because some preprocessors can't
         handle a compile-time conditional inside a macro call. */
      serr = svn_ra_dav__request_dispatch(&code, req, parent->cc->ras->sess,
                                          "DELETE", child,
                                          204 /* Created */,
                                          404 /* Not Found */,
#ifdef SVN_NEON_0_25
                                          NULL, NULL,
#endif /* SVN_NEON_0_25 */
                                          pool);
      SVN_ERR(serr);
    }
  else if (serr)
    return serr;

  /* Add this path to the valid targets hash. */
  add_valid_target(parent->cc, path, svn_nonrecursive);
  
  return SVN_NO_ERROR;
}



/* A callback of type ne_create_request_fn;  called whenever neon
   creates a request. */
static void 
create_request_hook(ne_request *req,
                    void *userdata,
                    const char *method,
                    const char *requri)
{
  struct copy_baton *cb = userdata;

  if (strcmp(method, "COPY") == 0)
    cb->making_a_copy = TRUE;
}


/* A callback of type ne_pre_send_fn;  called whenever neon is just
   about to send a COPY request. */
static void
pre_send_hook(ne_request *req,
              void *userdata,
              ne_buffer *header)
{
  struct copy_baton *cb = userdata;

  if (cb->making_a_copy)
    {
      cb->error_parser = ne_xml_create();
      svn_ra_dav__add_error_handler(req, cb->error_parser,
                                    &(cb->err), cb->pool);
    }
}


static svn_error_t * commit_add_dir(const char *path,
                                    void *parent_baton,
                                    const char *copyfrom_path,
                                    svn_revnum_t copyfrom_revision,
                                    apr_pool_t *dir_pool,
                                    void **child_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *child;
  int code;
  const char *name = svn_path_basename(path, dir_pool);
  apr_pool_t *workpool = svn_pool_create(dir_pool);
  version_rsrc_t *rsrc = NULL;

  /* check out the parent resource so that we can create the new collection
     as one of its children. */
  SVN_ERR(checkout_resource(parent->cc, parent->rsrc, TRUE, NULL, dir_pool));

  /* create a child object that contains all the resource urls */
  child = apr_pcalloc(dir_pool, sizeof(*child));
  child->pool = dir_pool;
  child->cc = parent->cc;
  child->created = TRUE;
  SVN_ERR(add_child(&rsrc, parent->cc, parent->rsrc,
                    name, 1, SVN_INVALID_REVNUM, workpool));
  child->rsrc = dup_resource(rsrc, dir_pool);

  if (! copyfrom_path)
    {
      /* This a new directory with no history, so just create a new,
         empty collection */
      SVN_ERR(simple_request(parent->cc->ras, "MKCOL", child->rsrc->wr_url,
                             &code, NULL, 201 /* Created */, 0, workpool));
    }
  else
    {
      svn_string_t bc_url, bc_relative;
      const char *copy_src;
      int status;

      /* This add has history, so we need to do a COPY. */
      
      /* Convert the copyfrom_* url/rev "public" pair into a Baseline
         Collection (BC) URL that represents the revision -- and a
         relative path under that BC.  */
      SVN_ERR(svn_ra_dav__get_baseline_info(NULL,
                                            &bc_url, &bc_relative, NULL,
                                            parent->cc->ras->sess,
                                            copyfrom_path,
                                            copyfrom_revision,
                                            workpool));


      /* Combine the BC-URL and relative path; this is the main
         "source" argument to the COPY request.  The "Destination:"
         header given to COPY is simply the wr_url that is already
         part of the child object. */
      copy_src = svn_path_url_add_component(bc_url.data,
                                            bc_relative.data, 
                                            workpool);

      /* Have neon do the COPY. */
      status = ne_copy(parent->cc->ras->sess,
                       1,                   /* overwrite */
                       NE_DEPTH_INFINITE,   /* always copy dirs deeply */
                       copy_src,            /* source URI */
                       child->rsrc->wr_url); /* dest URI */

      /* Did we get a <D:error> response? */
      if (parent->cc->cb->err)
        {
          if (parent->cc->cb->error_parser)
            ne_xml_destroy(parent->cc->cb->error_parser);
          return parent->cc->cb->err;
        }

      /* Did we get some error from neon? */
      if (status != NE_OK)
        {
          const char *msg = apr_psprintf(dir_pool, "COPY of %s", path);
          if (parent->cc->cb->error_parser)
            ne_xml_destroy(parent->cc->cb->error_parser);
          return svn_ra_dav__convert_error(parent->cc->ras->sess,
                                           msg, status, workpool);
        }

      if (parent->cc->cb->error_parser)
        ne_xml_destroy(parent->cc->cb->error_parser);
    }

  /* Add this path to the valid targets hash. */
  add_valid_target(parent->cc, path, 
                   copyfrom_path ? svn_recursive : svn_nonrecursive);

  svn_pool_destroy(workpool);
  *child_baton = child;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_open_dir(const char *path,
                                     void *parent_baton,
                                     svn_revnum_t base_revision,
                                     apr_pool_t *dir_pool,
                                     void **child_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *child = apr_pcalloc(dir_pool, sizeof(*child));
  const char *name = svn_path_basename(path, dir_pool);
  apr_pool_t *workpool = svn_pool_create(dir_pool);
  version_rsrc_t *rsrc = NULL;

  child->pool = dir_pool;
  child->cc = parent->cc;
  child->created = FALSE;

  SVN_ERR(add_child(&rsrc, parent->cc, parent->rsrc,
                    name, 0, base_revision, workpool));
  child->rsrc = dup_resource(rsrc, dir_pool);

  /*
  ** Note: open_dir simply means that a change has occurred somewhere
  **       within this directory. We have nothing to do, to prepare for
  **       those changes (each will be considered independently).
  **
  ** Note: if a directory is replaced by something else, then this callback
  **       will not be used: a true replacement is modeled with a "delete"
  **       followed by an "add".
  */
  svn_pool_destroy(workpool);
  *child_baton = child;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_change_dir_prop(void *dir_baton,
                                            const char *name,
                                            const svn_string_t *value,
                                            apr_pool_t *pool)
{
  resource_baton_t *dir = dir_baton;

  /* record the change. it will be applied at close_dir time. */
  /* ### we should put this into the dir_baton's pool */
  record_prop_change(dir->pool, dir, name, value);

  /* do the CHECKOUT sooner rather than later */
  SVN_ERR(checkout_resource(dir->cc, dir->rsrc, TRUE, NULL, pool));

  /* Add this path to the valid targets hash. */
  add_valid_target(dir->cc, dir->rsrc->local_path, svn_nonrecursive);

  return SVN_NO_ERROR;
}

static svn_error_t * commit_close_dir(void *dir_baton,
                                      apr_pool_t *pool)
{
  resource_baton_t *dir = dir_baton;

  /* Perform all of the property changes on the directory. Note that we
     checked out the directory when the first prop change was noted. */
  SVN_ERR(do_proppatch(dir->cc->ras, dir->rsrc, dir, pool));

  return SVN_NO_ERROR;
}

static svn_error_t * commit_add_file(const char *path,
                                     void *parent_baton,
                                     const char *copyfrom_path,
                                     svn_revnum_t copyfrom_revision,
                                     apr_pool_t *file_pool,
                                     void **file_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *file;
  const char *name = svn_path_basename(path, file_pool);
  apr_pool_t *workpool = svn_pool_create(file_pool);
  version_rsrc_t *rsrc = NULL;

  /*
  ** To add a new file into the repository, we CHECKOUT the parent
  ** collection, then PUT the file as a member of the resuling working
  ** collection.
  **
  ** If the file was copied from elsewhere, then we will use the COPY
  ** method to copy into the working collection.
  */

  /* Do the parent CHECKOUT first */
  SVN_ERR(checkout_resource(parent->cc, parent->rsrc, TRUE, NULL, workpool));

  /* Construct a file_baton that contains all the resource urls. */
  file = apr_pcalloc(file_pool, sizeof(*file));
  file->pool = file_pool;
  file->cc = parent->cc;
  file->created = TRUE;
  SVN_ERR(add_child(&rsrc, parent->cc, parent->rsrc,
                    name, 1, SVN_INVALID_REVNUM, workpool));
  file->rsrc = dup_resource(rsrc, file_pool);
  if (parent->cc->tokens)
    file->token = apr_hash_get(parent->cc->tokens, path, APR_HASH_KEY_STRING);
  
  /* If the parent directory existed before this commit then there may be a
     file with this URL already. We need to ensure such a file does not
     exist, which we do by attempting a PROPFIND.  Of course, a
     PROPFIND *should* succeed if this "add" is actually the second
     half of a "replace".  

     ### For now, we'll assume that if this path has already been
     added to the valid targets hash, that addition occurred during the
     "delete" phase (if that's not the case, this editor is being
     driven incorrectly, as we should never visit the same path twice
     except in a delete+add situation). */
  if ((! parent->created) 
      && (! apr_hash_get(file->cc->valid_targets, path, APR_HASH_KEY_STRING)))
    {
      svn_ra_dav_resource_t *res;
      svn_error_t *err = svn_ra_dav__get_starting_props(&res,
                                                        file->cc->ras->sess,
                                                        file->rsrc->url, NULL,
                                                        workpool);
      if (!err)
        {
          /* If the PROPFIND succeeds the file already exists */
          return svn_error_createf(SVN_ERR_RA_DAV_ALREADY_EXISTS, NULL,
                                   _("File '%s' already exists"), 
                                   file->rsrc->url);
        }
      else if (err->apr_err == SVN_ERR_RA_DAV_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
        }
      else
        {
          /* A real error */
          return err;
        }
    }

  if (! copyfrom_path)
    {
      /* This a truly new file. */

      /* ### wait for apply_txdelta before doing a PUT. it might arrive a
         ### "long time" from now. certainly after many other operations, so
         ### we don't want to start a PUT just yet.
         ### so... anything else to do here?
      */
    }
  else
    {
      svn_string_t bc_url, bc_relative;
      const char *copy_src;
      int status;

      /* This add has history, so we need to do a COPY. */
      
      /* Convert the copyfrom_* url/rev "public" pair into a Baseline
         Collection (BC) URL that represents the revision -- and a
         relative path under that BC.  */
      SVN_ERR(svn_ra_dav__get_baseline_info(NULL,
                                            &bc_url, &bc_relative, NULL,
                                            parent->cc->ras->sess,
                                            copyfrom_path,
                                            copyfrom_revision,
                                            workpool));


      /* Combine the BC-URL and relative path; this is the main
         "source" argument to the COPY request.  The "Destination:"
         header given to COPY is simply the wr_url that is already
         part of the file_baton. */
      copy_src = svn_path_url_add_component(bc_url.data, 
                                            bc_relative.data, 
                                            workpool);

      /* Have neon do the COPY. */
      status = ne_copy(parent->cc->ras->sess,
                       1,                   /* overwrite */
                       NE_DEPTH_ZERO,       /* for a file, does it care? */
                       copy_src,            /* source URI */
                       file->rsrc->wr_url); /* dest URI */

      /* Did we get a <D:error> response? */
      if (parent->cc->cb->err)
        {
          if (parent->cc->cb->error_parser)
            ne_xml_destroy(parent->cc->cb->error_parser);
          return parent->cc->cb->err;
        }

      /* Did we get some error from neon? */
      if (status != NE_OK)
        {
          const char *msg = apr_psprintf(file_pool, "COPY of %s", path);
          if (parent->cc->cb->error_parser)
            ne_xml_destroy(parent->cc->cb->error_parser);
          return svn_ra_dav__convert_error(parent->cc->ras->sess,
                                           msg, status, workpool);
        }
      
      if (parent->cc->cb->error_parser)
        ne_xml_destroy(parent->cc->cb->error_parser);
    }

  /* Add this path to the valid targets hash. */
  add_valid_target(parent->cc, path, svn_nonrecursive);

  svn_pool_destroy(workpool);

  /* return the file_baton */
  *file_baton = file;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_open_file(const char *path,
                                      void *parent_baton,
                                      svn_revnum_t base_revision,
                                      apr_pool_t *file_pool,
                                      void **file_baton)
{
  resource_baton_t *parent = parent_baton;
  resource_baton_t *file;
  const char *name = svn_path_basename(path, file_pool);
  apr_pool_t *workpool = svn_pool_create(file_pool);
  version_rsrc_t *rsrc = NULL;

  file = apr_pcalloc(file_pool, sizeof(*file));
  file->pool = file_pool;
  file->cc = parent->cc;
  file->created = FALSE;
  SVN_ERR(add_child(&rsrc, parent->cc, parent->rsrc,
                    name, 0, base_revision, workpool));
  file->rsrc = dup_resource(rsrc, file_pool);
  if (parent->cc->tokens)
    file->token = apr_hash_get(parent->cc->tokens, path, APR_HASH_KEY_STRING);

  /* do the CHECKOUT now. we'll PUT the new file contents later on. */
  SVN_ERR(checkout_resource(parent->cc, file->rsrc, TRUE,
                            file->token, workpool));

  /* ### wait for apply_txdelta before doing a PUT. it might arrive a
     ### "long time" from now. certainly after many other operations, so
     ### we don't want to start a PUT just yet.
     ### so... anything else to do here? what about the COPY case?
  */

  svn_pool_destroy(workpool);
  *file_baton = file;
  return SVN_NO_ERROR;
}

static svn_error_t * commit_stream_write(void *baton,
                                         const char *data, 
                                         apr_size_t *len)
{
  put_baton_t *pb = baton;
  apr_status_t status;

  /* drop the data into our temp file */
  status = apr_file_write_full(pb->tmpfile, data, *len, NULL);
  if (status)
    return svn_error_wrap_apr(status, 
                              _("Could not write svndiff to temp file"));

  return SVN_NO_ERROR;
}

static svn_error_t * 
commit_apply_txdelta(void *file_baton, 
                     const char *base_checksum,
                     apr_pool_t *pool,
                     svn_txdelta_window_handler_t *handler,
                     void **handler_baton)
{
  resource_baton_t *file = file_baton;
  put_baton_t *baton;
  svn_stream_t *stream;

  baton = apr_pcalloc(file->pool, sizeof(*baton));
  file->put_baton = baton;

  if (base_checksum)
    baton->base_checksum = apr_pstrdup(file->pool, base_checksum);
  else
    baton->base_checksum = NULL;

  /* ### oh, hell. Neon's request body support is either text (a C string),
     ### or a FILE*. since we are getting binary data, we must use a FILE*
     ### for now. isn't that special? */

  /* Use the client callback to create a tmpfile. */
  SVN_ERR(file->cc->ras->callbacks->open_tmp_file 
          (&baton->tmpfile, 
           file->cc->ras->callback_baton,
           file->pool));

  /* ### register a cleanup on file_pool which closes the file; this
     ### will ensure that the file always gets tossed, even if we exit
     ### with an error. */

  stream = svn_stream_create(baton, pool);
  svn_stream_set_write(stream, commit_stream_write);

  svn_txdelta_to_svndiff(stream, pool, handler, handler_baton);

  /* Add this path to the valid targets hash. */
  add_valid_target(file->cc, file->rsrc->local_path, svn_nonrecursive);

  return SVN_NO_ERROR;
}

static svn_error_t * commit_change_file_prop(void *file_baton,
                                             const char *name,
                                             const svn_string_t *value,
                                             apr_pool_t *pool)
{
  resource_baton_t *file = file_baton;

  /* record the change. it will be applied at close_file time. */
  /* ### we should put this into the file_baton's pool */
  record_prop_change(file->pool, file, name, value);

  /* do the CHECKOUT sooner rather than later */
  SVN_ERR(checkout_resource(file->cc, file->rsrc, TRUE, file->token, pool));

  /* Add this path to the valid targets hash. */
  add_valid_target(file->cc, file->rsrc->local_path, svn_nonrecursive);

  return SVN_NO_ERROR;
}

static svn_error_t * commit_close_file(void *file_baton,
                                       const char *text_checksum,
                                       apr_pool_t *pool)
{
  resource_baton_t *file = file_baton;
  commit_ctx_t *cc = file->cc;

  if (file->put_baton)
    {
      ne_session *sess = cc->ras->sess;
      put_baton_t *pb = file->put_baton;
      const char *url = file->rsrc->wr_url;
      ne_request *req;
      int code;
      svn_error_t *err;

      /* create/prep the request */
      req = ne_request_create(sess, "PUT", url);
      if (req == NULL)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_CREATING_REQUEST, NULL,
                                   _("Could not create a PUT request (%s)"),
                                   url);
        }
      
      ne_add_request_header(req, "Content-Type", SVN_SVNDIFF_MIME_TYPE);
      
      if (file->token)
        {
          const char *token_header_val;
          const char *token_uri;

          token_uri = svn_path_url_add_component(cc->ras->url->data,
                                                 file->rsrc->url, pool);
          token_header_val = apr_psprintf(pool, "<%s> (<%s>)",
                                          token_uri, file->token);
          ne_add_request_header(req, "If", token_header_val);
        }

      if (pb->base_checksum)
        ne_add_request_header
          (req, SVN_DAV_BASE_FULLTEXT_MD5_HEADER, pb->base_checksum);
      
      if (text_checksum)
        ne_add_request_header
          (req, SVN_DAV_RESULT_FULLTEXT_MD5_HEADER, text_checksum);
      
      /* Give the file to neon. The provider will rewind the file. */
      err = svn_ra_dav__set_neon_body_provider(req, pb->tmpfile);
      if (err)
        {
          apr_file_close(pb->tmpfile);
          ne_request_destroy(req);
          return err;
        }
      
      /* run the request and get the resulting status code (and svn_error_t) */
      err = svn_ra_dav__request_dispatch(&code, req, sess, "PUT", url,
                                         201 /* Created */,
                                         204 /* No Content */,
#ifdef SVN_NEON_0_25
                                         NULL, NULL,
#endif /* SVN_NEON_0_25 */
                                         pool);
      
      /* we're done with the file.  this should delete it. */
      (void) apr_file_close(pb->tmpfile);
      
      if (err)
        return err;
    }

  /* Perform all of the property changes on the file. Note that we
     checked out the file when the first prop change was noted. */
  SVN_ERR(do_proppatch(cc->ras, file->rsrc, file, pool));

  return SVN_NO_ERROR;
}


static svn_error_t * commit_close_edit(void *edit_baton,
                                       apr_pool_t *pool)
{
  commit_ctx_t *cc = edit_baton;
  svn_commit_info_t *commit_info = svn_create_commit_info(pool);

  SVN_ERR(svn_ra_dav__merge_activity(&(commit_info->revision),
                                     &(commit_info->date),
                                     &(commit_info->author),
                                     &(commit_info->post_commit_err),
                                     cc->ras,
                                     cc->ras->root.path,
                                     cc->activity_url,
                                     cc->valid_targets,
                                     cc->tokens,
                                     cc->keep_locks,
                                     cc->disable_merge_response,
                                     pool));
  SVN_ERR(delete_activity(edit_baton, pool));
  SVN_ERR(svn_ra_dav__maybe_store_auth_info(cc->ras, pool));

  if (commit_info->revision != SVN_INVALID_REVNUM)
    SVN_ERR(cc->callback(commit_info, cc->callback_baton, pool));

  return SVN_NO_ERROR;
}


static svn_error_t * commit_abort_edit(void *edit_baton,
                                       apr_pool_t *pool)
{
  return delete_activity(edit_baton, pool);
}


static svn_error_t * apply_log_message(commit_ctx_t *cc,
                                       const char *log_msg,
                                       apr_pool_t *pool)
{
  const svn_string_t *vcc;
  const svn_string_t *baseline_url;
  version_rsrc_t baseline_rsrc = { SVN_INVALID_REVNUM };
  ne_proppatch_operation po[2] = { { 0 } };
  int rv;
  svn_stringbuf_t *xml_data;
  svn_error_t *err = NULL;
  int retry_count = 5;
  
  /* ### this whole sequence can/should be replaced with an expand-property
     ### REPORT when that is available on the server. */

  /* fetch the DAV:version-controlled-configuration from the session's URL */
  SVN_ERR(svn_ra_dav__get_one_prop(&vcc, cc->ras->sess, cc->ras->root.path, 
                                   NULL, &svn_ra_dav__vcc_prop, pool));

  /* ### we should use DAV:apply-to-version on the CHECKOUT so we can skip
     ### retrieval of the baseline */

  do {

    svn_error_clear(err);

    /* Get the latest baseline from VCC's DAV:checked-in property.
       This should give us the HEAD revision of the moment. */
    SVN_ERR(svn_ra_dav__get_one_prop(&baseline_url, cc->ras->sess,
                                     vcc->data, NULL,
                                     &svn_ra_dav__checked_in_prop, pool));
    baseline_rsrc.pool = pool;
    baseline_rsrc.vsn_url = baseline_url->data;
    
    /* To set the log message, we must checkout the latest baseline
       and get back a mutable "working" baseline.  */
    err = checkout_resource(cc, &baseline_rsrc, FALSE, NULL, pool);

    /* There's a small chance of a race condition here, if apache is
       experiencing heavy commit concurrency or if the network has
       long latency.  It's possible that the value of HEAD changed
       between the time we fetched the latest baseline and the time we
       checkout that baseline.  If that happens, apache will throw us
       a BAD_BASELINE error (deltaV says you can only checkout the
       latest baseline).  We just ignore that specific error and
       retry a few times, asking for the latest baseline again. */
    if (err && err->apr_err != SVN_ERR_APMOD_BAD_BASELINE)
      return err;

  } while (err && (--retry_count > 0));

  /* Yikes, if we couldn't hold onto HEAD after a few retries, throw a
     real error.*/
  if (err)
    return err;

  /* XML-Escape the log message. */
  xml_data = NULL;           /* Required by svn_xml_escape_*. */
  svn_xml_escape_cdata_cstring(&xml_data, log_msg, pool);

  po[0].name = &log_message_prop;
  po[0].type = ne_propset;
  po[0].value = xml_data->data;

  rv = ne_proppatch(cc->ras->sess, baseline_rsrc.wr_url, po);
  if (rv != NE_OK)
    {
      const char *msg = apr_psprintf(pool, _("applying log message to %s"),
                                     baseline_rsrc.wr_url);
      return svn_ra_dav__convert_error(cc->ras->sess, msg, rv, pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_commit_editor(svn_ra_session_t *session,
                                            const svn_delta_editor_t **editor,
                                            void **edit_baton,
                                            const char *log_msg,
                                            svn_commit_callback2_t callback,
                                            void *callback_baton,
                                            apr_hash_t *lock_tokens,
                                            svn_boolean_t keep_locks,
                                            apr_pool_t *pool)
{
  svn_ra_dav__session_t *ras = session->priv;
  svn_delta_editor_t *commit_editor;
  commit_ctx_t *cc;

  /* Only initialize the baton the first time through. */
  if (! ras->cb)
    {
      /* Build a copy_baton for COPY requests. */
      ras->cb = apr_pcalloc(ras->pool, sizeof(*ras->cb));

      /* Register request hooks in the neon session.  They specifically
         allow any COPY requests (ne_copy()) to parse <D:error>
         responses.  They're no-ops for other requests. */
      ne_hook_create_request(ras->sess, create_request_hook, ras->cb);
      ne_hook_pre_send(ras->sess, pre_send_hook, ras->cb);
    }

  /* Make sure the baton uses our current pool, so we don't leak. */
  ras->cb->pool = pool;

  /* Build the main commit editor's baton. */
  cc = apr_pcalloc(pool, sizeof(*cc));
  cc->ras = ras;
  cc->valid_targets = apr_hash_make(pool);
  cc->get_func = ras->callbacks->get_wc_prop;
  cc->push_func = ras->callbacks->push_wc_prop;
  cc->cb_baton = ras->callback_baton;
  cc->log_msg = log_msg;
  cc->callback = callback;
  cc->callback_baton = callback_baton;
  cc->tokens = lock_tokens;
  cc->keep_locks = keep_locks;
  cc->cb = ras->cb;

  /* If the caller didn't give us any way of storing wcprops, then
     there's no point in getting back a MERGE response full of VR's. */
  if (ras->callbacks->push_wc_prop == NULL)
    cc->disable_merge_response = TRUE;

  /* ### should we perform an OPTIONS to validate the server we're about
     ### to talk to? */

  /*
  ** Create an Activity. This corresponds directly to an FS transaction.
  ** We will check out all further resources within the context of this
  ** activity.
  */
  SVN_ERR(create_activity(cc, pool));

  /*
  ** Find the latest baseline resource, check it out, and then apply the
  ** log message onto the thing.
  */
  SVN_ERR(apply_log_message(cc, log_msg, pool));

  /*
  ** Set up the editor.
  **
  ** This structure is used during the commit process. An external caller
  ** uses these callbacks to describe all the changes in the working copy
  ** that must be committed to the server.
  */
  commit_editor = svn_delta_default_editor(pool);
  commit_editor->open_root = commit_open_root;
  commit_editor->delete_entry = commit_delete_entry;
  commit_editor->add_directory = commit_add_dir;
  commit_editor->open_directory = commit_open_dir;
  commit_editor->change_dir_prop = commit_change_dir_prop;
  commit_editor->close_directory = commit_close_dir;
  commit_editor->add_file = commit_add_file;
  commit_editor->open_file = commit_open_file;
  commit_editor->apply_textdelta = commit_apply_txdelta;
  commit_editor->change_file_prop = commit_change_file_prop;
  commit_editor->close_file = commit_close_file;
  commit_editor->close_edit = commit_close_edit;
  commit_editor->abort_edit = commit_abort_edit;

  *editor = commit_editor;
  *edit_baton = cc;
  return SVN_NO_ERROR;
}
