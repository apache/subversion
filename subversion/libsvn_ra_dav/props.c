/*
 * props.c :  routines for fetching DAV properties
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include "svn_path.h"
#include "svn_dav.h"
#include "svn_base64.h"
#include "svn_xml.h"
#include "svn_pools.h"

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
  { ELEM_get_content_length, SVN_RA_DAV__PROP_GETCONTENTLENGTH, 1 },

  /* SVN elements */
  { ELEM_baseline_relpath, SVN_RA_DAV__PROP_BASELINE_RELPATH, 1 },
  { ELEM_md5_checksum, SVN_RA_DAV__PROP_MD5_CHECKSUM, 1 },
  { ELEM_repository_uuid, SVN_RA_DAV__PROP_REPOSITORY_UUID, 1 },
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
  { "DAV:", "getcontentlength", ELEM_get_content_length, NE_XML_CDATA },

  /* SVN elements */
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path", ELEM_baseline_relpath,
    NE_XML_CDATA },
  { SVN_DAV_PROP_NS_DAV, "md5-checksum", ELEM_md5_checksum,
    NE_XML_CDATA },
  { SVN_DAV_PROP_NS_DAV, "repository-uuid", ELEM_repository_uuid,
    NE_XML_CDATA },
 
  /* Unknown things (we use this so that Neon let's us examine custom
     properties). */
#if 0
  /* ### if we do this, then Neon will not recognize and parse any of the
     ### properties. that means that process_results will not be called,
     ### which means that some properties won't be added to the propset
     ### hash. we could do all that in end_element, but since we're
     ### disabling Neon's property processing, it also means that we're
     ### losing Neon's propstatus checks.
     ###
     ### disabling for now...
  */
  { "", "", NE_ELM_unknown, NE_XML_COLLECT },
#endif

  { NULL }
};

typedef struct {
  apr_hash_t *props; /* const char *URL-PATH -> svn_ra_dav_resource_t
                        RESOURCE */
  const char *encoding; /* property encoding (or NULL) */
  apr_pool_t *pool;
  ne_propfind_handler *dph;
  ne_xml_parser *hip;
} prop_ctx_t;

/* when we begin a checkout, we fetch these from the "public" resources to
   steer us towards a Baseline Collection. we fetch the resourcetype to
   verify that we're accessing a collection. */
static const ne_propname starting_props[] =
{
  { "DAV:", "version-controlled-configuration" },
  { "DAV:", "resourcetype" },
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path" },
  { NULL }
};

/* when speaking to a Baseline to reach the Baseline Collection, fetch these
   properties. */
static const ne_propname baseline_props[] =
{
  { "DAV:", "baseline-collection" },
  { "DAV:", "version-name" },
  { NULL }
};



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
  ne_uri parsed_url;
  char *url_path;
  svn_ra_dav_resource_t *r = apr_pcalloc(pc->pool, sizeof(*r));
  apr_size_t len;

  r->pool = pc->pool;

  /* parse the PATH element out of the URL

     Note: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) ne_uri_parse(url, &parsed_url);
  url_path = apr_pstrdup(pc->pool, parsed_url.path);
  ne_uri_free(&parsed_url);

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
  const svn_string_t *valstr;
  const char *name;

  if (value == NULL)
    /* According to neon's docstrings, this means that there was an
       error fetching this property.  We don't care about the exact
       error status code, though. */
    return 0;
  
  name = apr_pstrcat(r->pool, pname->nspace, pname->name, NULL);
  valstr = svn_string_create(value, r->pool);

  /* ### woah... what about a binary VALUE with a NULL character? */
  apr_hash_set(r->propset, name, APR_HASH_KEY_STRING, valstr);

  return 0;
}

static void process_results(void *userdata, const char *uri,
                            const ne_prop_result_set *rset)
{
  /*  prop_ctx_t *pc = userdata; */
  svn_ra_dav_resource_t *r = ne_propset_private(rset);

  /* Only call iterate() on the 200-status properties. */
  (void) ne_propset_iterate(rset, add_to_hash, r);
}

static int validate_element(void *userdata, ne_xml_elmid parent, ne_xml_elmid child)
{
  switch (parent)
    {
    case NE_ELM_prop:
        switch (child)
          {
          case ELEM_baseline_coll:
          case ELEM_baseline_relpath:
          case ELEM_md5_checksum:
          case ELEM_repository_uuid:
          case ELEM_checked_in:
          case ELEM_resourcetype:
          case ELEM_vcc:
          case ELEM_version_name:
          case ELEM_get_content_length:
          case NE_ELM_unknown:
            return NE_XML_VALID;

          default:
            /* some other, unrecognized property */
            return NE_XML_VALID;
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

    case NE_ELM_unknown:
      /* these are our user-visible properties, presumably. */
      pc->encoding = ne_xml_get_attr(pc->hip, atts, SVN_DAV_PROP_NS_DAV,
                                     "encoding");
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
  const svn_string_t *value = NULL;
  const elem_defn *parent_defn;
  const elem_defn *defn;

  /* Reset the encoding. */

  switch (elm->id)
    {
    case NE_ELM_href:
      /* use the parent element's name, not the href */
      parent_defn = defn_from_id(r->href_parent);
      name = parent_defn ? apr_pstrdup(pc->pool, parent_defn->name) : NULL;
        
      /* if name == NULL, then we don't know about this DAV:href. */
      if (name == NULL)
        return 0;

      value = svn_string_create(cdata, pc->pool);
      break;

    case NE_ELM_unknown:
      /* if this is not a user-visible property, we don't care about it.  */
      if (! ((strcmp(elm->nspace, SVN_DAV_PROP_NS_CUSTOM) == 0)
             || (strcmp(elm->nspace, SVN_DAV_PROP_NS_SVN) == 0)))
        return 0;

      /* is there an encoding on this property?  handle it. */
      if (pc->encoding)
        {
          if (strcmp(pc->encoding, "base64") == 0)
            {
              svn_string_t in;
              in.data = cdata;
              in.len = strlen(cdata);
              value = svn_base64_decode_string(&in, pc->pool);
            }
          else /* unknown encoding type! */
            {
              return 1;
            }
        }
      else /* no encoding, so just transform the CDATA into an svn_string_t. */
        {
          value = svn_string_create(cdata, pc->pool);
        }

      /* slap the name back together so that other processors know
         what they are looking at. */
      name = apr_pstrcat(pc->pool, elm->nspace, elm->name, NULL);
      break;

    default:
      defn = defn_from_id(elm->id);

      /* if this element isn't a property, then skip it */
      if (defn == NULL || !defn->is_property)
        return 0;

      name = apr_pstrdup(pc->pool, defn->name);
      value = svn_string_create(cdata, pc->pool);
    }

  /* store VALUE in the property hash (keyed with NAME). */
  apr_hash_set(r->propset, name, APR_HASH_KEY_STRING, value);

  return 0;
}

svn_error_t * svn_ra_dav__get_props(apr_hash_t **results,
                                    ne_session *sess,
                                    const char *url,
                                    int depth,
                                    const char *label,
                                    const ne_propname *which_props,
                                    apr_pool_t *pool)
{
  int rv;
  prop_ctx_t pc = { 0 };
  ne_request *req;
  int status_code;

  pc.pool = pool;
  pc.props = apr_hash_make(pc.pool);

  pc.dph = ne_propfind_create(sess, url, depth);
  ne_propfind_set_private(pc.dph, create_private, &pc);
  pc.hip = ne_propfind_get_parser(pc.dph);
  ne_xml_push_handler(pc.hip, neon_descriptions,
                      validate_element, start_element, end_element, &pc);
  req = ne_propfind_get_request(pc.dph);

  if (label != NULL)
    {
      /* get the request pointer and add a Label header */
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

  status_code = ne_get_status(req)->code;

  ne_propfind_destroy(pc.dph);

  if (rv != NE_OK
      /* No error if any 2XX status code is returned. */
      || !(200 <= status_code && status_code <= 299))
    {
      const char *msg = apr_psprintf(pool, "PROPFIND of %s", url);
      return svn_ra_dav__convert_error(sess, msg, rv);
    }

  *results = pc.props;

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_props_resource(svn_ra_dav_resource_t **rsrc,
                                             ne_session *sess,
                                             const char *url,
                                             const char *label,
                                             const ne_propname *which_props,
                                             apr_pool_t *pool)
{
  apr_hash_t *props;
  char * url_path = apr_pstrdup(pool, url);
  int len = strlen(url);
  /* Clean up any trailing slashes. */
  if (len > 1 && url[len - 1] == '/')
      url_path[len - 1] = '\0';

  SVN_ERR( svn_ra_dav__get_props(&props, sess, url_path, NE_DEPTH_ZERO,
                                 label, which_props, pool) );

  /* ### HACK.  We need to have the client canonicalize paths, get rid
     of double slashes and such.  This check is just a check against
     non-SVN servers;  in the long run we want to re-enable this. */
  if (1 || label != NULL)
    {
      /* pick out the first response: the URL requested will not match
       * the response href. */
      apr_hash_index_t *hi = apr_hash_first(pool, props);

      if (hi)
        {
          void *ent;
          apr_hash_this(hi, NULL, NULL, &ent);
          *rsrc = ent;
        }
      else
        *rsrc = NULL;
    }
  else
    {
      *rsrc = apr_hash_get(props, url_path, APR_HASH_KEY_STRING);
    }

  if (*rsrc == NULL)
    {
      /* ### hmmm, should have been in there... */
      return svn_error_createf(APR_EGENERAL, NULL,
                               "failed to find label \"%s\" for url \"%s\"",
                               label ? label : "NULL", url_path);
    }

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_one_prop(const svn_string_t **propval,
                                       ne_session *sess,
                                       const char *url,
                                       const char *label,
                                       const ne_propname *propname,
                                       apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  ne_propname props[2] = { { 0 } };
  const char *name;
  const svn_string_t *value;

  props[0] = *propname;
  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, url, label, props,
                                          pool) );

  name = apr_pstrcat(pool, propname->nspace, propname->name, NULL);
  value = apr_hash_get(rsrc->propset, name, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      /* ### need an SVN_ERR here */
      return svn_error_createf(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                               "'%s' was not present on the resource.", name);
    }

  *propval = value;
  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_starting_props(svn_ra_dav_resource_t **rsrc,
                                             ne_session *sess,
                                             const char *url,
                                             const char *label,
                                             apr_pool_t *pool)
{
  return svn_ra_dav__get_props_resource(rsrc, sess, url, label, starting_props,
                                        pool);
}


svn_error_t *svn_ra_dav__get_baseline_props(svn_string_t *bc_relative,
                                            svn_ra_dav_resource_t **bln_rsrc,
                                            ne_session *sess,
                                            const char *url,
                                            svn_revnum_t revision,
                                            const ne_propname *which_props,
                                            apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  const svn_string_t *vcc;
  const svn_string_t *relative_path;
  ne_uri parsed_url;
  const char *my_bc_relative;
  const char *lopped_path = "";

  /* ### we may be able to replace some/all of this code with an
     ### expand-property REPORT when that is available on the server. */

  /* -------------------------------------------------------------------
     STEP 1

     Fetch the following properties from the given URL (or, if URL no
     longer exists in HEAD, get the properties from the nearest
     still-existing parent resource):

     *) DAV:version-controlled-configuration so that we can reach the
        baseline information.

     *) svn:baseline-relative-path so that we can find this resource
        within a Baseline Collection.  If we need to search up parent
        directories, then the relative path is this property value
        *plus* any trailing components we had to chop off.

     *) DAV:resourcetype so that we can identify whether this resource
        is a collection or not -- assuming we never had to search up
        parent directories.
  */

  /* Split the url into it's component pieces (schema, host, path,
     etc).  We want the path part. */
  ne_uri_parse (url, &parsed_url);

  /* ### do we want to optimize the props we fetch, based on what the
     ### user has requested? i.e. omit resourcetype when is_dir is NULL
     ### and omit relpath when bc_relative is NULL. */

  {
    /* Try to get the starting_props from the public url.  If the
       resource no longer exists in HEAD, we'll get a failure.  That's
       fine: just keep removing components and trying to get the
       starting_props from parent directories. */
    svn_error_t *err;
    apr_size_t len;
    svn_stringbuf_t *path_s = svn_stringbuf_create (parsed_url.path, pool);

    while (! svn_path_is_empty (path_s->data))
      {
        err = svn_ra_dav__get_starting_props(&rsrc, sess, path_s->data,
                                             NULL, pool);
        if (! err)
          break;   /* found an existing parent! */

        if (err->apr_err != SVN_ERR_RA_DAV_REQUEST_FAILED)
          return err;  /* found a _real_ error */

        /* else... lop off the basename and try again. */
        lopped_path = svn_path_join(svn_path_basename (path_s->data, pool),
                                    lopped_path,
                                    pool);
        len = path_s->len;
        svn_path_remove_component(path_s);
        if (path_s->len == len)          
            /* whoa, infinite loop, get out. */
          return svn_error_quick_wrap(err,
                                      "The path was not part of a repository");

        svn_error_clear (err);
      }

    if (svn_path_is_empty (path_s->data))
      {
        /* entire URL was bogus;  not a single part of it exists in
           the repository!  */
        err = svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                "No part of path '%s' was found in "
                                "repository HEAD.", parsed_url.path);
        ne_uri_free(&parsed_url);
        return err;
      }
    ne_uri_free(&parsed_url);
  }

  vcc = apr_hash_get(rsrc->propset, SVN_RA_DAV__PROP_VCC, APR_HASH_KEY_STRING);
  if (vcc == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "The VCC property was not found on the "
                              "resource.");
    }

  /* Allocate our own bc_relative path. */
  relative_path = apr_hash_get(rsrc->propset, 
                               SVN_RA_DAV__PROP_BASELINE_RELPATH,
                               APR_HASH_KEY_STRING);
  if (relative_path == NULL)
    {
      /* ### better error reporting... */        
      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "The relative-path property was not "
                              "found on the resource.");
    }
    
  /* don't forget to tack on the parts we lopped off in order
     to find the VCC... */
  my_bc_relative = svn_path_join(relative_path->data, lopped_path, pool);
 
  /* if they want the relative path (could be, they're just trying to find
     the baseline collection), then return it */
  if (bc_relative)
    {
      bc_relative->data = my_bc_relative;
      bc_relative->len = strlen(my_bc_relative);     
    }

  /* -------------------------------------------------------------------
     STEP 2

     We have the Version Controlled Configuration (VCC). From here, we
     need to reach the Baseline for specified revision.

     If the revision is SVN_INVALID_REVNUM, then we're talking about
     the HEAD revision. We have one extra step to reach the Baseline:

     *) Fetch the DAV:checked-in from the VCC; it points to the Baseline.

     If we have a specific revision, then we use a Label header when
     fetching props from the VCC. This will direct us to the Baseline
     with that label (in this case, the label == the revision number).

     From the Baseline, we fetch the following properties:

     *) DAV:baseline-collection, which is a complete tree of the Baseline
        (in SVN terms, this tree is rooted at a specific revision)

     *) DAV:version-name to get the revision of the Baseline that we are
        querying. When asking about the HEAD, this tells us its revision.
  */

  if (revision == SVN_INVALID_REVNUM)
    {
      /* Fetch the latest revision */

      const svn_string_t *baseline;

      /* Get the Baseline from the DAV:checked-in value, then fetch its
         DAV:baseline-collection property. */
      /* ### should wrap this with info about rsrc==VCC */
      SVN_ERR( svn_ra_dav__get_one_prop(&baseline, sess, vcc->data, NULL,
                                        &svn_ra_dav__checked_in_prop, pool) );

      /* ### do we want to optimize the props we fetch, based on what the
         ### user asked for? i.e. omit version-name if latest_rev is NULL */
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, 
                                              baseline->data, NULL,
                                              which_props, pool) );
    }
  else
    {
      /* Fetch a specific revision */

      char label[20];

      /* ### send Label hdr, get DAV:baseline-collection [from the baseline] */

      apr_snprintf(label, sizeof(label), "%" SVN_REVNUM_T_FMT, revision);

      /* ### do we want to optimize the props we fetch, based on what the
         ### user asked for? i.e. omit version-name if latest_rev is NULL */
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, vcc->data, label,
                                              which_props, pool) );
    }
  
  /* Return the baseline rsrc, which now contains whatever set of
     props the caller wanted. */
  *bln_rsrc = rsrc;
  return SVN_NO_ERROR;
}


svn_error_t *svn_ra_dav__get_baseline_info(svn_boolean_t *is_dir,
                                           svn_string_t *bc_url,
                                           svn_string_t *bc_relative,
                                           svn_revnum_t *latest_rev,
                                           ne_session *sess,
                                           const char *url,
                                           svn_revnum_t revision,
                                           apr_pool_t *pool)
{
  svn_ra_dav_resource_t *baseline_rsrc, *rsrc;
  const svn_string_t *my_bc_url;
  svn_string_t my_bc_rel;

  /* Go fetch a BASELINE_RSRC that contains specific properties we
     want.  This routine will also fill in BC_RELATIVE as best it
     can. */
  SVN_ERR (svn_ra_dav__get_baseline_props(&my_bc_rel,
                                          &baseline_rsrc,
                                          sess,
                                          url,
                                          revision,
                                          baseline_props, /* specific props */
                                          pool));

  /* baseline_rsrc now points at the Baseline. We will checkout from
     the DAV:baseline-collection.  The revision we are checking out is
     in DAV:version-name */
  
  /* Allocate our own copy of bc_url regardless. */
  my_bc_url = apr_hash_get(baseline_rsrc->propset,
                           SVN_RA_DAV__PROP_BASELINE_COLLECTION,
                           APR_HASH_KEY_STRING);
  if (my_bc_url == NULL)
    {
      /* ### better error reporting... */
      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "DAV:baseline-collection was not present "
                              "on the baseline resource.");
    }

  /* maybe return bc_url to the caller */
  if (bc_url)
    *bc_url = *my_bc_url;

  if (latest_rev != NULL)
    {
      const svn_string_t *vsn_name= apr_hash_get(baseline_rsrc->propset,
                                                 SVN_RA_DAV__PROP_VERSION_NAME,
                                                 APR_HASH_KEY_STRING);
      if (vsn_name == NULL)
        {
          /* ### better error reporting... */

          /* ### need an SVN_ERR here */
          return svn_error_create(APR_EGENERAL, NULL,
                                  "DAV:version-name was not present on the "
                                  "baseline resource.");
        }
      *latest_rev = SVN_STR_TO_REV(vsn_name->data);
    }

  if (is_dir != NULL)
    {
      /* query the DAV:resourcetype of the full, assembled URL. */
      const char *full_bc_url = svn_path_url_add_component(my_bc_url->data, 
                                                           my_bc_rel.data, 
                                                           pool);
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, full_bc_url,
                                              NULL, starting_props, pool) );
      *is_dir = rsrc->is_collection;
    }

  if (bc_relative)
    *bc_relative = my_bc_rel;

  return SVN_NO_ERROR;
}


/* Helper function for svn_ra_dav__do_proppatch() below. */
static void
do_setprop(ne_buffer *body, 
           const char *name, 
           const svn_string_t *value,
           apr_pool_t *pool)
{
  const char *encoding = "";
  const char *xml_safe;
  const char *xml_tag_name;

  /* Map property names to namespaces */
#define NSLEN (sizeof(SVN_PROP_PREFIX) - 1)
  if (strncmp(name, SVN_PROP_PREFIX, NSLEN) == 0)
    {
      xml_tag_name = apr_pstrcat(pool, "S:", name + NSLEN, NULL);
    }
#undef NSLEN
  else 
    {
      xml_tag_name = apr_pstrcat(pool, "C:", name, NULL);
    }

  /* If there is no value, just generate an empty tag and get outta
     here. */
  if (! value)
    {
      ne_buffer_concat(body, "<", xml_tag_name, "/>", NULL);
      return;
    }

  /* If a property is XML-safe, XML-encode it.  Else, base64-encode
     it. */
  if (svn_xml_is_xml_safe(value->data, value->len))
    {
      svn_stringbuf_t *xml_esc = NULL;
      svn_xml_escape_cdata_string(&xml_esc, value, pool);
      xml_safe = xml_esc->data;
    }
  else
    {
#ifdef SVN_DAV_FEATURE_BINARY_PROPS
      const svn_string_t *base64ed = svn_base64_encode_string(value, pool);
      encoding = " V:encoding=\"base64\"";
      xml_safe = base64ed->data;
#else /* SVN_DAV_FEATURE_BINARY_PROPS */
      xml_safe = value->data;
#endif /* SVN_DAV_FEATURE_BINARY_PROPS */
    }

  ne_buffer_concat(body, "<", xml_tag_name, encoding, ">", 
                   xml_safe, "</", xml_tag_name, ">", NULL);
  return;
}


svn_error_t *
svn_ra_dav__do_proppatch (svn_ra_session_t *ras,
                          const char *url,
                          apr_hash_t *prop_changes,
                          apr_array_header_t *prop_deletes,
                          apr_pool_t *pool)
{
  ne_request *req;
  int code;
  ne_buffer *body; /* ### using an ne_buffer because it can realloc */
  svn_error_t *err;

  /* just punt if there are no changes to make. */
  if ((prop_changes == NULL || (! apr_hash_count(prop_changes)))
      && (prop_deletes == NULL || prop_deletes->nelts == 0))
    return SVN_NO_ERROR;

  /* easier to roll our own PROPPATCH here than use ne_proppatch(), which 
   * doesn't really do anything clever. */
  body = ne_buffer_create();

  ne_buffer_zappend(body,
                    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>" DEBUG_CR
                    "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:V=\""
                    SVN_DAV_PROP_NS_DAV "\" xmlns:C=\""
                    SVN_DAV_PROP_NS_CUSTOM "\" xmlns:S=\""
                    SVN_DAV_PROP_NS_SVN "\">");

  /* Handle property changes. */
  if (prop_changes)
    {
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create(pool);
      ne_buffer_zappend(body, "<D:set><D:prop>");
      for (hi = apr_hash_first(pool, prop_changes); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          apr_hash_this(hi, &key, NULL, &val);
          do_setprop(body, key, val, subpool);
          svn_pool_clear(subpool);
        }
      ne_buffer_zappend(body, "</D:prop></D:set>");
      svn_pool_destroy(subpool);
    }
  
  /* Handle property deletions. */
  if (prop_deletes)
    {
      int n;
      ne_buffer_zappend(body, "<D:remove><D:prop>");
      for (n = 0; n < prop_deletes->nelts; n++) 
        {
          const char *name = APR_ARRAY_IDX(prop_deletes, n, const char *);
          do_setprop(body, name, NULL, pool);
        }
      ne_buffer_zappend(body, "</D:prop></D:remove>");
    }

  /* Finish up the body. */
  ne_buffer_zappend(body, "</D:propertyupdate>");
  req = ne_request_create(ras->sess, "PROPPATCH", url);
  ne_set_request_body_buffer(req, body->data, ne_buffer_size(body));
  ne_add_request_header(req, "Content-Type", "text/xml; charset=UTF-8");

  /* run the request and get the resulting status code (and svn_error_t) */
  err = svn_ra_dav__request_dispatch(&code, req, ras->sess, "PROPPATCH",
                                     url,
                                     207 /* Multistatus */,
                                     0 /* nothing else allowed */,
                                     pool);

  ne_buffer_destroy(body);
  return err;
}



svn_error_t *
svn_ra_dav__do_check_path(svn_node_kind_t *kind,
                          void *session_baton,
                          const char *path,
                          svn_revnum_t revision,
                          apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  const char *url = ras->url;
  svn_error_t *err;
  svn_boolean_t is_dir;

  /* ### For now, using svn_ra_dav__get_baseline_info() works because
     we only have three possibilities: dir, file, or none.  When we
     add symlinks, we will need to do something different.  Here's one
     way described by Greg Stein:

       That is a PROPFIND (Depth:0) for the DAV:resourcetype property.

       You can use the svn_ra_dav__get_one_prop() function to fetch
       it. If the PROPFIND fails with a 404, then you have
       svn_node_none. If the resulting property looks like:

           <D:resourcetype>
             <D:collection/>
           </D:resourcetype>

       Then it is a collection (directory; svn_node_dir). Otherwise,
       it is a regular resource (svn_node_file).

       The harder part is parsing the resourcetype property. "Proper"
       parsing means treating it as an XML property and looking for
       the DAV:collection element in there. To do that, however, means
       that get_one_prop() can't be used. I think there may be some
       Neon functions for parsing XML properties; we'd need to
       look. That would probably be the best approach. (an alternative
       is to use apr_xml_* parsing functions on the returned string;
       get back a DOM-like thing, and look for the element).
  */

  /* If we were given a relative path to append, append it. */
  if (path)
    url = svn_path_url_add_component(url, path, pool);

  err = svn_ra_dav__get_baseline_info(&is_dir, NULL, NULL, NULL,
                                      ras->sess, url, revision, pool);

  if (err == SVN_NO_ERROR)
    {
      if (is_dir)
        *kind = svn_node_dir;
      else
        *kind = svn_node_file;
    }
  else  /* some error, read the comment below */
    {
      /* ### This is way too general.  We should only convert the
       * error to `svn_node_none' if we're sure that's what the error
       * means; for example, the test used to be this
       *
       *   (err && (err->apr_err == SVN_ERR_RA_DAV_PROPS_NOT_FOUND))
       *
       * which seemed reasonable...
       *
       * However, right now svn_ra_dav__get_props() returns a generic
       * error when the entity doesn't exist.  It's APR_EGENERAL or
       * something like that, and ne_get_status(req)->code == 500, not
       * 404.  I don't know whether this is something that can be
       * improved just in that function, or if the server will need to
       * be more descriptive about the error.  Greg, thoughts?
       */

      svn_error_clear (err);
      *kind = svn_node_none;
      return SVN_NO_ERROR;
    }

  return err;
}
