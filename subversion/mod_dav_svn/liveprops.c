/*
 * liveprops.c: mod_dav_svn live property provider functions for Subversion
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

#define SVN_RO_DAV_PROP(name) { DAV_SVN_URI_DAV, #name, DAV_PROPID_##name, 0 }
#define SVN_RW_DAV_PROP(name) { DAV_SVN_URI_DAV, #name, DAV_PROPID_##name, 1 }

static const dav_liveprop_spec dav_svn_props[] =
{
  /* ### don't worry about these for a bit */
#if 0
  /* WebDAV properties */
  SVN_RO_DAV_PROP(creationdate),
  SVN_RO_DAV_PROP(getcontentlanguage),  /* ### make this r/w? */
  SVN_RO_DAV_PROP(getcontentlength),
  SVN_RO_DAV_PROP(getcontenttype),      /* ### make this r/w? */
#endif
  SVN_RO_DAV_PROP(getetag),
#if 0
  SVN_RO_DAV_PROP(getlastmodified),
#endif

  /* DeltaV properties */
  SVN_RO_DAV_PROP(target),

  { 0 } /* sentinel */
};

static const dav_liveprop_group dav_svn_liveprop_group =
{
    dav_svn_props,
    dav_svn_namespace_uris,
    &dav_svn_hooks_liveprop
};


static dav_prop_insert dav_svn_insert_prop(const dav_resource *resource,
                                           int propid, int insvalue,
                                           ap_text_header *phdr)
{
  const char *value;
  const char *s;
  dav_prop_insert which;
  apr_pool_t *p = resource->info->pool;
  const dav_liveprop_spec *info;
  int global_ns;

  /*
  ** None of SVN provider properties are defined if the resource does not
  ** exist. Just bail for this case.
  **
  ** Even though we state that the SVN properties are not defined, the
  ** client cannot store dead values -- we deny that thru the is_writable
  ** hook function.
  */
  if (!resource->exists)
    return DAV_PROP_INSERT_NOTDEF;

  switch (propid)
    {
    case DAV_PROPID_creationdate:
      /* ### need a creation date */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    case DAV_PROPID_getcontentlanguage:
      /* ### need something here */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    case DAV_PROPID_getcontentlength:
      /* our property, but not defined on collection resources */
      if (resource->collection)
        return DAV_PROP_INSERT_NOTDEF;

      /* ### call svn_fs_file_length() */
      value = "0";
      break;

    case DAV_PROPID_getcontenttype:
      /* ### need something here */
      /* ### maybe application/octet-stream and text/plain? */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    case DAV_PROPID_getetag:
      value = dav_svn_getetag(resource);
      break;

    case DAV_PROPID_getlastmodified:
      /* ### need a modified date */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    case DAV_PROPID_target:
      /* ### need the target... */
      return DAV_PROP_INSERT_NOTDEF;
      break;

    default:
      /* ### what the heck was this property? */
      return DAV_PROP_INSERT_NOTDEF;
    }

  /* assert: value != NULL */

  /* get the information and global NS index for the property */
  global_ns = dav_get_liveprop_info(propid, &dav_svn_liveprop_group, &info);

    /* assert: info != NULL && info->name != NULL */

  if (insvalue) {
    s = apr_psprintf(p, "<lp%d:%s>%s</lp%d:%s>" DEBUG_CR,
                     global_ns, info->name, value, global_ns, info->name);
    which = DAV_PROP_INSERT_VALUE;
  }
  else {
    s = apr_psprintf(p, "<lp%d:%s/>" DEBUG_CR, global_ns, info->name);
    which = DAV_PROP_INSERT_NAME;
  }
  ap_text_append(p, phdr, s);

  /* we inserted a name or value (this prop is done) */
  return which;
}

static int dav_svn_is_writable(const dav_resource *resource, int propid)
{
  const dav_liveprop_spec *info;

  (void) dav_get_liveprop_info(propid, &dav_svn_liveprop_group, &info);
  return info->is_writable;
}

static dav_error * dav_svn_patch_validate(const dav_resource *resource,
                                          const ap_xml_elem *elem,
                                          int operation, void **context,
                                          int *defer_to_dead)
{
  /* NOTE: this function will not be called unless/until we have
     modifiable (writable) live properties. */
  return NULL;
}

static dav_error * dav_svn_patch_exec(dav_resource *resource,
                                      const ap_xml_elem *elem,
                                      int operation, void *context,
                                      dav_liveprop_rollback **rollback_ctx)
{
  /* NOTE: this function will not be called unless/until we have
     modifiable (writable) live properties. */
  return NULL;
}

static void dav_svn_patch_commit(dav_resource *resource,
                                 int operation, void *context,
                                 dav_liveprop_rollback *rollback_ctx)
{
  /* NOTE: this function will not be called unless/until we have
     modifiable (writable) live properties. */
}

static dav_error * dav_svn_patch_rollback(dav_resource *resource,
                                          int operation, void *context,
                                          dav_liveprop_rollback *rollback_ctx)
{
  /* NOTE: this function will not be called unless/until we have
     modifiable (writable) live properties. */
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
  return dav_do_find_liveprop(ns_uri, name, &dav_svn_liveprop_group, hooks);
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

    (void) dav_svn_insert_prop(resource, DAV_PROPID_creationdate,
                               insvalue, phdr);
    (void) dav_svn_insert_prop(resource, DAV_PROPID_getcontentlength,
                               insvalue, phdr);
    (void) dav_svn_insert_prop(resource, DAV_PROPID_getlastmodified,
                               insvalue, phdr);
    (void) dav_svn_insert_prop(resource, DAV_PROPID_getetag,
                               insvalue, phdr);

    /* ### we know the others aren't defined as liveprops */
}

void dav_svn_register_uris(apr_pool_t *p)
{
    /* register the namespace URIs */
    dav_register_liveprop_group(p, &dav_svn_liveprop_group);
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
