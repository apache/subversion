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
};

static dav_error *dav_svn_db_open(apr_pool_t *p, const dav_resource *resource,
                                  int ro, dav_db **pdb)
{
  dav_db *db;

  /* Baselines and some resource types do not have deadprop databases. */
  /* ### baselines might in the future; clients "could" attach a property
     ### to the working baseline. */
  if ((resource->type == DAV_RESOURCE_TYPE_VERSION && resource->baselined)
      || resource->type == DAV_RESOURCE_TYPE_HISTORY
      || resource->type == DAV_RESOURCE_TYPE_ACTIVITY
      || resource->type == DAV_RESOURCE_TYPE_PRIVATE)
    {
      *pdb = NULL;
      return NULL;
    }

  db = apr_pcalloc(p, sizeof(*db));

  db->resource = resource;
  db->p = p;

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
  return NULL;
}

static dav_error *dav_svn_db_store(dav_db *db, dav_datum key, dav_datum value)
{
  svn_string_t propname = { key.dptr, key.dsize };
  svn_string_t propval = { value.dptr, value.dsize };
  svn_error_t *serr;

  /* ### hope node is open, and it is mutable */

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

  serr = svn_fs_node_prop(&propval, db->resource->info->root.root,
                          db->resource->info->repos_path,
                          &propname, db->p);

  /* ### try and dispose of the value? */

  return serr == NULL && propval != NULL;
}

static void get_key(apr_hash_index_t *hi, dav_datum *pkey)
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
    }
}

static dav_error *dav_svn_db_firstkey(dav_db *db, dav_datum *pkey)
{
  /* if we don't have a copy of the properties, then get one */
  if (db->props == NULL)
    {
      svn_error_t *serr;

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
  get_key(db->hi, pkey);

  return NULL;
}

static dav_error *dav_svn_db_nextkey(dav_db *db, dav_datum *pkey)
{
  /* skip to the next hash entry */
  if (db->hi != NULL)
    db->hi = apr_hash_next(db->hi);

  /* fetch the key */
  get_key(db->hi, pkey);

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
