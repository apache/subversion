/*
 * deadprops.c: mod_dav_svn dead property provider functions for Subversion
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
#include <mod_dav.h>

#include <apr_hash.h>

#include "dav_svn.h"


struct dav_db {
  const dav_resource *resource;
  apr_pool_t *p;

  /* the resource's properties that we are sequencing over */
  apr_hash_t *props;
  apr_hash_index_t *hi;

  /* ### part of hack on prop name changes/mapping */
  svn_stringbuf_t *work;
};

/* ### temp hack for fixing prop names */
static void fix_name(dav_db *db, svn_string_t *propname)
{
  /* all properties have no prefix, or they have an svn: prefix */
  if (*propname->data == ':')
    {
      /* not in a namespace, so drop the ':' */
      ++propname->data;
      --propname->len;
    }
  else
    {
      const char *s = ap_strchr_c(propname->data, ':');

      svn_stringbuf_set(db->work, "svn:");
      svn_stringbuf_appendcstr(db->work, s + 1);
      propname->data = db->work->data;
      propname->len = db->work->len;
    }
}

static dav_error *dav_svn_db_open(apr_pool_t *p, const dav_resource *resource,
                                  int ro, dav_db **pdb)
{
  dav_db *db;

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
      return dav_new_error(p, HTTP_CONFLICT, 0,
                           "Properties may only be changed on working "
                           "resources.");
    }

  db = apr_pcalloc(p, sizeof(*db));

  db->resource = resource;
  db->p = p;

  /* ### temp hack */
  db->work = svn_stringbuf_ncreate("", 0, p);

  /* ### use RO and node's mutable status to look for an error? */

  *pdb = db;

  return NULL;
}

static void dav_svn_db_close(dav_db *db)
{
  /* nothing to do */
}

static dav_error *dav_svn_db_fetch(dav_db *db, dav_datum key,
                                   dav_datum *pvalue)
{
  svn_string_t propname = { key.dptr, key.dsize };
  svn_stringbuf_t *propval;
  svn_error_t *serr;

  if (strcmp(key.dptr, "METADATA") == 0)
    {
#define THE_METADATA "\004\000\000\002DAV:\0svn:" /* plus '\0' */
      pvalue->dptr = (char *)THE_METADATA;
      pvalue->dsize = sizeof(THE_METADATA);
      return NULL;
    }

  /* ### temp hack to remap the name */
  fix_name(db, &propname);

  /* ### if db->props exists, then try in there first */

  /* Working Baseline, Baseline, or (Working) Version resource */
  if (db->resource->baselined)
    if (db->resource->type == DAV_RESOURCE_TYPE_WORKING)
      serr = svn_fs_txn_prop(&propval, db->resource->info->root.txn,
                             &propname, db->p);
    else
      serr = svn_fs_revision_prop(&propval, db->resource->info->repos->fs,
                                  db->resource->info->root.rev,
                                  &propname, db->p);
  else
    serr = svn_fs_node_prop(&propval, db->resource->info->root.root,
                            db->resource->info->repos_path,
                            &propname, db->p);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not fetch a property");

  if (propval == NULL)
    {
      /* the property wasn't present. */
      pvalue->dptr = NULL;
      pvalue->dsize = 0;
      return NULL;
    }

  pvalue->dptr = propval->data;
  pvalue->dsize = propval->len;

  /* ### temp hack. fix the return value */
  svn_stringbuf_setempty(db->work);
  svn_stringbuf_appendbytes(db->work, "", 1);
  svn_stringbuf_appendbytes(db->work, propval->data, propval->len);
  pvalue->dptr = db->work->data;
  pvalue->dsize = db->work->len + 1;    /* include null term */

  return NULL;
}

static dav_error *dav_svn_db_store(dav_db *db, dav_datum key, dav_datum value)
{
  svn_string_t propname = { key.dptr, key.dsize };
  svn_string_t propval = { value.dptr, value.dsize };
  svn_error_t *serr;

  /* ### hope node is open, and it is mutable */

  /* don't store this */
  if (strcmp(key.dptr, "METADATA") == 0)
    return NULL;

  /* ### temp hack to remap the name. fix the value (skip lang; toss null). */
  fix_name(db, &propname);
  ++propval.data;
  propval.len -= 2;

  /* Working Baseline or Working (Version) Resource */
  if (db->resource->baselined)
    serr = svn_fs_change_txn_prop(db->resource->info->root.txn,
                                  &propname, &propval, db->resource->pool);
  else
    serr = svn_fs_change_node_prop(db->resource->info->root.root,
                                   db->resource->info->repos_path,
                                   &propname, &propval, db->resource->pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not change a property");

  /* a change to the props was made; make sure our cached copy is gone */
  db->props = NULL;

  return NULL;
}

static dav_error *dav_svn_db_remove(dav_db *db, dav_datum key)
{
  svn_string_t propname = { key.dptr, key.dsize };
  svn_error_t *serr;

  /* ### hope node is open, and it is mutable */

  /* ### temp hack to remap the name */
  fix_name(db, &propname);

  /* Working Baseline or Working (Version) Resource */
  if (db->resource->baselined)
    serr = svn_fs_change_txn_prop(db->resource->info->root.txn,
                                  &propname, NULL, db->resource->pool);
  else
    serr = svn_fs_change_node_prop(db->resource->info->root.root,
                                   db->resource->info->repos_path,
                                   &propname, NULL, db->resource->pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not remove a property");

  /* a change to the props was made; make sure our cached copy is gone */
  db->props = NULL;

  return NULL;
}

static int dav_svn_db_exists(dav_db *db, dav_datum key)
{
  svn_string_t propname = { key.dptr, key.dsize };
  svn_stringbuf_t *propval;
  svn_error_t *serr;

  /* ### temp hack to remap the name */
  fix_name(db, &propname);

  /* Working Baseline, Baseline, or (Working) Version resource */
  if (db->resource->baselined)
    if (db->resource->type == DAV_RESOURCE_TYPE_WORKING)
      serr = svn_fs_txn_prop(&propval, db->resource->info->root.txn,
                             &propname, db->p);
    else
      serr = svn_fs_revision_prop(&propval, db->resource->info->repos->fs,
                                  db->resource->info->root.rev,
                                  &propname, db->p);
  else
    serr = svn_fs_node_prop(&propval, db->resource->info->root.root,
                            db->resource->info->repos_path,
                            &propname, db->p);

  /* ### try and dispose of the value? */

  return serr == NULL && propval != NULL;
}

static void get_key(apr_hash_index_t *hi, dav_datum *pkey,
                    dav_db *db)
{
  if (hi == NULL)
    {
      pkey->dptr = NULL;
      pkey->dsize = 0;
    }
  else
    {
      const void *name;
      apr_size_t namelen;

      apr_hash_this(hi, &name, &namelen, NULL);
      pkey->dptr = (char *)name;        /* hope the caller doesn't change */
      pkey->dsize = namelen;

      /* ### temp hack for name remapping. a prop with the svn: prefix will
         ### get a namespace index of 1. others will get "no namespace" */
      if (strncmp(name, "svn:", 4) == 0)
        {
          svn_stringbuf_set(db->work, "1:");
          svn_stringbuf_appendcstr(db->work, (const char *)name + 4);
        }
      else
        {
          svn_stringbuf_set(db->work, ":");
          svn_stringbuf_appendcstr(db->work, name);
        }
      pkey->dptr = db->work->data;
      pkey->dsize = db->work->len + 1;  /* include null term */
    }
}

static dav_error *dav_svn_db_firstkey(dav_db *db, dav_datum *pkey)
{
  /* if we don't have a copy of the properties, then get one */
  if (db->props == NULL)
    {
      svn_error_t *serr;

      /* Working Baseline, Baseline, or (Working) Version resource */
      if (db->resource->baselined)
        if (db->resource->type == DAV_RESOURCE_TYPE_WORKING)
          serr = svn_fs_txn_proplist(&db->props, db->resource->info->root.txn,
                                     db->p);
        else
          serr = svn_fs_revision_proplist(&db->props,
                                          db->resource->info->repos->fs,
                                          db->resource->info->root.rev, db->p);
      else
        serr = svn_fs_node_proplist(&db->props, db->resource->info->root.root,
                                    db->resource->info->repos_path, db->p);
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "could not begin sequencing through "
                                   "properties");
    }

  /* begin the iteration over the hash */
  db->hi = apr_hash_first(db->p, db->props);

  /* fetch the first key */
  get_key(db->hi, pkey, db);

  return NULL;
}

static dav_error *dav_svn_db_nextkey(dav_db *db, dav_datum *pkey)
{
  /* skip to the next hash entry */
  if (db->hi != NULL)
    db->hi = apr_hash_next(db->hi);

  /* fetch the key */
  get_key(db->hi, pkey, db);

  return NULL;
}

static void dav_svn_db_freedatum(dav_db *db, dav_datum data)
{
  /* nothing to do */
}

const dav_hooks_propdb dav_svn_hooks_propdb = {
  dav_svn_db_open,
  dav_svn_db_close,
  dav_svn_db_fetch,
  dav_svn_db_store,
  dav_svn_db_remove,
  dav_svn_db_exists,
  dav_svn_db_firstkey,
  dav_svn_db_nextkey,
  dav_svn_db_freedatum
};


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
