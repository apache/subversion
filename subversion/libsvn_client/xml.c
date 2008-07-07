/*
 * xml.c:  XML output of Subversion data
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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

/* ==================================================================== */


/*** Includes. ***/
#include "svn_client.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_base64.h"

#include "svn_private_config.h"


/*** Code. ***/

void
svn_client__print_xml_prop(svn_stringbuf_t **outstr,
                           const char* propname,
                           svn_string_t *propval,
                           apr_pool_t *pool)
{
  const char *xml_safe;
  const char *encoding = NULL;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create("", pool);

  if (svn_xml_is_xml_safe(propval->data, propval->len))
    {
      svn_stringbuf_t *xml_esc = NULL;
      svn_xml_escape_cdata_string(&xml_esc, propval, pool);
      xml_safe = xml_esc->data;
    }
  else
    {
      const svn_string_t *base64ed = svn_base64_encode_string(propval, pool);
      encoding = "base64";
      xml_safe = base64ed->data;
    }

  if (encoding)
    svn_xml_make_open_tag(outstr, pool, svn_xml_protect_pcdata,
                          "property", "name", propname,
                          "encoding", encoding, NULL);
  else
    svn_xml_make_open_tag(outstr, pool, svn_xml_protect_pcdata,
                          "property", "name", propname, NULL);

  svn_stringbuf_appendcstr(*outstr, xml_safe);

  svn_xml_make_close_tag(outstr, pool, "property");
}
