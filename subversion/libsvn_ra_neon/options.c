/*
 * options.c :  routines for performing OPTIONS server requests
 *
 * ====================================================================
 * Copyright (c) 2000-2006, 2009 CollabNet.  All rights reserved.
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



#include "svn_pools.h"
#include "svn_error.h"
#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"

#include "ra_neon.h"


static const svn_ra_neon__xml_elm_t options_elements[] =
{
  { "DAV:", "activity-collection-set", ELEM_activity_coll_set, 0 },
  { "DAV:", "href", ELEM_href, SVN_RA_NEON__XML_CDATA },
  { "DAV:", "options-response", ELEM_options_response, 0 },

  { NULL }
};

typedef struct {
  /*WARNING: WANT_CDATA should stay the first element in the baton:
    svn_ra_neon__xml_collect_cdata() assumes the baton starts with a stringbuf.
  */
  svn_stringbuf_t *want_cdata;
  svn_stringbuf_t *cdata;
  apr_pool_t *pool;
  svn_string_t *activity_coll;
} options_ctx_t;

static int
validate_element(svn_ra_neon__xml_elmid parent, svn_ra_neon__xml_elmid child)
{
  switch (parent)
    {
    case ELEM_root:
      if (child == ELEM_options_response)
        return child;
      else
        return SVN_RA_NEON__XML_INVALID;

    case ELEM_options_response:
      if (child == ELEM_activity_coll_set)
        return child;
      else
        return SVN_RA_NEON__XML_DECLINE; /* not concerned with other response */

    case ELEM_activity_coll_set:
      if (child == ELEM_href)
        return child;
      else
        return SVN_RA_NEON__XML_DECLINE; /* not concerned with unknown crud */

    default:
      return SVN_RA_NEON__XML_DECLINE;
    }

  /* NOTREACHED */
}

static svn_error_t *
start_element(int *elem, void *baton, int parent,
              const char *nspace, const char *name, const char **atts)
{
  options_ctx_t *oc = baton;
  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(options_elements, nspace, name);

  *elem = elm ? validate_element(parent, elm->id) : SVN_RA_NEON__XML_DECLINE;
  if (*elem < 1) /* Not a valid element */
    return SVN_NO_ERROR;

  if (elm->id == ELEM_href)
    oc->want_cdata = oc->cdata;
  else
    oc->want_cdata = NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(void *baton, int state,
            const char *nspace, const char *name)
{
  options_ctx_t *oc = baton;

  if (state == ELEM_href)
    oc->activity_coll = svn_string_create_from_buf(oc->cdata, oc->pool);

  return SVN_NO_ERROR;
}


/** Capabilities exchange. */

/* Both server and repository support the capability. */
static const char *capability_yes = "yes";
/* Either server or repository does not support the capability. */
static const char *capability_no = "no";
/* Server supports the capability, but don't yet know if repository does. */
static const char *capability_server_yes = "server-yes";


/* Store in RAS the capabilities discovered from REQ's headers.
   Use POOL for temporary allocation only. */
static void
parse_capabilities(ne_request *req,
                   svn_ra_neon__session_t *ras,
                   apr_pool_t *pool)
{
  const char *header_value;

  /* Start out assuming all capabilities are unsupported. */
  apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_DEPTH,
               APR_HASH_KEY_STRING, capability_no);
  apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_MERGEINFO,
               APR_HASH_KEY_STRING, capability_no);
  apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_LOG_REVPROPS,
               APR_HASH_KEY_STRING, capability_no);

  /* Then find out which ones are supported. */
  header_value = ne_get_response_header(req, "dav");
  if (header_value)
    {
      /* Multiple headers of the same name will have been merged
         together by the time we see them (either by an intermediary,
         as is permitted in HTTP, or by neon) -- merged in the sense
         that if a header "foo" appears multiple times, all the values
         will be concatenated together, with spaces at the splice
         points.  For example, if the server sent:

            DAV: 1,2
            DAV: version-control,checkout,working-resource
            DAV: merge,baseline,activity,version-controlled-collection
            DAV: http://subversion.tigris.org/xmlns/dav/svn/depth

          Here we might see:

          header_value == "1,2, version-control,checkout,working-resource, merge,baseline,activity,version-controlled-collection, http://subversion.tigris.org/xmlns/dav/svn/depth, <http://apache.org/dav/propset/fs/1>"

          (Deliberately not line-wrapping that, so you can see what
          we're about to parse.)
      */

      apr_array_header_t *vals =
        svn_cstring_split(header_value, ",", TRUE, pool);

      /* Right now we only have a few capabilities to detect, so
         just seek for them directly.  This could be written
         slightly more efficiently, but that wouldn't be worth it
         until we have many more capabilities. */

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_DEPTH, vals))
        apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_DEPTH,
                     APR_HASH_KEY_STRING, capability_yes);

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_MERGEINFO, vals))
        /* The server doesn't know what repository we're referring
           to, so it can't just say capability_yes. */
        apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_MERGEINFO,
                     APR_HASH_KEY_STRING, capability_server_yes);

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_LOG_REVPROPS, vals))
        apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_LOG_REVPROPS,
                     APR_HASH_KEY_STRING, capability_yes);

      if (svn_cstring_match_glob_list(SVN_DAV_NS_DAV_SVN_PARTIAL_REPLAY,
                                      vals))
        apr_hash_set(ras->capabilities, SVN_RA_CAPABILITY_PARTIAL_REPLAY,
                     APR_HASH_KEY_STRING, capability_yes);
    }
}


svn_error_t *
svn_ra_neon__exchange_capabilities(svn_ra_neon__session_t *ras,
                                   apr_pool_t *pool)
{
  svn_ra_neon__request_t* req;
  svn_error_t *err = SVN_NO_ERROR;
  ne_xml_parser *parser = NULL;
  options_ctx_t oc = { 0 };
  const char *msg;
  int status_code;

  oc.pool = pool;
  oc.cdata = svn_stringbuf_create("", pool);

  req = svn_ra_neon__request_create(ras, "OPTIONS", ras->url->data, pool);

  /* ### Use a symbolic name somewhere for this MIME type? */
  ne_add_request_header(req->ne_req, "Content-Type", "text/xml");

  /* Create a parser to read the normal response body */
  parser = svn_ra_neon__xml_parser_create(req, ne_accept_2xx, start_element,
                                          svn_ra_neon__xml_collect_cdata,
                                          end_element, &oc);

  /* Run the request and get the resulting status code. */
  if ((err = svn_ra_neon__request_dispatch(&status_code, req, NULL,
                                           "<?xml version=\"1.0\" "
                                           "encoding=\"utf-8\"?>"
                                           "<D:options xmlns:D=\"DAV:\">"
                                           "<D:activity-collection-set/>"
                                           "</D:options>",
                                           200, 0, pool)))
    goto cleanup;

  /* Was there an XML parse error somewhere? */
  msg = ne_xml_get_error(parser);
  if (msg && *msg)
    {
      err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                              _("The %s request returned invalid XML "
                                "in the response: %s (%s)"),
                              "OPTIONS", msg, ras->url->data);
      goto cleanup;
    }

  /* We asked for, and therefore expect, to have found an activity
     collection in the response.  */
  if (oc.activity_coll == NULL)
    {
      err = svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                             _("The OPTIONS response did not include the "
                               "requested activity-collection-set; this often "
                               "means that the URL is not WebDAV-enabled"));
      goto cleanup;
    }

  ras->act_coll = apr_pstrdup(ras->pool, oc.activity_coll->data);
  parse_capabilities(req->ne_req, ras, pool);

 cleanup:
  svn_ra_neon__request_destroy(req);

  return err;
}


svn_error_t *
svn_ra_neon__get_activity_collection(const svn_string_t **activity_coll,
                                     svn_ra_neon__session_t *ras,
                                     apr_pool_t *pool)
{
  if (! ras->act_coll)
    SVN_ERR(svn_ra_neon__exchange_capabilities(ras, pool));
  *activity_coll = svn_string_create(ras->act_coll, pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_neon__has_capability(svn_ra_session_t *session,
                            svn_boolean_t *has,
                            const char *capability,
                            apr_pool_t *pool)
{
  svn_ra_neon__session_t *ras = session->priv;
  const char *cap_result;

  /* This capability doesn't rely on anything server side. */
  if (strcmp(capability, SVN_RA_CAPABILITY_COMMIT_REVPROPS) == 0)
    {
      *has = TRUE;
      return SVN_NO_ERROR;
    }

 cap_result = apr_hash_get(ras->capabilities,
                           capability,
                           APR_HASH_KEY_STRING);

  /* If any capability is unknown, they're all unknown, so ask. */
  if (cap_result == NULL)
    SVN_ERR(svn_ra_neon__exchange_capabilities(ras, pool));


  /* Try again, now that we've fetched the capabilities. */
  cap_result = apr_hash_get(ras->capabilities,
                            capability, APR_HASH_KEY_STRING);

  /* Some capabilities depend on the repository as well as the server.
     NOTE: ../libsvn_ra_serf/serf.c:svn_ra_serf__has_capability()
     has a very similar code block.  If you change something here,
     check there as well. */
  if (cap_result == capability_server_yes)
    {
      if (strcmp(capability, SVN_RA_CAPABILITY_MERGEINFO) == 0)
        {
          /* Handle mergeinfo specially.  Mergeinfo depends on the
             repository as well as the server, but the server routine
             that answered our svn_ra_neon__exchange_capabilities() call
             above didn't even know which repository we were interested in
             -- it just told us whether the server supports mergeinfo.
             If the answer was 'no', there's no point checking the
             particular repository; but if it was 'yes, we still must
             change it to 'no' iff the repository itself doesn't
             support mergeinfo. */
          svn_mergeinfo_catalog_t ignored;
          svn_error_t *err;
          apr_array_header_t *paths = apr_array_make(pool, 1,
                                                     sizeof(char *));
          APR_ARRAY_PUSH(paths, const char *) = "";

          err = svn_ra_neon__get_mergeinfo(session, &ignored, paths, 0,
                                           FALSE, FALSE, pool);

          if (err)
            {
              if (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)
                {
                  svn_error_clear(err);
                  cap_result = capability_no;
                }
              else if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
                {
                  /* Mergeinfo requests use relative paths, and
                     anyway we're in r0, so this is a likely error,
                     but it means the repository supports mergeinfo! */
                  svn_error_clear(err);
                  cap_result = capability_yes;
                }
              else
                return err;

            }
          else
            cap_result = capability_yes;

          apr_hash_set(ras->capabilities,
                       SVN_RA_CAPABILITY_MERGEINFO, APR_HASH_KEY_STRING,
                       cap_result);
        }
      else
        {
          return svn_error_createf
            (SVN_ERR_UNKNOWN_CAPABILITY, NULL,
             _("Don't know how to handle '%s' for capability '%s'"),
             capability_server_yes, capability);
        }
    }

  if (cap_result == capability_yes)
    {
      *has = TRUE;
    }
  else if (cap_result == capability_no)
    {
      *has = FALSE;
    }
  else if (cap_result == NULL)
    {
      return svn_error_createf
        (SVN_ERR_UNKNOWN_CAPABILITY, NULL,
         _("Don't know anything about capability '%s'"), capability);
    }
  else  /* "can't happen" */
    {
      /* Well, let's hope it's a string. */
      return svn_error_createf
        (SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
         _("Attempt to fetch capability '%s' resulted in '%s'"),
         capability, cap_result);
    }

  return SVN_NO_ERROR;
}
