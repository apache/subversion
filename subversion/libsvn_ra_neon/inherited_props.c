/*
 * inherited_props.c : ra_neon implementation of svn_ra_get_inherited_props
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>
#include <apr_xml.h>

#include <ne_socket.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_props.h"
#include "svn_base64.h"
#include "private/svn_dav_protocol.h"
#include "../libsvn_ra/ra_loader.h"

#include "ra_neon.h"


/* Our parser states. */
static const svn_ra_neon__xml_elm_t iprops_report_elements[] =
  {
    { SVN_XML_NAMESPACE, SVN_DAV__INHERITED_PROPS_REPORT,
      ELEM_iprop_report, 0 },
    { SVN_XML_NAMESPACE, SVN_DAV__IPROP_ITEM,
      ELEM_iprop_item, 0 },
    { SVN_XML_NAMESPACE, SVN_DAV__IPROP_PATH,
      ELEM_iprop_path, SVN_RA_NEON__XML_CDATA },
    { SVN_XML_NAMESPACE, SVN_DAV__IPROP_PROPNAME,
      ELEM_iprop_propname, SVN_RA_NEON__XML_CDATA },
    { SVN_XML_NAMESPACE, SVN_DAV__IPROP_PROPVAL,
      ELEM_iprop_propval, SVN_RA_NEON__XML_CDATA },
    { NULL }
  };

/* Struct for accumulating inherited props. */

typedef struct inherited_props_baton
{
  /* The depth-first ordered array of svn_prop_inherited_item_t *
     structures we are building. */
  apr_array_header_t *iprops;

  /* Pool in which to allocate elements of IPROPS. */
  apr_pool_t *pool;

  /* The repository's root URL. */
  const char *repos_root_url;

  /* Current CDATA values*/
  svn_stringbuf_t *curr_path;
  svn_stringbuf_t *curr_propname;
  svn_stringbuf_t *curr_propval;
  const char *curr_prop_val_encoding;

  /* Current element in IPROPS. */
  svn_prop_inherited_item_t *curr_iprop;
} inherited_props_baton;


static svn_error_t *
start_element(int *elem,
              void *baton,
              int parent_state,
              const char *nspace,
              const char *elt_name,
              const char **atts)
{
  inherited_props_baton *iprops_baton = baton;

  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(iprops_report_elements, nspace,
                                   elt_name);
  if (! elm)
    {
      *elem = NE_XML_DECLINE;
      return SVN_NO_ERROR;
    }

  if (parent_state == ELEM_root)
    {
      /* If we're at the root of the tree, the element has to be the editor
         report itself. */
      if (elm->id != ELEM_iprop_report)
        return UNEXPECTED_ELEMENT(nspace, elt_name);
    }
  else if (elm->id == ELEM_iprop_item)
    {
      svn_stringbuf_setempty(iprops_baton->curr_path);
      svn_stringbuf_setempty(iprops_baton->curr_propname);
      svn_stringbuf_setempty(iprops_baton->curr_propval);
      iprops_baton->curr_prop_val_encoding = NULL;
      iprops_baton->curr_iprop = NULL;
    }
  else if (elm->id == ELEM_iprop_propval)
    {
      const char *prop_val_encoding = svn_xml_get_attr_value("encoding",
                                                             atts);
      iprops_baton->curr_prop_val_encoding = apr_pstrdup(iprops_baton->pool,
                                                         prop_val_encoding);
    }

  *elem = elm->id;
  return SVN_NO_ERROR;
}

static svn_error_t *
end_element(void *baton,
            int state,
            const char *nspace,
            const char *elt_name)
{
  inherited_props_baton *iprops_baton = baton;

  const svn_ra_neon__xml_elm_t *elm
    = svn_ra_neon__lookup_xml_elem(iprops_report_elements, nspace,
                                   elt_name);
  if (! elm)
    return UNEXPECTED_ELEMENT(nspace, elt_name);

  if (elm->id == ELEM_iprop_path)
    {
      iprops_baton->curr_iprop = apr_palloc(
        iprops_baton->pool, sizeof(svn_prop_inherited_item_t));

      iprops_baton->curr_iprop->path_or_url =
        svn_path_url_add_component2(iprops_baton->repos_root_url,
                                    iprops_baton->curr_path->data,
                                    iprops_baton->pool);
      iprops_baton->curr_iprop->prop_hash = apr_hash_make(iprops_baton->pool);
    }
  else if (elm->id == ELEM_iprop_propval)
    {
      const svn_string_t *prop_val;

      if (iprops_baton->curr_prop_val_encoding)
        {
          svn_string_t encoded_prop_val;

          if (strcmp(iprops_baton->curr_prop_val_encoding, "base64") != 0)
            return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

          encoded_prop_val.data = iprops_baton->curr_propval->data;
          encoded_prop_val.len = iprops_baton->curr_propval->len;
          prop_val = svn_base64_decode_string(&encoded_prop_val,
                                              iprops_baton->pool);
        }
      else
        {
          prop_val = svn_string_create_from_buf(iprops_baton->curr_propval,
                                                iprops_baton->pool);
        }

      apr_hash_set(iprops_baton->curr_iprop->prop_hash,
                   apr_pstrdup(iprops_baton->pool,
                               iprops_baton->curr_propname->data),
                   APR_HASH_KEY_STRING,
                   prop_val);

      /* Clear current propname and propval in the event there are
         multiple properties on the current path. */
      svn_stringbuf_setempty(iprops_baton->curr_propname);
      svn_stringbuf_setempty(iprops_baton->curr_propval);
    } 
  else if (elm->id == ELEM_iprop_item)
    {
      APR_ARRAY_PUSH(iprops_baton->iprops, svn_prop_inherited_item_t *) =
        iprops_baton->curr_iprop;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cdata_handler(void *baton, int state, const char *cdata, size_t len)
{
  inherited_props_baton *iprops_baton = baton;
  apr_size_t nlen = len;

  switch (state)
    {
    case ELEM_iprop_path:
      svn_stringbuf_appendbytes(iprops_baton->curr_path, cdata, nlen);
      break;

    case ELEM_iprop_propname:
      svn_stringbuf_appendbytes(iprops_baton->curr_propname, cdata, nlen);
      break;

    case ELEM_iprop_propval:
      svn_stringbuf_appendbytes(iprops_baton->curr_propval, cdata, nlen);
      break;

    default:
      break;
    }
  return SVN_NO_ERROR;
}

/* Request a mergeinfo-report from the URL attached to SESSION,
   and fill in the CATALOG with the results.  */
svn_error_t *
svn_ra_neon__get_inherited_props(svn_ra_session_t *session,
                                 apr_array_header_t **iprops,
                                 const char *path,
                                 svn_revnum_t revision,
                                 apr_pool_t *pool)
{
  svn_ra_neon__session_t *ras = session->priv;
  svn_stringbuf_t *request_body = svn_stringbuf_create_empty(pool);
  inherited_props_baton iprops_baton;
  const char *bc_url;
  const char *bc_relative;
  const char *final_bc_url;
  static const char iprops_report_head[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>" DEBUG_CR
    "<S:" SVN_DAV__INHERITED_PROPS_REPORT " xmlns:S=\""
    SVN_XML_NAMESPACE "\" xmlns:D=\"DAV:\">" DEBUG_CR;

  static const char iprops_report_tail[] =
    "</S:" SVN_DAV__INHERITED_PROPS_REPORT ">" DEBUG_CR;

  /* Construct the request body. */
  svn_stringbuf_appendcstr(request_body, iprops_report_head);
  svn_stringbuf_appendcstr(request_body,
                           apr_psprintf(pool,
                                        "<S:revision>%ld"
                                        "</S:revision>" DEBUG_CR, revision));
  svn_stringbuf_appendcstr(request_body, "<S:path>");
  svn_stringbuf_appendcstr(request_body, apr_xml_quote_string(pool, path, 0));
  svn_stringbuf_appendcstr(request_body, "</S:path>");

  svn_stringbuf_appendcstr(request_body, iprops_report_tail);
  iprops_baton.repos_root_url = ras->repos_root;
  iprops_baton.pool = pool;
  iprops_baton.curr_path = svn_stringbuf_create_empty(pool);
  iprops_baton.curr_propname = svn_stringbuf_create_empty(pool);
  iprops_baton.curr_propval = svn_stringbuf_create_empty(pool);
  iprops_baton.curr_iprop = NULL;
  iprops_baton.iprops = apr_array_make(pool, 1,
                                       sizeof(svn_prop_inherited_item_t *));

  /* ras's URL may not exist in HEAD, and thus it's not safe to send
     it as the main argument to the REPORT request; it might cause
     dav_get_resource() to choke on the server.  So instead, we pass a
     baseline-collection URL, which we get from END. */
  SVN_ERR(svn_ra_neon__get_baseline_info(&bc_url, &bc_relative, NULL, ras,
                                         ras->url->data, revision, pool));
  final_bc_url = svn_path_url_add_component2(bc_url, bc_relative, pool);

  SVN_ERR(svn_ra_neon__parsed_request(ras,
                                      "REPORT",
                                      final_bc_url,
                                      request_body->data,
                                      NULL, NULL,
                                      start_element,
                                      cdata_handler,
                                      end_element,
                                      &iprops_baton,
                                      NULL,
                                      NULL,
                                      FALSE,
                                      pool));
  *iprops = iprops_baton.iprops;
  return SVN_NO_ERROR;
}
