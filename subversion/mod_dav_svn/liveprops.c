/*
 * liveprops.c: mod_dav_svn live property provider functions for Subversion
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
 */



#include <httpd.h>
#include <util_xml.h>
#include <apr_tables.h>
#include <mod_dav.h>

#include "dav_svn.h"


/*
** The namespace URIs that we use. This list and the enumeration must
** stay in sync.
*/
static const char * const dav_svn_namespace_uris[] =
{
    "DAV:",
    /* ### SVN-specific namespace... */

    NULL	/* sentinel */
};
enum {
    DAV_SVN_URI_DAV		/* the DAV: namespace URI */
    /* ### SVN-specific */
};

/*
** The properties that we define.
*/
enum {
  /* using DAV_SVN_URI_DAV */
  DAV_PROPID_SVN_creationdate = 1,
  DAV_PROPID_SVN_displayname,
  DAV_PROPID_SVN_getcontentlength,
  DAV_PROPID_SVN_getetag,
  DAV_PROPID_SVN_getlastmodified,
  DAV_PROPID_SVN_source

  /* ### SVN props... */
};
typedef struct {
    int ns;
    const char * name;

    int propid;
} dav_svn_liveprop_name;

static const dav_svn_liveprop_name dav_svn_props[] =
{
  { DAV_SVN_URI_DAV, "creationdate",     DAV_PROPID_SVN_creationdate },
  { DAV_SVN_URI_DAV, "getcontentlength", DAV_PROPID_SVN_getcontentlength },
  { DAV_SVN_URI_DAV, "getetag",          DAV_PROPID_SVN_getetag },
  { DAV_SVN_URI_DAV, "getlastmodified",  DAV_PROPID_SVN_getlastmodified },

  /* ### these aren't SVN specific */
  { DAV_SVN_URI_DAV, "displayname",      DAV_PROPID_SVN_displayname },
  { DAV_SVN_URI_DAV, "source",           DAV_PROPID_SVN_source },

  { 0 } /* sentinel */
};

static dav_prop_insert dav_svn_insert_prop(const dav_resource *resource,
                                           int propid, int insvalue,
                                           ap_text_header *phdr)
{
  const char *value;
  const char *s;
  dav_prop_insert which;
  apr_pool_t *p = resource->info->pool;
  const dav_svn_liveprop_name *scan;
  int ns;

  /*
  ** None of SVN provider properties are defined if the resource does not
  ** exist. Just bail for this case.
  **
  ** Note that DAV:displayname and DAV:source will be stored as dead
  ** properties; the NOTDEF return code indicates that mod_dav should
  ** look there for the value.
  **
  ** Even though we state that the SVN properties are not defined, the
  ** client cannot store dead values -- we deny that thru the is_writable
  ** hook function.
  */
  if (!resource->exists)
    return DAV_PROP_INSERT_NOTDEF;

  switch (propid)
    {
    case DAV_PROPID_SVN_creationdate:
      /* ### need a creation date */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    case DAV_PROPID_SVN_getcontentlength:
      /* our property, but not defined on collection resources */
      if (resource->collection)
        return DAV_PROP_INSERT_NOTDEF;

      /* ### call svn_fs_file_length() */
      value = "0";
      break;

    case DAV_PROPID_SVN_getetag:
      value = dav_svn_getetag(resource);
      break;

    case DAV_PROPID_SVN_getlastmodified:
      /* ### need a modified date */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    case DAV_PROPID_SVN_displayname:
    case DAV_PROPID_SVN_source:
    default:
      /*
      ** This property is not defined. However, it may be a dead
      ** property.
      */
      return DAV_PROP_INSERT_NOTDEF;
    }

  /* assert: value != NULL */

  for (scan = dav_svn_props; scan->name != NULL; ++scan)
    if (scan->propid == propid)
      break;

  /* assert: scan->name != NULL */

  /* map our namespace into a global NS index */
  ns = dav_get_liveprop_ns_index(dav_svn_namespace_uris[scan->ns]);

  if (insvalue) {
    s = apr_psprintf(p, "<lp%d:%s>%s</lp%d:%s>" DEBUG_CR,
                     ns, scan->name, value, ns, scan->name);
    which = DAV_PROP_INSERT_VALUE;
  }
  else {
    s = apr_psprintf(p, "<lp%d:%s/>" DEBUG_CR, ns, scan->name);
    which = DAV_PROP_INSERT_NAME;
  }
  ap_text_append(p, phdr, s);

  /* we inserted a name or value (this prop is done) */
  return which;
}

static dav_prop_rw dav_svn_is_writable(const dav_resource *resource,
                                       int propid)
{
  if (propid == DAV_PROPID_SVN_displayname
      || propid == DAV_PROPID_SVN_source)
    return DAV_PROP_RW_YES;

  return DAV_PROP_RW_NO;
}

static dav_error * dav_svn_patch_validate(const dav_resource *resource,
                                          const ap_xml_elem *elem,
                                          int operation, void **context,
                                          int *defer_to_dead)
{
  return NULL;
}

static dav_error * dav_svn_patch_exec(dav_resource *resource,
                                      const ap_xml_elem *elem,
                                      int operation, void *context,
                                      dav_liveprop_rollback **rollback_ctx)
{
  return NULL;
}

static void dav_svn_patch_commit(dav_resource *resource,
                                 int operation, void *context,
                                 dav_liveprop_rollback *rollback_ctx)
{
}

static dav_error * dav_svn_patch_rollback(dav_resource *resource,
                                          int operation, void *context,
                                          dav_liveprop_rollback *rollback_ctx)
{
  return NULL;
}

const dav_hooks_liveprop dav_svn_hooks_liveprop = {
  dav_svn_insert_prop,
  dav_svn_is_writable,
  dav_svn_namespace_uris,
  dav_svn_patch_validate,
  dav_svn_patch_exec,
  dav_svn_patch_commit,
  dav_svn_patch_rollback,
};

void dav_svn_gather_propsets(apr_array_header_t *uris)
{
  /* ### what should we use for a URL to describe the available prop set? */
  /* ### for now... nothing. we will *only* have DAV properties */
#if 0
    *(const char **)apr_push_array(uris) =
        "<http://subversion.tigris.org/dav/propset/svn/1>";
#endif
}

int dav_svn_find_liveprop(request_rec *r, const char *ns_uri, const char *name,
                          const dav_hooks_liveprop **hooks)
{
  const dav_svn_liveprop_name *scan;

  if (*ns_uri != 'D' || strcmp(ns_uri, "DAV:") != 0)
    return 0;

  for (scan = dav_svn_props; scan->name != NULL; ++scan)
    if (DAV_SVN_URI_DAV == scan->ns && strcmp(name, scan->name) == 0)
      {
        *hooks = &dav_svn_hooks_liveprop;
        return scan->propid;
      }

  return 0;
}

void dav_svn_insert_all_liveprops(request_rec *r, const dav_resource *resource,
                                  int insvalue, ap_text_header *phdr)
{
    if (!resource->exists) {
	/* a lock-null resource */
	/*
	** ### technically, we should insert empty properties. dunno offhand
	** ### what part of the spec said this, but it was essentially thus:
	** ### "the properties should be defined, but may have no value".
	*/
	return;
    }

    (void) dav_svn_insert_prop(resource, DAV_PROPID_SVN_creationdate,
                               insvalue, phdr);
    (void) dav_svn_insert_prop(resource, DAV_PROPID_SVN_getcontentlength,
                               insvalue, phdr);
    (void) dav_svn_insert_prop(resource, DAV_PROPID_SVN_getlastmodified,
                               insvalue, phdr);
    (void) dav_svn_insert_prop(resource, DAV_PROPID_SVN_getetag,
                               insvalue, phdr);

    /* ### we know the others aren't defined as liveprops */
}

void dav_svn_register_uris(apr_pool_t *p)
{
    const char * const * uris = dav_svn_namespace_uris;

    for ( ; *uris != NULL; ++uris) {
        dav_register_liveprop_namespace(p, *uris);
    }
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
