/*
 * deadprops.c: mod_dav_svn dead property provider functions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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



#include <httpd.h>
#include <mod_dav.h>

#include <apr_hash.h>

#include "dav_svn.h"

#include "svn_xml.h"
#include "svn_pools.h"
#include "svn_dav.h"
#include "svn_base64.h"
#include "svn_props.h"


struct dav_db {
  const dav_resource *resource;
  apr_pool_t *p;

  /* the resource's properties that we are sequencing over */
  apr_hash_t *props;
  apr_hash_index_t *hi;

  /* used for constructing repos-local names for properties */
  svn_stringbuf_t *work;

  /* passed to svn_repos_ funcs that fetch revprops. */
  svn_repos_authz_func_t authz_read_func;
  void *authz_read_baton;
};

struct dav_deadprop_rollback {
  dav_prop_name name;
  svn_string_t value;
};


/* retrieve the "right" string to use as a repos path */
static const char *get_repos_path(struct dav_resource_private *info)
{
  return info->repos_path;
}


/* construct the repos-local name for the given DAV property name */
static void get_repos_propname(dav_db *db, const dav_prop_name *name,
                               const char **repos_propname)
{
  if (strcmp(name->ns, SVN_DAV_PROP_NS_SVN) == 0)
    {
      /* recombine the namespace ("svn:") and the name. */
      svn_stringbuf_set(db->work, SVN_PROP_PREFIX);
      svn_stringbuf_appendcstr(db->work, name->name);
      *repos_propname = db->work->data;
    }
  else if (strcmp(name->ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    {
      /* the name of a custom prop is just the name -- no ns URI */
      *repos_propname = name->name;
    }
  else
    {
      *repos_propname = NULL;
    }
}

static dav_error *get_value(dav_db *db, const dav_prop_name *name,
                            svn_string_t **pvalue)
{
  const char *propname;
  svn_error_t *serr;

  /* get the repos-local name */
  get_repos_propname(db, name, &propname);

  if (propname == NULL)
    {
      /* we know these are not present. */
      *pvalue = NULL;
      return NULL;
    }

  /* ### if db->props exists, then try in there first */

  /* Working Baseline, Baseline, or (Working) Version resource */
  if (db->resource->baselined)
    if (db->resource->type == DAV_RESOURCE_TYPE_WORKING)
      serr = svn_fs_txn_prop(pvalue, db->resource->info->root.txn,
                             propname, db->p);
    else
      serr = svn_repos_fs_revision_prop(pvalue,
                                        db->resource->info-> repos->repos,
                                        db->resource->info->root.rev, propname,
                                        db->authz_read_func,
                                        db->authz_read_baton, db->p);
  else
    serr = svn_fs_node_prop(pvalue, db->resource->info->root.root,
                            get_repos_path(db->resource->info),
                            propname, db->p);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not fetch a property",
                               db->resource->pool);

  return NULL;
}

static dav_error *save_value(dav_db *db, const dav_prop_name *name,
                             const svn_string_t *value)
{
  const char *propname;
  svn_error_t *serr;

  /* get the repos-local name */
  get_repos_propname(db, name, &propname);

  if (propname == NULL)
    {
      if (db->resource->info->repos->autoversioning)
        /* ignore the unknown namespace of the incoming prop. */
        propname = name->name;
      else
        return dav_new_error(db->p, HTTP_CONFLICT, 0,
                             "Properties may only be defined in the "
                             SVN_DAV_PROP_NS_SVN " and " SVN_DAV_PROP_NS_CUSTOM
                             " namespaces.");
    }

  /* Working Baseline or Working (Version) Resource */
  if (db->resource->baselined)
    if (db->resource->working)
      serr = svn_repos_fs_change_txn_prop(db->resource->info->root.txn,
                                          propname, value, db->resource->pool);
    else
      {
        /* ### VIOLATING deltaV: you can't proppatch a baseline, it's
           not a working resource!  But this is how we currently
           (hackily) allow the svn client to change unversioned rev
           props.  See issue #916. */
        serr = svn_repos_fs_change_rev_prop2
          (db->resource->info->repos->repos,
           db->resource->info->root.rev,
           db->resource->info->repos->username,
           propname, value,
           db->authz_read_func,
           db->authz_read_baton,
           db->resource->pool);

        /* Tell the logging subsystem about the revprop change. */
        apr_table_set(db->resource->info->r->subprocess_env, "SVN-ACTION",
                      apr_psprintf(db->resource->pool,
                                   "revprop-change r%" SVN_REVNUM_T_FMT 
                                   " '%s'", db->resource->info->root.rev,
                                   svn_path_uri_encode(propname,
                                                       db->resource->pool)));
      }
  else
    serr = svn_repos_fs_change_node_prop(db->resource->info->root.root,
                                         get_repos_path(db->resource->info),
                                         propname, value, db->resource->pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               NULL,
                               db->resource->pool);

  /* a change to the props was made; make sure our cached copy is gone */
  db->props = NULL;

  return NULL;
}

static dav_error *dav_svn_db_open(apr_pool_t *p, const dav_resource *resource,
                                  int ro, dav_db **pdb)
{
  dav_db *db;
  dav_svn_authz_read_baton *arb;

  /* Some resource types do not have deadprop databases. Specifically:
     REGULAR, VERSION, and WORKING resources have them. (SVN does not
     have WORKSPACE resources, and isn't covered here) */
  if (resource->type == DAV_RESOURCE_TYPE_HISTORY
      || resource->type == DAV_RESOURCE_TYPE_ACTIVITY
      || resource->type == DAV_RESOURCE_TYPE_PRIVATE)
    {
      *pdb = NULL;
      return NULL;
    }

  /* If the DB is being opened R/W, and this isn't a working resource, then
     we have a problem! */
  if (!ro && resource->type != DAV_RESOURCE_TYPE_WORKING)
    {
      /* ### Exception: in violation of deltaV, we *are* allowing a
         baseline resource to receive a proppatch, as a way of
         changing unversioned rev props.  Remove this someday: see IZ #916. */
      if (! (resource->baselined
             && resource->type == DAV_RESOURCE_TYPE_VERSION))
        return dav_new_error(p, HTTP_CONFLICT, 0,
                             "Properties may only be changed on working "
                             "resources.");
    }

  db = apr_pcalloc(p, sizeof(*db));

  db->resource = resource;
  db->p = svn_pool_create(p);

  /* ### temp hack */
  db->work = svn_stringbuf_ncreate("", 0, db->p);

  /* make our path-based authz callback available to svn_repos_* funcs. */
  arb = apr_pcalloc(p, sizeof(*arb));
  arb->r = resource->info->r;
  arb->repos = resource->info->repos;
  db->authz_read_baton = arb;
  db->authz_read_func = dav_svn_authz_read_func(arb);

  /* ### use RO and node's mutable status to look for an error? */

  *pdb = db;

  return NULL;
}

static void dav_svn_db_close(dav_db *db)
{
  svn_pool_destroy(db->p);
}

static dav_error *dav_svn_db_define_namespaces(dav_db *db, dav_xmlns_info *xi)
{
  dav_xmlns_add(xi, "S", SVN_DAV_PROP_NS_SVN);
  dav_xmlns_add(xi, "C", SVN_DAV_PROP_NS_CUSTOM);
  dav_xmlns_add(xi, "V", SVN_DAV_PROP_NS_DAV);

  /* ### we don't have any other possible namespaces right now. */

  return NULL;
}

static dav_error *dav_svn_db_output_value(dav_db *db, 
                                          const dav_prop_name *name,
                                          dav_xmlns_info *xi,
                                          apr_text_header *phdr, int *found)
{
  const char *prefix;
  const char *s;
  svn_string_t *propval;
  dav_error *err;
  apr_pool_t *pool = db->resource->pool;

  if ((err = get_value(db, name, &propval)) != NULL)
    return err;

  /* return whether the prop was found, then punt or handle it. */
  *found = (propval != NULL);
  if (propval == NULL)
    return NULL;

  if (strcmp(name->ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    prefix = "C:";
  else
    prefix = "S:";

  if (propval->len == 0)
    {
      /* empty value. add an empty elem. */
      s = apr_psprintf(pool, "<%s%s/>" DEBUG_CR, prefix, name->name);
      apr_text_append(pool, phdr, s);
    }
  else
    {
      /* add <prefix:name [V:encoding="base64"]>value</prefix:name> */
      const char *xml_safe;
      const char *encoding = "";

      /* Ensure XML-safety of our property values before sending them
         across the wire. */
      if (! svn_xml_is_xml_safe(propval->data, propval->len))
        {
          const svn_string_t *enc_propval
            = svn_base64_encode_string(propval, pool);
          xml_safe = enc_propval->data;
          encoding = apr_pstrcat(pool, " V:encoding=\"base64\"", NULL);
        }
      else
        {    
          svn_stringbuf_t *xmlval = NULL;
          svn_xml_escape_cdata_string(&xmlval, propval, pool);
          xml_safe = xmlval->data;
        }

      s = apr_psprintf(pool, "<%s%s%s>", prefix, name->name, encoding);
      apr_text_append(pool, phdr, s);

      /* the value is in our pool which means it has the right lifetime. */
      /* ### at least, per the current mod_dav architecture/API */
      apr_text_append(pool, phdr, xml_safe);
      
      s = apr_psprintf(pool, "</%s%s>" DEBUG_CR, prefix, name->name);
      apr_text_append(pool, phdr, s);
    }

  return NULL;
}

static dav_error *dav_svn_db_map_namespaces(
    dav_db *db,
    const apr_array_header_t *namespaces,
    dav_namespace_map **mapping)
{
  /* we don't need a namespace mapping right now. nothing to do */

  return NULL;
}

static dav_error *dav_svn_db_store(dav_db *db, const dav_prop_name *name,
                                   const apr_xml_elem *elem,
                                   dav_namespace_map *mapping)
{
  const svn_string_t *propval;
  apr_pool_t *pool = db->p;
  apr_xml_attr *attr = elem->attr;

  /* SVN sends property values as a big blob of bytes. Thus, there should be
     no child elements of the property-name element. That also means that
     the entire contents of the blob is located in elem->first_cdata. The
     dav_xml_get_cdata() will figure it all out for us, but (normally) it
     should be awfully fast and not need to copy any data. */

  propval = svn_string_create
    (dav_xml_get_cdata(elem, pool, 0 /* strip_white */), pool);
  
  /* Check for special encodings of the property value. */
  while (attr)
    {
      if (strcmp(attr->name, "encoding") == 0) /* ### namespace check? */
        {
          const char *enc_type = attr->value;

          /* Handle known encodings here. */
          if (enc_type && (strcmp(enc_type, "base64") == 0))
            propval = svn_base64_decode_string(propval, pool);
          else
            return dav_new_error(pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                                 "Unknown property encoding");
          break;
        }
      /* Next attribute, please. */
      attr = attr->next;
    }

  return save_value(db, name, propval);
}

static dav_error *dav_svn_db_remove(dav_db *db, const dav_prop_name *name)
{
  svn_error_t *serr;
  const char *propname;

  /* get the repos-local name */
  get_repos_propname(db, name, &propname);

  /* ### non-svn props aren't in our repos, so punt for now */
  if (propname == NULL)
    return NULL;

  /* Working Baseline or Working (Version) Resource */
  if (db->resource->baselined)
    if (db->resource->working)
      serr = svn_repos_fs_change_txn_prop(db->resource->info->root.txn,
                                          propname, NULL, db->resource->pool);
    else
      /* ### VIOLATING deltaV: you can't proppatch a baseline, it's
         not a working resource!  But this is how we currently
         (hackily) allow the svn client to change unversioned rev
         props.  See issue #916. */
      serr = svn_repos_fs_change_rev_prop2(db->resource->info->repos->repos,
                                           db->resource->info->root.rev,
                                           db->resource->info->repos->username,
                                           propname, NULL,
                                           db->authz_read_func,
                                           db->authz_read_baton,
                                           db->resource->pool);
  else
    serr = svn_repos_fs_change_node_prop(db->resource->info->root.root,
                                         get_repos_path(db->resource->info),
                                         propname, NULL, db->resource->pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not remove a property",
                               db->resource->pool);

  /* a change to the props was made; make sure our cached copy is gone */
  db->props = NULL;

  return NULL;
}

static int dav_svn_db_exists(dav_db *db, const dav_prop_name *name)
{
  const char *propname;
  svn_string_t *propval;
  svn_error_t *serr;
  int retval;

  /* get the repos-local name */
  get_repos_propname(db, name, &propname);

  /* ### non-svn props aren't in our repos */
  if (propname == NULL)
    return 0;

  /* Working Baseline, Baseline, or (Working) Version resource */
  if (db->resource->baselined)
    if (db->resource->type == DAV_RESOURCE_TYPE_WORKING)
      serr = svn_fs_txn_prop(&propval, db->resource->info->root.txn,
                             propname, db->p);
    else
      serr = svn_repos_fs_revision_prop(&propval,
                                        db->resource->info->repos->repos,
                                        db->resource->info->root.rev,
                                        propname,
                                        db->authz_read_func,
                                        db->authz_read_baton, db->p);
  else
    serr = svn_fs_node_prop(&propval, db->resource->info->root.root,
                            get_repos_path(db->resource->info),
                            propname, db->p);

  /* ### try and dispose of the value? */

  retval = (serr == NULL && propval != NULL);
  svn_error_clear(serr);
  return retval;
}

static void get_name(dav_db *db, dav_prop_name *pname)
{
  if (db->hi == NULL)
    {
      pname->ns = pname->name = NULL;
    }
  else
    {
      const void *name;

      apr_hash_this(db->hi, &name, NULL, NULL);

#define PREFIX_LEN (sizeof(SVN_PROP_PREFIX) - 1)
      if (strncmp(name, SVN_PROP_PREFIX, PREFIX_LEN) == 0)
#undef PREFIX_LEN
        {
          pname->ns = SVN_DAV_PROP_NS_SVN;
          pname->name = (const char *)name + 4;
        }
      else
        {
          pname->ns = SVN_DAV_PROP_NS_CUSTOM;
          pname->name = name;
        }
    }
}

static dav_error *dav_svn_db_first_name(dav_db *db, dav_prop_name *pname)
{
  /* if we don't have a copy of the properties, then get one */
  if (db->props == NULL)
    {
      svn_error_t *serr;

      /* Working Baseline, Baseline, or (Working) Version resource */
      if (db->resource->baselined)
        {
          if (db->resource->type == DAV_RESOURCE_TYPE_WORKING)
            serr = svn_fs_txn_proplist(&db->props, 
                                       db->resource->info->root.txn,
                                       db->p);
          else
            serr = svn_repos_fs_revision_proplist
              (&db->props,
               db->resource->info->repos->repos,
               db->resource->info->root.rev,
               db->authz_read_func,
               db->authz_read_baton,
               db->p);
        }
      else
        {
          serr = svn_fs_node_proplist(&db->props, 
                                      db->resource->info->root.root,
                                      get_repos_path(db->resource->info), 
                                      db->p);
        }
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "could not begin sequencing through "
                                   "properties",
                                   db->resource->pool);
    }

  /* begin the iteration over the hash */
  db->hi = apr_hash_first(db->p, db->props);

  /* fetch the first key */
  get_name(db, pname);

  return NULL;
}

static dav_error *dav_svn_db_next_name(dav_db *db, dav_prop_name *pname)
{
  /* skip to the next hash entry */
  if (db->hi != NULL)
    db->hi = apr_hash_next(db->hi);

  /* fetch the key */
  get_name(db, pname);

  return NULL;
}

static dav_error *dav_svn_db_get_rollback(dav_db *db, 
                                          const dav_prop_name *name,
                                          dav_deadprop_rollback **prollback)
{
  dav_error *err;
  dav_deadprop_rollback *ddp;
  svn_string_t *propval;

  if ((err = get_value(db, name, &propval)) != NULL)
    return err;

  ddp = apr_palloc(db->p, sizeof(*ddp));
  ddp->name = *name;
  ddp->value.data = propval ? propval->data : NULL;
  ddp->value.len = propval ? propval->len : 0;

  *prollback = ddp;
  return NULL;
}

static dav_error *dav_svn_db_apply_rollback(dav_db *db,
                                            dav_deadprop_rollback *rollback)
{
  if (rollback->value.data == NULL)
    {
      return dav_svn_db_remove(db, &rollback->name);
    }
  
  return save_value(db, &rollback->name, &rollback->value);
}


const dav_hooks_propdb dav_svn_hooks_propdb = {
  dav_svn_db_open,
  dav_svn_db_close,
  dav_svn_db_define_namespaces,
  dav_svn_db_output_value,
  dav_svn_db_map_namespaces,
  dav_svn_db_store,
  dav_svn_db_remove,
  dav_svn_db_exists,
  dav_svn_db_first_name,
  dav_svn_db_next_name,
  dav_svn_db_get_rollback,
  dav_svn_db_apply_rollback,
};
