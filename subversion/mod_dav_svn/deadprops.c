/*
 * deadprops.c: mod_dav_svn dead property provider functions for Subversion
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
  dav_db *db = apr_pcalloc(p, sizeof(*db));

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
  svn_string_t propname = { key.dptr, key.dsize, 0, NULL };
  svn_string_t *propval;
  svn_error_t *serr;

  serr = svn_fs_get_node_prop(&propval, db->resource->info->node,
                              &propname, db->p);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not fetch a property");

  pvalue->dptr = propval->data;
  pvalue->dsize = propval->len;
  return NULL;
}

static dav_error *dav_svn_db_store(dav_db *db, dav_datum key, dav_datum value)
{
  svn_string_t propname = { key.dptr, key.dsize, 0, NULL };
  svn_string_t propval = { value.dptr, value.dsize, 0, NULL };
  svn_error_t *serr;

  /* ### hope node is open, and it is mutable */

  serr = svn_fs_change_prop(db->resource->info->node, &propname, &propval);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not change a property");

  /* a change to the props was made; make sure our cached copy is gone */
  db->props = NULL;

  return NULL;
}

static dav_error *dav_svn_db_remove(dav_db *db, dav_datum key)
{
  svn_string_t propname = { key.dptr, key.dsize, 0, NULL };
  svn_error_t *serr;

  /* ### hope node is open, and it is mutable */

  serr = svn_fs_change_prop(db->resource->info->node, &propname, NULL);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not remove a property");

  /* a change to the props was made; make sure our cached copy is gone */
  db->props = NULL;

  return NULL;
}

static int dav_svn_db_exists(dav_db *db, dav_datum key)
{
  svn_string_t propname = { key.dptr, key.dsize, 0, NULL };
  svn_string_t *propval;
  svn_error_t *serr;

  serr = svn_fs_get_node_prop(&propval, db->resource->info->node,
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

      serr = svn_fs_get_node_proplist(&db->props, db->resource->info->node,
                                      db->p);
      if (serr != NULL)
        return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                   "could not begin sequencing through "
                                   "properties");
    }

  /* begin the iteration over the hash */
  db->hi = apr_hash_first(db->props);

  /* fetch the first key */
  get_key(db->hi, pkey);

  return NULL;
}

static dav_error *dav_svn_db_nextkey(dav_db *db, dav_datum *pkey)
{
  /* skip to the next hash entry */
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
