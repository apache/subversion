/* caching.c : in-memory caching
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

#include "fs.h"
#include "fs_fs.h"
#include "id.h"
#include "dag.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/*** Dup/serialize/deserialize functions. ***/


/** Caching SVN_FS_ID_T values. **/
static svn_cache_dup_func_t dup_id;
static svn_error_t *
dup_id(void **out,
       void *in,
       apr_pool_t *pool)
{
  svn_fs_id_t *id = in;
  *out = svn_fs_fs__id_copy(id, pool);
  return SVN_NO_ERROR;
}

static svn_cache_serialize_func_t serialize_id;
static svn_error_t *
serialize_id(char **data,
             apr_size_t *data_len,
             void *in,
             apr_pool_t *pool)
{
  svn_fs_id_t *id = in;
  svn_string_t *id_str = svn_fs_fs__id_unparse(id, pool);
  *data = (char *) id_str->data;
  *data_len = id_str->len;

  return SVN_NO_ERROR;
}


static svn_cache_deserialize_func_t deserialize_id;
static svn_error_t *
deserialize_id(void **out,
               const char *data,
               apr_size_t data_len,
               apr_pool_t *pool)
{
  svn_fs_id_t *id = svn_fs_fs__id_parse(data, data_len, pool);
  if (id == NULL)
    {
      return svn_error_create(SVN_ERR_FS_NOT_ID, NULL,
                              _("Bad ID in cache"));
    }

  *out = id;
  return SVN_NO_ERROR;
}


/** Caching directory listings. **/
static svn_cache_dup_func_t dup_dir_listing;
static svn_error_t *
dup_dir_listing(void **out,
                void *in,
                apr_pool_t *pool)
{
  apr_hash_t *new_entries = apr_hash_make(pool), *entries = in;
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_fs_dirent_t *dirent, *new_dirent;

      apr_hash_this(hi, NULL, NULL, &val);
      dirent = val;
      new_dirent = apr_palloc(pool, sizeof(*new_dirent));
      new_dirent->name = apr_pstrdup(pool, dirent->name);
      new_dirent->kind = dirent->kind;
      new_dirent->id = svn_fs_fs__id_copy(dirent->id, pool);
      apr_hash_set(new_entries, new_dirent->name, APR_HASH_KEY_STRING,
                   new_dirent);
    }

  *out = new_entries;
  return SVN_NO_ERROR;
}


/* Return a *MEMCACHE_P for FS if it's configured to use memcached.
   Use FS->pool for allocations. */
svn_error_t *
find_memcache(apr_memcache_t **memcache_p,
              svn_fs_t *fs)
{
  apr_memcache_t *memcache;
  apr_status_t apr_err;
  apr_memcache_server_t *server;

  /* ### TODO: Read from a config file. */

  apr_err = apr_memcache_create(fs->pool,
                                5, /* ### TODO: max servers */
                                0, /* flags */
                                &memcache);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown error creating apr_memcache_t"));

  apr_err = apr_memcache_server_create(fs->pool,
                                       "localhost",
                                       11211, /* default port */
                                       0,  /* min connections */
                                       5,  /* soft max connections */
                                       10, /* hard max connections */
                                       50, /* connection time to live (secs) */
                                       &server);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown error creating memcache server"));

  apr_err = apr_memcache_add_server(memcache, server);
  if (apr_err != APR_SUCCESS)
    return svn_error_wrap_apr(apr_err,
                              _("Unknown error adding server to memcache"));

  *memcache_p = memcache;
  return SVN_NO_ERROR;

}


svn_error_t *
svn_fs_fs__initialize_caches(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *prefix = apr_pstrcat(fs->pool,
                                   "fsfs:", ffd->uuid,
                                   "/", fs->path, ":",
                                   NULL);
  apr_memcache_t *memcache;

  SVN_ERR(find_memcache(&memcache, fs));

  /* Make the cache for revision roots.  For the vast majority of
   * commands, this is only going to contain a few entries (svnadmin
   * dump/verify is an exception here), so to reduce overhead let's
   * try to keep it to just one page.  I estimate each entry has about
   * 72 bytes of overhead (svn_revnum_t key, svn_fs_id_t +
   * id_private_t + 3 strings for value, and the cache_entry); the
   * default pool size is 8192, so about a hundred should fit
   * comfortably. */
  if (memcache)
    SVN_ERR(svn_cache_create_memcache(&(ffd->rev_root_id_cache),
                                      memcache,
                                      serialize_id,
                                      deserialize_id,
                                      sizeof(svn_revnum_t),
                                      apr_pstrcat(fs->pool, prefix, "RRI",
                                                  NULL),
                                      fs->pool));
  else
    SVN_ERR(svn_cache_create_inprocess(&(ffd->rev_root_id_cache),
                                       dup_id, sizeof(svn_revnum_t),
                                       1, 100, FALSE, fs->pool));


  /* Rough estimate: revision DAG nodes have size around 320 bytes, so
   * let's put 16 on a page. */
  if (memcache)
    SVN_ERR(svn_cache_create_memcache(&(ffd->rev_node_cache),
                                      memcache,
                                      svn_fs_fs__dag_serialize,
                                      svn_fs_fs__dag_deserialize,
                                      APR_HASH_KEY_STRING,
                                      apr_pstrcat(fs->pool, prefix, "DAG",
                                                  NULL),
                                      fs->pool));
  else
    SVN_ERR(svn_cache_create_inprocess(&(ffd->rev_node_cache),
                                       svn_fs_fs__dag_dup_for_cache,
                                       APR_HASH_KEY_STRING,
                                       1024, 16, FALSE, fs->pool));

  /* Very rough estimate: 1K per directory. */
  if (memcache)
    SVN_ERR(svn_cache_create_memcache(&(ffd->dir_cache),
                                      memcache,
                                      svn_fs_fs__dir_entries_serialize,
                                      svn_fs_fs__dir_entries_deserialize,
                                      APR_HASH_KEY_STRING,
                                      apr_pstrcat(fs->pool, prefix, "DIR",
                                                  NULL),
                                      fs->pool));
  else
    SVN_ERR(svn_cache_create_inprocess(&(ffd->dir_cache),
                                       dup_dir_listing, APR_HASH_KEY_STRING,
                                       1024, 8, FALSE, fs->pool));

  return SVN_NO_ERROR;
}
