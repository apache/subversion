/*
 * liveprops.c: mod_dav_svn live property provider functions for Subversion
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
    "SVN:",     /* ### need to get this approved from IANA */

    NULL	/* sentinel */
};
enum {
    DAV_SVN_NAMESPACE_URI_DAV,  /* the DAV: namespace URI */
    DAV_SVN_NAMESPACE_URI       /* the SVN: namespace URI */
};

#define SVN_RO_DAV_PROP(name) \
	{ DAV_SVN_NAMESPACE_URI_DAV, #name, DAV_PROPID_##name, 0 }
#define SVN_RW_DAV_PROP(name) \
	{ DAV_SVN_NAMESPACE_URI_DAV, #name, DAV_PROPID_##name, 1 }
#define SVN_RO_DAV_PROP2(sym,name) \
	{ DAV_SVN_NAMESPACE_URI_DAV, #name, DAV_PROPID_##sym, 0 }
#define SVN_RW_DAV_PROP2(sym,name) \
	{ DAV_SVN_NAMESPACE_URI_DAV, #name, DAV_PROPID_##sym, 1 }

#define SVN_RO_SVN_PROP(sym,name) \
	{ DAV_SVN_NAMESPACE_URI, #name, SVN_PROPID_##sym, 0 }
#define SVN_RW_SVN_PROP(sym,name) \
	{ DAV_SVN_NAMESPACE_URI, #name, SVN_PROPID_##sym, 1 }

enum {
  SVN_PROPID_baseline_relative_path = 1
};

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
  SVN_RO_DAV_PROP2(baseline_collection, baseline-collection),
  SVN_RO_DAV_PROP2(checked_in, checked-in),
  SVN_RO_DAV_PROP2(version_controlled_configuration,
                   version-controlled-configuration),
  SVN_RO_DAV_PROP2(version_name, version-name),

  /* SVN properties */
  SVN_RO_SVN_PROP(baseline_relative_path, baseline-relative-path),

  { 0 } /* sentinel */
};

static const dav_liveprop_group dav_svn_liveprop_group =
{
    dav_svn_props,
    dav_svn_namespace_uris,
    &dav_svn_hooks_liveprop
};


static dav_prop_insert dav_svn_insert_prop(const dav_resource *resource,
                                           int propid, dav_prop_insert what,
                                           ap_text_header *phdr)
{
  const char *value;
  const char *s;
  apr_pool_t *p = resource->pool;
  const dav_liveprop_spec *info;
  int global_ns;
  svn_error_t *serr;

  /*
  ** None of SVN provider properties are defined if the resource does not
  ** exist. Just bail for this case.
  **
  ** Even though we state that the SVN properties are not defined, the
  ** client cannot store dead values -- we deny that thru the is_writable
  ** hook function.
  */
  if (!resource->exists)
    return DAV_PROP_INSERT_NOTSUPP;

  /* ### we may want to respond to DAV_PROPID_resourcetype for PRIVATE
     ### resources. need to think on "proper" interaction with mod_dav */

  switch (propid)
    {
    case DAV_PROPID_creationdate:
      /* ### need a creation date */
      return DAV_PROP_INSERT_NOTSUPP;
      break;

    case DAV_PROPID_getcontentlanguage:
      /* ### need something here */
      return DAV_PROP_INSERT_NOTSUPP;
      break;

    case DAV_PROPID_getcontentlength:
      /* our property, but not defined on collection resources */
      if (resource->collection)
        return DAV_PROP_INSERT_NOTSUPP;

      /* ### call svn_fs_file_length() */
      value = "0";
      break;

    case DAV_PROPID_getcontenttype:
      /* ### need something here */
      /* ### maybe application/octet-stream and text/plain? */
      return DAV_PROP_INSERT_NOTSUPP;
      break;

    case DAV_PROPID_getetag:
      value = dav_svn_getetag(resource);
      break;

    case DAV_PROPID_getlastmodified:
      /* ### need a modified date */
      return DAV_PROP_INSERT_NOTSUPP;
      break;

    case DAV_PROPID_baseline_collection:
      /* only defined for Baselines */
      /* ### whoops. also defined for a VCC. deal with it later. */
      if (resource->type != DAV_RESOURCE_TYPE_VERSION || !resource->baselined)
        return DAV_PROP_INSERT_NOTSUPP;
      value = dav_svn_build_uri(resource->info->repos, DAV_SVN_BUILD_URI_BC,
                                resource->info->root.rev, NULL,
                                1 /* add_href */, p);
      break;

    case DAV_PROPID_checked_in:
      /* only defined for VCRs */
      /* ### VCRs within the BC should not have this property! */
      /* ### note that a VCC (a special VCR) is defined as _PRIVATE for now */
      if (resource->type == DAV_RESOURCE_TYPE_PRIVATE
          && resource->info->restype == DAV_SVN_RESTYPE_VCC)
        {
          svn_revnum_t revnum;

          serr = svn_fs_youngest_rev(&revnum, resource->info->repos->fs, p);
          if (serr != NULL)
            {
              /* ### what to do? */
              value = "###error###";
              break;
            }
          value = dav_svn_build_uri(resource->info->repos,
                                    DAV_SVN_BUILD_URI_BASELINE,
                                    revnum, NULL,
                                    1 /* add_href */, p);
                                    
        }
      else if (resource->type != DAV_RESOURCE_TYPE_REGULAR)
        {
          /* not defined for this resource type */
          return DAV_PROP_INSERT_NOTSUPP;
        }
      else
        {
          svn_fs_id_t *id;
          svn_stringbuf_t *stable_id;

          serr = svn_fs_node_id(&id, resource->info->root.root,
                                resource->info->repos_path, p);
          if (serr != NULL)
            {
              /* ### what to do? */
              value = "###error###";
              break;
            }

          stable_id = svn_fs_unparse_id(id, p);
          svn_string_appendcstr(stable_id, resource->info->repos_path);

          value = dav_svn_build_uri(resource->info->repos,
                                    DAV_SVN_BUILD_URI_VERSION,
                                    SVN_INVALID_REVNUM, stable_id->data,
                                    1 /* add_href */, p);
        }
      break;

    case DAV_PROPID_version_controlled_configuration:
      /* only defined for VCRs */
      /* ### VCRs within the BC should not have this property! */
      /* ### note that a VCC (a special VCR) is defined as _PRIVATE for now */
      if (resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return DAV_PROP_INSERT_NOTSUPP;
      value = dav_svn_build_uri(resource->info->repos, DAV_SVN_BUILD_URI_VCC,
                                SVN_IGNORED_REVNUM, NULL,
                                1 /* add_href */, p);
      break;

    case DAV_PROPID_version_name:
      /* only defined for Version Resources and Baselines */
      /* ### whoops. also defined for VCRs. deal with it later. */
      if (resource->type != DAV_RESOURCE_TYPE_VERSION)
        return DAV_PROP_INSERT_NOTSUPP;
      if (resource->baselined)
        {
          /* just the revision number for baselines */
          value = apr_psprintf(p, "%ld", resource->info->root.rev);
        }
      else if (resource->info->node_id != NULL)
        {
          svn_stringbuf_t *id = svn_fs_unparse_id(resource->info->node_id, p);

          /* use ":ID" */
          value = apr_psprintf(p, ":%s", id->data);
        }
      else
        {
          /* assert: repos_path != NULL */

          /* use "REV:PATH" */
          value = apr_psprintf(p, "%ld:%s",
                               resource->info->root.rev,
                               resource->info->repos_path);
        }
      break;

    case SVN_PROPID_baseline_relative_path:
      /* only defined for VCRs */
      /* ### VCRs within the BC should not have this property! */
      /* ### note that a VCC (a special VCR) is defined as _PRIVATE for now */
      if (resource->type != DAV_RESOURCE_TYPE_REGULAR)
        return DAV_PROP_INSERT_NOTSUPP;

      /* drop the leading slash, so it is relative */
      value = resource->info->repos_path + 1;
      break;

    default:
      /* ### what the heck was this property? */
      return DAV_PROP_INSERT_NOTDEF;
    }

  /* assert: value != NULL */

  /* get the information and global NS index for the property */
  global_ns = dav_get_liveprop_info(propid, &dav_svn_liveprop_group, &info);

  /* assert: info != NULL && info->name != NULL */

  if (what == DAV_PROP_INSERT_NAME
      || (what == DAV_PROP_INSERT_VALUE && *value == '\0')) {
    s = apr_psprintf(p, "<lp%d:%s/>" DEBUG_CR, global_ns, info->name);
  }
  else if (what == DAV_PROP_INSERT_VALUE) {
    s = apr_psprintf(p, "<lp%d:%s>%s</lp%d:%s>" DEBUG_CR,
                     global_ns, info->name, value, global_ns, info->name);
  }
  else {
    /* assert: what == DAV_PROP_INSERT_SUPPORTED */
    s = apr_psprintf(p,
                     "<D:supported-live-property D:name=\"%s\" "
                     "D:namespace=\"%s\"/>" DEBUG_CR,
                     info->name, dav_svn_namespace_uris[info->ns]);
  }
  ap_text_append(p, phdr, s);

  /* we inserted whatever was asked for */
  return what;
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

static dav_error * dav_svn_patch_exec(const dav_resource *resource,
                                      const ap_xml_elem *elem,
                                      int operation, void *context,
                                      dav_liveprop_rollback **rollback_ctx)
{
  /* NOTE: this function will not be called unless/until we have
     modifiable (writable) live properties. */
  return NULL;
}

static void dav_svn_patch_commit(const dav_resource *resource,
                                 int operation, void *context,
                                 dav_liveprop_rollback *rollback_ctx)
{
  /* NOTE: this function will not be called unless/until we have
     modifiable (writable) live properties. */
}

static dav_error * dav_svn_patch_rollback(const dav_resource *resource,
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
    *(const char **)apr_array_push(uris) =
        "<http://subversion.tigris.org/dav/propset/svn/1>";
#endif
}

int dav_svn_find_liveprop(const dav_resource *resource,
                          const char *ns_uri, const char *name,
                          const dav_hooks_liveprop **hooks)
{
  /* don't try to find any liveprops if this isn't "our" resource */
  if (resource->hooks != &dav_svn_hooks_repos)
    return 0;

  return dav_do_find_liveprop(ns_uri, name, &dav_svn_liveprop_group, hooks);
}

void dav_svn_insert_all_liveprops(request_rec *r, const dav_resource *resource,
                                  dav_prop_insert what, ap_text_header *phdr)
{
    const dav_liveprop_spec *spec;

    /* don't insert any liveprops if this isn't "our" resource */
    if (resource->hooks != &dav_svn_hooks_repos)
        return;

    if (!resource->exists) {
	/* a lock-null resource */
	/*
	** ### technically, we should insert empty properties. dunno offhand
	** ### what part of the spec said this, but it was essentially thus:
	** ### "the properties should be defined, but may have no value".
	*/
	return;
    }

    for (spec = dav_svn_props; spec->name != NULL; ++spec)
      {
        (void) dav_svn_insert_prop(resource, spec->propid, what, phdr);
      }

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
