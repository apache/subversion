/*
 * merge.c :  routines for performing a MERGE server requests
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_pools.h>
#include <apr_hash.h>

#include <hip_xml.h>
#include <http_request.h>

#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"

#include "ra_dav.h"


static const struct hip_xml_elm merge_elements[] =
{
  { "DAV:", "updated-set", ELEM_updated_set, 0 },
  { "DAV:", "merged-set", ELEM_merged_set, 0 },
  { "DAV:", "ignored-set", ELEM_ignored_set, 0 },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { "DAV:", "merge-response", ELEM_merge_response, 0 },
  { "DAV:", "checked-in", ELEM_checked_in, 0 },
  { "DAV:", "response", DAV_ELM_response, 0 },
  { "DAV:", "propstat", DAV_ELM_propstat, 0 },
  { "DAV:", "status", DAV_ELM_status, 0 },
  { "DAV:", "responsedescription", DAV_ELM_responsedescription, 0 },
  { "DAV:", "prop", DAV_ELM_prop, 0 },
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "collection", ELEM_collection, 0 },
  { "DAV:", "baseline", ELEM_baseline, 0 },
  { "DAV:", "version-name", ELEM_version_name, 0 },

  { NULL }
};

enum merge_rtype {
  RTYPE_UNKNOWN,	/* unknown (haven't seen it in the response yet) */
  RTYPE_REGULAR,        /* a regular (member) resource */
  RTYPE_COLLECTION,     /* a collection resource */
  RTYPE_BASELINE        /* a baseline resource */
};

typedef struct {
  apr_pool_t *pool;

  /* any error that may have occurred during the MERGE response handling */
  svn_error_t *err;

  /* the BASE_HREF contains the merge target. as resources are specified in
     the merge response, we make their URLs relative to this URL, thus giving
     us a path for use in the commit callbacks. */
  const char *base_href;
  apr_size_t base_len;

  svn_revnum_t rev;	/* the new/target revision number for this commit */

  svn_boolean_t response_has_error;
  int response_parent;  /* what element did DAV:response appear within? */

  int href_parent;      /* what element is the DAV:href appearing within? */
  svn_string_t *href;   /* current response */

  int status;           /* HTTP status for this DAV:propstat */
  enum merge_rtype rtype;       /* DAV:resourcetype of this resource */

  svn_string_t *vsn_name;	/* DAV:version-name for this resource */
  svn_string_t *vsn_url;	/* DAV:checked-in for this resource */

  /* ### damned set_prop needs an svn_string_t for a constant */
  svn_string_t *vsn_url_name;

  /* if resources arrive before we know the target revision, then we store
     their PATH -> VERSION-URL mappings in here. when the revision arrives,
     we empty this hash table, setting version URLs and bumping to the
     revision that arrived. */
  apr_hash_t *hold;

  svn_ra_set_wc_prop_func_t set_prop;
  svn_ra_close_commit_func_t close_commit;
  void *close_baton;

} merge_ctx_t;


static void add_ignored(merge_ctx_t *mc, const char *cdata)
{
  /* ### the server didn't check in the file(!) */
  /* ### remember the file and issue a report/warning later */
}

static svn_error_t *bump_resource(merge_ctx_t *mc, char *path, char *vsn_url)
{
  svn_string_t path_str = { 0 };
  svn_string_t vsn_url_str = { 0 };

  /* ### damned callbacks take svn_string_t even though they don't plan
     ### to change the values whatsoever... */
  /* set up two svn_string_t values around the path and vsn_url. */
  path_str.data = path;
  path_str.len = strlen(path);
  path_str.blocksize = path_str.len + 1;
  path_str.pool = mc->pool;

  vsn_url_str.data = vsn_url;
  vsn_url_str.len = strlen(vsn_url);
  vsn_url_str.blocksize = vsn_url_str.len + 1;
  vsn_url_str.pool = mc->pool;

  /* store the version URL */
  SVN_ERR( (*mc->set_prop)(mc->close_baton, &path_str,
                           mc->vsn_url_name, &vsn_url_str) );

  /* bump the revision and commit the file */
  return (*mc->close_commit)(mc->close_baton, &path_str, mc->rev);
}

static svn_error_t * handle_resource(merge_ctx_t *mc)
{
  char *relative;

  if (mc->response_has_error)
    {
      /* ### what to do? */
      /* ### return "no error", presuming whatever set response_has_error
         ### has already handled the problem. */
      return SVN_NO_ERROR;
    }
  if (mc->response_parent == ELEM_merged_set)
    {
      /* ### shouldn't have happened. we told the server "don't merge" */
      /* ### need something better than APR_EGENERAL */
      return svn_error_createf(APR_EGENERAL, 0, NULL, mc->pool,
                               "Protocol error: we told the server to not "
                               "auto-merge any resources, but it said that "
                               "\"%s\" was merged.", mc->href->data);
    }
  if (mc->response_parent != ELEM_updated_set)
    {
      /* ### unknown parent for this response(!) */
      /* ### need something better than APR_EGENERAL */
      return svn_error_createf(APR_EGENERAL, 0, NULL, mc->pool,
                               "Internal error: there is an unknown parent "
                               "(%d) for the DAV:response element within the "
                               "MERGE response", mc->response_parent);
    }
  if (mc->href->len == 0
      || mc->vsn_name->len == 0
      || mc->vsn_url->len == 0
      || mc->rtype == RTYPE_UNKNOWN)
    {
      /* one or more properties were missing in the DAV:response for the
         resource. */
      return svn_error_createf(APR_EGENERAL, 0, NULL, mc->pool,
                               "Protocol error: the MERGE response for the "
                               "\"%s\" resource did not return all of the "
                               "properties that we asked for (and need to "
                               "complete the commit.", mc->href->data);
    }

  if (mc->rtype == RTYPE_BASELINE)
    {
      svn_error_t *err = NULL;

      /* cool. the DAV:version-name tells us the new revision */
      mc->rev = atol(mc->vsn_name->data);

      /* that's all we need from the baseline. replay everything in "hold"
         to commit the resources. */
      if (mc->hold != NULL)
        {
          apr_hash_index_t *hi = apr_hash_first(mc->hold);

          for (; hi != NULL; hi = apr_hash_next(hi))
            {
              const void *key;
              void *val;
              svn_error_t *one_err;

              apr_hash_this(hi, &key, NULL, &val);
              one_err = bump_resource(mc,
                                      (char *)key /* path */,
                                      val /* vsn_url */);
              if (one_err != NULL && err == NULL)
                err = one_err;
            }
        }

      return err;
    }

  /* a collection or regular resource */

  if (mc->href->len < mc->base_len)
    {
      /* ### need something better than APR_EGENERAL */
      return svn_error_createf(APR_EGENERAL, 0, NULL, mc->pool,
                               "A MERGE response for \"%s\" is not a child "
                               "of the destination (\"%s\")",
                               mc->href->data, mc->base_href);
    }

  /* given HREF of the form: BASE "/" RELATIVE, extract the relative portion */
  relative = mc->href->data + mc->base_len + 1;

  if (mc->rev == SVN_INVALID_REVNUM)
    {
      /* we don't the target revision yet, so store this for later */
      if (mc->hold == NULL)
        mc->hold = apr_hash_make(mc->pool);

      /* copy the key (the relative path) and value since they are in shared
         storage (i.e. the next resource will overwrite them) */
      apr_hash_set(mc->hold,
                   apr_pstrdup(mc->pool, relative), APR_HASH_KEY_STRING,
                   apr_pstrdup(mc->pool, mc->vsn_url->data));

      return SVN_NO_ERROR;
    }

  /* we've got everything needed, so bump the resource */
  return bump_resource(mc, relative, mc->vsn_url->data);
}

static int validate_element(hip_xml_elmid parent, hip_xml_elmid child)
{
  if ((child == ELEM_collection || child == ELEM_baseline)
      && parent != ELEM_resourcetype) {
    /* ### technically, they could occur elsewhere, but screw it */
    return HIP_XML_INVALID;
  }

  switch (parent)
    {
    case HIP_ELM_root:
      if (child == ELEM_merge_response)
        return HIP_XML_DECLINE; /* valid, but we don't need to see it */
      else
        return HIP_XML_INVALID;

    case ELEM_merge_response:
      if (child == ELEM_updated_set
          || child == ELEM_merged_set
          || child == ELEM_ignored_set)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* any child is allowed */

    case ELEM_updated_set:
    case ELEM_merged_set:
      if (child == DAV_ELM_response)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case ELEM_ignored_set:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case DAV_ELM_response:
      /* ### check these child elements against the RFC */
      if (child == DAV_ELM_href
          || child == DAV_ELM_status
          || child == DAV_ELM_propstat)
        return HIP_XML_VALID;
      else if (child == DAV_ELM_responsedescription)
        /* ### I think we want this... to save a message for the user */
        return HIP_XML_DECLINE; /* valid, but we don't need to see it */
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case DAV_ELM_propstat:
      if (child == DAV_ELM_prop || child == DAV_ELM_status)
        return HIP_XML_VALID;
      else if (child == DAV_ELM_responsedescription)
        /* ### I think we want this... to save a message for the user */
        return HIP_XML_DECLINE; /* valid, but we don't need to see it */
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case DAV_ELM_prop:
      if (child == ELEM_checked_in
          || child == ELEM_resourcetype
          || child == ELEM_version_name
          /* other props */)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore other props */

    case ELEM_checked_in:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    case ELEM_resourcetype:
      if (child == ELEM_collection || child == ELEM_baseline)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* ignore if something else was in there */

    default:
      return HIP_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int start_element(void *userdata, const struct hip_xml_elm *elm,
                         const char **atts)
{
  merge_ctx_t *mc = userdata;

  switch (elm->id)
    {
    case DAV_ELM_response:
      mc->response_has_error = FALSE;

      /* for each response (which corresponds to one resource), note that we
         haven't seen its resource type yet */
      mc->rtype = RTYPE_UNKNOWN;

      /* and we haven't seen these elements yet */
      mc->href->len = 0;
      mc->vsn_name->len = 0;
      mc->vsn_url->len = 0;

      /* FALLTHROUGH */

    case ELEM_ignored_set:
    case ELEM_checked_in:
      /* if we see an href "soon", then its parent is ELM */
      mc->href_parent = elm->id;
      break;

    case ELEM_updated_set:
    case ELEM_merged_set:
      mc->response_parent = elm->id;
      break;

    case DAV_ELM_propstat:
      /* initialize the status so we can figure out if we ever saw a
         status element in the propstat */
      mc->status = 0;
      break;

    case ELEM_resourcetype:
      /* we've seen a DAV:resourcetype, so it will be "regular" unless we
         see something within this element */
      mc->rtype = RTYPE_REGULAR;
      break;

    case ELEM_collection:
      mc->rtype = RTYPE_COLLECTION;
      break;

    case ELEM_baseline:
      mc->rtype = RTYPE_BASELINE;
      break;

    default:
      /* one of: DAV_ELM_href, DAV_ELM_status, DAV_ELM_prop,
         ELEM_version_name */
      break;
    }

  return 0;
}

static void copy_href(svn_string_t *dst, const char *src)
{
  struct uri parsed_url;

  /* parse the PATH element out of the URL and store it.

     ### do we want to verify the rest matches the current session?

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) uri_parse(src, &parsed_url, NULL);
  svn_string_set(dst, parsed_url.path);
  uri_free(&parsed_url);
}

static int end_element(void *userdata, const struct hip_xml_elm *elm,
                       const char *cdata)
{
  merge_ctx_t *mc = userdata;

  switch (elm->id)
    {
    case DAV_ELM_href:
      switch (mc->href_parent)
        {
        case ELEM_ignored_set:
          add_ignored(mc, cdata);
          break;

        case DAV_ELM_response:
          /* we're now working on this href... */
          copy_href(mc->href, cdata);
          break;

        case ELEM_checked_in:
          copy_href(mc->vsn_url, cdata);
          break;
        }
      break;

    case DAV_ELM_responsedescription:
      /* ### I don't think we'll see this right now, due to validate_element */
      /* ### remember this for error messages? */
      break;

    case DAV_ELM_status:
      {
        http_status hs;

        if (http_parse_statusline(cdata, &hs) != 0)
          mc->response_has_error = TRUE;
        else
          {
            mc->status = hs.code;
            if (hs.code != 200)
              {
                /* ### create an error structure? */
                mc->response_has_error = TRUE;
              }
          }
      }
      break;

    case DAV_ELM_propstat:
      /* ### does Neon have a symbol for 200? */
      if (mc->status == 200 /* OK */)
        {
          /* ### what to do? reset all the data? */
        }
      /* ### else issue an error? status==0 means we never saw one */
      break;

    case DAV_ELM_response:
      {
        svn_error_t *err;

        /* the end of a DAV:response means that we've seen all the information
           related to this resource. process it. */
        err = handle_resource(mc);
        if (err != NULL)
          {
            /* ### how best to handle this error? for now, just remember the
               ### first one found */
            if (mc->err == NULL)
              mc->err = err;
          }
      }
      break;

    case ELEM_checked_in:
      /* When we leave a DAV:checked-in element, the parents are DAV:prop,
         DAV:propstat, then DAV:response. If we see a DAV:href "on the way
         out", then it is going to belong to the DAV:response. */
      mc->href_parent = DAV_ELM_response;
      break;

    case ELEM_version_name:
      svn_string_set(mc->vsn_name, cdata);
      break;

    default:
      /* one of: ELEM_updated_set, ELEM_merged_set, ELEM_ignored_set,
         DAV_ELM_prop, ELEM_resourcetype, ELEM_collection, ELEM_baseline */
      break;
    }

  return 0;
}

svn_error_t * svn_ra_dav__merge_activity(
    svn_ra_session_t *ras,
    const char *repos_url,
    const char *activity_url,
    svn_ra_set_wc_prop_func_t set_prop,
    svn_ra_close_commit_func_t close_commit,
    void *close_baton,
    apr_pool_t *pool)
{
  merge_ctx_t mc = { 0 };
  http_req *req;
  hip_xml_parser *parser;
  int rv;
  int code;
  const char *body;

  /* create/prep the request */
  req = http_request_create(ras->sess, "MERGE", repos_url);
  if (req == NULL)
    {
      return svn_error_createf(SVN_ERR_RA_CREATING_REQUEST, 0, NULL, pool,
                               "Could not create a MERGE request (%s)",
                               repos_url);
    }

  body = apr_psprintf(pool,
                      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                      "<D:merge xmlns:D=\"DAV:\">"
                      "<D:source><D:href>%s</D:href></D:source>"
                      "<D:no-auto-merge/><D:no-checkout/>"
                      "<D:prop>"
                      "<D:checked-in/><D:version-name/><D:resourcetype/>"
                      "</D:prop>"
                      "</D:merge>", activity_url);
  http_set_request_body_buffer(req, body);

  mc.pool = pool;
  mc.base_href = repos_url;
  mc.base_len = strlen(repos_url);
  mc.rev = SVN_INVALID_REVNUM;

  /* ### it would be nice to create these with N bytes of storage, and
     ### avoid copying anything into them. */
  mc.href = svn_string_ncreate("", 0, pool);
  mc.vsn_name = svn_string_ncreate("", 0, pool);
  mc.vsn_url = svn_string_ncreate("", 0, pool);

  /* ### damn it */
  mc.vsn_url_name = svn_string_create(SVN_RA_DAV__LP_VSN_URL, pool);

  /* create a parser to read the MERGE response body */
  parser = hip_xml_create();
  hip_xml_push_handler(parser, merge_elements,
                       validate_element, start_element, end_element, &mc);
  http_add_response_body_reader(req, http_accept_2xx, hip_xml_parse_v, parser);

  /* run the request and get the resulting status code. */
  rv = http_request_dispatch(req);
  code = http_get_status(req)->code;
  http_request_destroy(req);

  if (rv != HTTP_OK)
    {
      /* ### need to be more sophisticated with reporting the failure */

      switch (rv)
        {
        case HTTP_CONNECT:
          /* ### need an SVN_ERR here */
          return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                                   "Could not connect to server "
                                   "(%s, port %d).",
                                   ras->root.host, ras->root.port);

        case HTTP_AUTH:
          return svn_error_create(SVN_ERR_NOT_AUTHORIZED, 0, NULL, pool,
                                  "Authentication failed on server.");

        default:
          return svn_error_createf(SVN_ERR_RA_REQUEST_FAILED, 0, NULL, pool,
                                   "The MERGE request failed (#%d) (%s)",
                                   rv, repos_url);
        }
    }

  if (code != 200)
    {
      /* ### need an SVN_ERR here */
      return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                               "The MERGE status was %d, but expected 200.",
                               code);
    }

  /* return any error that may have occurred */
  return mc.err;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
