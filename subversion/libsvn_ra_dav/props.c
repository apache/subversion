/*
 * props.c :  routines for fetching DAV properties
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <ne_basic.h>
#include <ne_props.h>
#include <ne_xml.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"

#include "ra_dav.h"


/* some definitions of various properties that may be fetched */
const ne_propname svn_ra_dav__vcc_prop = {
  "DAV:", "version-controlled-configuration"
};
const ne_propname svn_ra_dav__checked_in_prop = {
  "DAV:", "checked-in"
};


typedef struct {
  ne_xml_elmid id;
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

static const struct ne_xml_elm neon_descriptions[] =
{
  /* DAV elements */
  { "DAV:", "baseline-collection", ELEM_baseline_coll, NE_XML_CDATA },
  { "DAV:", "checked-in", ELEM_checked_in, 0 },
  { "DAV:", "collection", ELEM_collection, NE_XML_CDATA },
  { "DAV:", "href", NE_ELM_href, NE_XML_CDATA },
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "version-controlled-configuration", ELEM_vcc, 0 },
  { "DAV:", "version-name", ELEM_version_name, NE_XML_CDATA },

  /* SVN elements */
  { SVN_PROP_PREFIX, "baseline-relative-path", ELEM_baseline_relpath,
    NE_XML_CDATA },

  { NULL }
};

typedef struct {
  /* PROPS: URL-PATH -> RESOURCE (const char * -> svn_ra_dav_resource_t *) */
  apr_hash_t *props;

  apr_pool_t *pool;

  ne_propfind_handler *dph;

} prop_ctx_t;



/* look up an element definition. may return NULL if the elem is not
   recognized. */
static const elem_defn *defn_from_id(ne_xml_elmid id)
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
  char *url_path;
  svn_ra_dav_resource_t *r = apr_pcalloc(pc->pool, sizeof(*r));
  apr_size_t len;

  r->pool = pc->pool;

  /* parse the PATH element out of the URL

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) uri_parse(url, &parsed_url, NULL);
  url_path = apr_pstrdup(pc->pool, parsed_url.path);
  uri_free(&parsed_url);

  /* clean up trailing slashes from the URL */
  len = strlen(url_path);
  if (len > 1 && url_path[len - 1] == '/')
    url_path[len - 1] = '\0';
  r->url = url_path;

  /* the properties for this resource */
  r->propset = apr_hash_make(pc->pool);

  /* store this resource into the top-level hash table */
  apr_hash_set(pc->props, url_path, APR_HASH_KEY_STRING, r);

  return r;
}

static int add_to_hash(void *userdata, const ne_propname *pname,
                       const char *value, const ne_status *status)
{
  svn_ra_dav_resource_t *r = userdata;
  const char *name;
  
  name = apr_pstrcat(r->pool, pname->nspace, pname->name, NULL);
  value = apr_pstrdup(r->pool, value);

  /* ### woah... what about a binary VALUE with a NULL character? */
  apr_hash_set(r->propset, name, APR_HASH_KEY_STRING, value);

  return 0;
}

static void process_results(void *userdata, const char *uri,
                            const ne_prop_result_set *rset)
{
  /*  prop_ctx_t *pc = userdata; */
  svn_ra_dav_resource_t *r = ne_propset_private(rset);

  /* ### should use ne_propset_status(rset) to determine whether the
   * ### PROPFIND failed for the properties we're interested in. */
  (void) ne_propset_iterate(rset, add_to_hash, r);
}

static int validate_element(ne_xml_elmid parent, ne_xml_elmid child)
{
  switch (parent)
    {
    case NE_ELM_prop:
        switch (child)
          {
          case ELEM_baseline_coll:
          case ELEM_baseline_relpath:
          case ELEM_checked_in:
          case ELEM_resourcetype:
          case ELEM_vcc:
          case ELEM_version_name:
            return NE_XML_VALID;

          default:
            /* some other, unrecognized property */
            return NE_XML_DECLINE;
          }
        
    case ELEM_baseline_coll:
    case ELEM_checked_in:
    case ELEM_vcc:
      if (child == NE_ELM_href)
        return NE_XML_VALID;
      else
        return NE_XML_DECLINE; /* not concerned with other types */
      
    case ELEM_resourcetype:
      if (child == ELEM_collection)
        return NE_XML_VALID;
      else
        return NE_XML_DECLINE; /* not concerned with other types (### now) */

    default:
      return NE_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int start_element(void *userdata, const struct ne_xml_elm *elm,
                         const char **atts)
{
  prop_ctx_t *pc = userdata;
  svn_ra_dav_resource_t *r = ne_propfind_current_private(pc->dph);

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

static int end_element(void *userdata, const struct ne_xml_elm *elm,
                       const char *cdata)
{
  prop_ctx_t *pc = userdata;
  svn_ra_dav_resource_t *r = ne_propfind_current_private(pc->dph);
  const char *name;

  if (elm->id == NE_ELM_href)
    {
      /* use the parent element's name, not the href */
      const elem_defn *parent_defn = defn_from_id(r->href_parent);

      name = parent_defn ? parent_defn->name : NULL;

      /* if name == NULL, then we don't know about this DAV:href. leave name
         NULL so that we don't store a property. */
    }
  else
    {
      const elem_defn *defn = defn_from_id(elm->id);

      /* if this element isn't a property, then skip it */
      if (defn == NULL || !defn->is_property)
        return 0;

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
                                    const ne_propname *which_props,
                                    apr_pool_t *pool)
{
  ne_xml_parser *hip;
  int rv;
  prop_ctx_t pc = { 0 };

  pc.pool = pool;
  pc.props = apr_hash_make(pc.pool);

  pc.dph = ne_propfind_create(ras->sess, url, depth);
  ne_propfind_set_private(pc.dph, create_private, &pc);
  hip = ne_propfind_get_parser(pc.dph);
  ne_xml_push_handler(hip, neon_descriptions,
                      validate_element, start_element, end_element, &pc);

  if (label != NULL)
    {
      /* get the request pointer and add a Label header */
      ne_request *req = ne_propfind_get_request(pc.dph);
      ne_add_request_header(req, "Label", label);
    }
  
  if (which_props) 
    {
      rv = ne_propfind_named(pc.dph, which_props, process_results, &pc);
    } 
  else
    { 
      rv = ne_propfind_allprop(pc.dph, process_results, &pc);
    }

  ne_propfind_destroy(pc.dph);

  if (rv != NE_OK)
    {
      switch (rv)
        {
        case NE_CONNECT:
          /* ### need an SVN_ERR here */
          return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                                   "Could not connect to server "
                                   "(%s, port %d).",
                                   ras->root.host, ras->root.port);
        case NE_AUTH:
          return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, 0, NULL, 
                                  pool,
                                  "Authentication failed on server.");
        default:
          /* ### need an SVN_ERR here */
          return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                                  ne_get_error(ras->sess));
        }
    }

  *results = pc.props;

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_props_resource(svn_ra_dav_resource_t **rsrc,
                                             svn_ra_session_t *ras,
                                             const char *url,
                                             const char *label,
                                             const ne_propname *which_props,
                                             apr_pool_t *pool)
{
  apr_hash_t *props;

  SVN_ERR( svn_ra_dav__get_props(&props, ras, url, NE_DEPTH_ZERO,
                                 label, which_props, pool) );
  *rsrc = apr_hash_get(props, url, APR_HASH_KEY_STRING);
  if (*rsrc == NULL)
    {
      /* ### should have been in there... */
    }

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_one_prop(const svn_string_t **propval,
                                       svn_ra_session_t *ras,
                                       const char *url,
                                       const char *label,
                                       const ne_propname *propname,
                                       apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  ne_propname props[2] = { { 0 } };
  const char *name;
  const char *value;
  svn_string_t *sv;

  props[0] = *propname;
  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, ras, url, label, props,
                                          pool) );

  name = apr_pstrcat(pool, propname->nspace, propname->name, NULL);
  value = apr_hash_get(rsrc->propset, name, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      /* ### need an SVN_ERR here */
      return svn_error_createf(APR_EGENERAL, 0, NULL, pool,
                               "%s was not present on the resource.", name);
    }

  /* ### hmm. we can't deal with embedded NULLs right now... */
  sv = apr_palloc(pool, sizeof(*sv));
  sv->data = value;
  sv->len = strlen(value);
  *propval = sv;

  return SVN_NO_ERROR;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
