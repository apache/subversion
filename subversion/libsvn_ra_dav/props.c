/*
 * props.c :  routines for fetching DAV properties
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
#include <apr_tables.h>
#include <apr_strings.h>

#include <dav_basic.h>
#include <dav_props.h>
#include <hip_xml.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"

#include "ra_dav.h"


enum {
  /* DAV elements */
  ELEM_baseline_coll = DAV_ELM_207_UNUSED,
  ELEM_checked_in,
  ELEM_collection,
  ELEM_resourcetype,
  ELEM_vcc,
  ELEM_version_name,

  /* SVN elements */
  ELEM_baseline_relpath
};

typedef struct {
  hip_xml_elmid id;
  const char *name;
  int is_property;      /* is it a property, or part of some structure? */
} elem_defn;

static const elem_defn elem_definitions[] =
{
  /* DAV elements */
  { ELEM_baseline_coll, SVN_RA_DAV__PROP_BASELINE_COLLECTION, 0 },
  { ELEM_checked_in, SVN_RA_DAV__PROP_CHECKED_IN, 0 },
  { ELEM_vcc, SVN_RA_DAV__PROP_VCC, 0 },
  { ELEM_version_name, SVN_RA_DAV__PROP_VERSION_NAME, 1 },

  /* SVN elements */
  { ELEM_baseline_relpath, SVN_RA_DAV__PROP_BASELINE_RELPATH, 1 },

  { 0 }
};

static const struct hip_xml_elm neon_descriptions[] =
{
  /* DAV elements */
  { "DAV:", "baseline-collection", ELEM_baseline_coll, HIP_XML_CDATA },
  { "DAV:", "checked-in", ELEM_checked_in, 0 },
  { "DAV:", "collection", ELEM_collection, HIP_XML_CDATA },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "version-controlled-configuration", ELEM_vcc, 0 },
  { "DAV:", "version-name", ELEM_version_name, HIP_XML_CDATA },

  /* SVN elements */
  { "SVN:", "baseline-relative-path", ELEM_baseline_relpath, HIP_XML_CDATA },

  { NULL }
};

typedef struct {
  /* PROPS: URL-PATH -> RESOURCE (const char * -> svn_ra_dav_resource_t *) */
  apr_hash_t *props;

  apr_pool_t *pool;

  dav_propfind_handler *dph;

} prop_ctx_t;


#ifndef USING_NEON_REPLACEMENT_HACK

#include <ne_alloc.h>
/* this is defined (differently) in 0.12. theoretically, a compile error
   will result if somebody tries to use 0.12. better than crashing due to
   a mismatch in the dav_propfind_handler_s structure... */
int ne_realloc(int foo);

/* ### UGH!! we're peeking into Neon's private structure! */
struct dav_propfind_handler_s {
    http_session *sess;
    const char *uri;
    int depth;

    int has_props; /* whether we've already written some
		    * props to the body. */
    sbuffer body;
    
    dav_207_parser *parser207;
    hip_xml_parser *parser;
    struct hip_xml_elm *elms;

    /* Callback to create the private structure. */
    dav_props_create_complex private_creator;
    void *private_userdata;
    
    /* Current propset. */
    dav_prop_result_set *current;

    dav_props_result callback;
    void *userdata;
};

/* ### extended replacement for Neon's dav_props.c::propfind(). allows us
   ### to send headers with the PROPFIND request. punt if/when Neon allows
   ### similar functionality. */
static int propfind(dav_propfind_handler *handler,
		    dav_props_result results, void *userdata,
                    const char *label)
{
    int ret;
    http_req *req;

    /* Register the catch-all handler to ignore any cruft the
     * server returns. */
    dav_207_ignore_unknown(handler->parser207);
    
    req = http_request_create(handler->sess, "PROPFIND", handler->uri);

    handler->callback = results;
    handler->userdata = userdata;

    http_set_request_body_buffer(req, sbuffer_data(handler->body));

    http_add_request_header(req, "Content-Type", "text/xml"); /* TODO: UTF-8? */
    dav_add_depth_header(req, handler->depth);

    /* ### SVN BEGIN ### */
    if (label != NULL)
      http_add_request_header(req, "Label", label);
    /* ### SVN END ### */
    
    http_add_response_body_reader(req, dav_accept_207, hip_xml_parse_v, 
				  handler->parser);

    ret = http_request_dispatch(req);

    if (ret == HTTP_OK && http_get_status(req)->klass != 2) {
	ret = HTTP_ERROR;
    } else if (!hip_xml_valid(handler->parser)) {
	http_set_error(handler->sess, hip_xml_get_error(handler->parser));
	ret = HTTP_ERROR;
    }

    http_request_destroy(req);

    return ret;
}
static int my_dav_propfind_named(dav_propfind_handler *handler,
                                 dav_props_result results, void *userdata,
                                 const char *label)
{
    sbuffer_zappend(handler->body, "</prop></propfind>" EOL);
    return propfind(handler, results, userdata, label);
}

#endif /* USING_NEON_REPLACEMENT_HACK */


/* look up an element definition. may return NULL if the elem is not
   recognized. */
static const elem_defn *defn_from_id(hip_xml_elmid id)
{
  const elem_defn *defn;

  for (defn = elem_definitions; defn->name != NULL; ++defn)
    {
      if (id == defn->id)
        return defn;
    }

  return NULL;
}

static void *create_private(void *userdata, const char *url)
{
  prop_ctx_t *pc = userdata;
  struct uri parsed_url;
  svn_ra_dav_resource_t *r = apr_pcalloc(pc->pool, sizeof(*r));

  /* parse the PATH element out of the URL

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path.
  */
  (void) uri_parse(url, &parsed_url, NULL);
  r->url = apr_pstrdup(pc->pool, parsed_url.path);
  uri_free(&parsed_url);

  r->propset = apr_hash_make(pc->pool);

  /* store this resource into the top-level hash table */
  apr_hash_set(pc->props, r->url, APR_HASH_KEY_STRING, r);

  return r;
}

static void process_results(void *userdata, const char *uri,
                            const dav_prop_result_set *rset)
{
#if 0
  prop_ctx_t *pc = userdata;
  svn_ra_dav_resource_t *r = dav_propset_private(rset);
#endif

  /* ### should use dav_propset_status(rset) to determine whether the
   * ### PROPFIND failed for the properties we're interested in. */

  /* ### use dav_propset_iterate(rset) to copy unhandled properties into
     ### the resource's hash table of props.
     ### maybe we need a special namespace for user props? */
}

static int validate_element(hip_xml_elmid parent, hip_xml_elmid child)
{
  switch (parent)
    {
    case DAV_ELM_prop:
        switch (child)
          {
          case ELEM_baseline_coll:
          case ELEM_baseline_relpath:
          case ELEM_checked_in:
          case ELEM_resourcetype:
          case ELEM_vcc:
          case ELEM_version_name:
            return HIP_XML_VALID;

          default:
            /* some other, unrecognized property */
            return HIP_XML_DECLINE;
          }
        
    case ELEM_baseline_coll:
    case ELEM_checked_in:
    case ELEM_vcc:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with other types */
      
    case ELEM_resourcetype:
      if (child == ELEM_collection)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with other types (### now) */

    default:
      return HIP_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int start_element(void *userdata, const struct hip_xml_elm *elm,
                         const char **atts)
{
  prop_ctx_t *pc = userdata;
  svn_ra_dav_resource_t *r = dav_propfind_current_private(pc->dph);

  switch (elm->id)
    {
    case ELEM_collection:
      r->is_collection = 1;
      break;

    case ELEM_baseline_coll:
    case ELEM_checked_in:
    case ELEM_vcc:
      /* each of these contains a DAV:href element that we want to process */
      r->href_parent = elm->id;
      break;

    default:
      /* nothing to do for these */
      break;
    }

  return 0;
}

static int end_element(void *userdata, const struct hip_xml_elm *elm,
                       const char *cdata)
{
  prop_ctx_t *pc = userdata;
  svn_ra_dav_resource_t *r;
  const elem_defn *defn = defn_from_id(elm->id);
  const char *name;

  /* if this element isn't a property, then skip it */
  if (defn == NULL || !defn->is_property)
    return 0;

  r = dav_propfind_current_private(pc->dph);

  if (elm->id == DAV_ELM_href)
    {
      /* use the parent element's name, not the href */
      const elem_defn *parent_defn = defn_from_id(r->href_parent);

      name = parent_defn ? parent_defn->name : NULL;

      /* if name == NULL, then we don't know about this DAV:href. leave name
         NULL so that we don't store a property. */
    }
  else
    {
      name = defn->name;
    }

  if (name != NULL)
    apr_hash_set(r->propset, name, APR_HASH_KEY_STRING,
                 apr_pstrdup(pc->pool, cdata));

  return 0;
}

svn_error_t * svn_ra_dav__get_props(apr_hash_t **results,
                                    svn_ra_session_t *ras,
                                    const char *url,
                                    int depth,
                                    const char *label,
                                    const dav_propname *which_props,
                                    apr_pool_t *pool)
{
  hip_xml_parser *hip;
  int rv;
  prop_ctx_t pc = { 0 };

  pc.props = apr_hash_make(pool);

  pc.dph = dav_propfind_create(ras->sess, url, depth);
  dav_propfind_set_complex(pc.dph, which_props, create_private, &pc);
  hip = dav_propfind_get_parser(pc.dph);
  hip_xml_push_handler(hip, neon_descriptions,
                       validate_element, start_element, end_element, &pc);
  rv = my_dav_propfind_named(pc.dph, process_results, &pc, label);
  dav_propfind_destroy(pc.dph);

  if (rv != HTTP_OK)
    {
      switch (rv)
        {
        case HTTP_CONNECT:
          /* ### need an SVN_ERR here */
          return svn_error_createf(0, 0, NULL, pool,
                                   "Could not connect to server "
                                   "(%s, port %d).",
                                   ras->root.host, ras->root.port);
        case HTTP_AUTH:
          return svn_error_create(SVN_ERR_NOT_AUTHORIZED, 0, NULL, 
                                  pool,
                                  "Authentication failed on server.");
        default:
          /* ### need an SVN_ERR here */
          return svn_error_create(0, 0, NULL, pool,
                                  http_get_error(ras->sess));
        }
    }

  *results = pc.props;

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_props_resource(svn_ra_dav_resource_t **rsrc,
                                             svn_ra_session_t *ras,
                                             const char *url,
                                             const char *label,
                                             const dav_propname *which_props,
                                             apr_pool_t *pool)
{
  apr_hash_t *props;

  SVN_ERR( svn_ra_dav__get_props(&props, ras, url, DAV_DEPTH_ZERO,
                                 label, which_props, pool) );
  *rsrc = apr_hash_get(props, ras->root.path, APR_HASH_KEY_STRING);
  if (*rsrc == NULL)
    {
      /* ### should have been in there... */
    }

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
