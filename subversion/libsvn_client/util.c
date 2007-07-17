/*
 * util.c :  utility functions for the libsvn_client library
 *
 * ====================================================================
 * Copyright (c) 2005 CollabNet.  All rights reserved.
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

#include <assert.h>
#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_path.h"
#include "svn_client.h"

#include "private/svn_wc_private.h"

#include "client.h"

#include "svn_private_config.h"

/* Duplicate a HASH containing (char * -> svn_string_t *) key/value
   pairs using POOL. */
static apr_hash_t *
string_hash_dup(apr_hash_t *hash, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *key;
  apr_ssize_t klen;
  void *val;
  apr_hash_t *new_hash = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, &klen, &val);
      key = apr_pstrdup(pool, key);
      val = svn_string_dup(val, pool);
      apr_hash_set(new_hash, key, klen, val);
    }
  return new_hash;
}

svn_error_t *
svn_client_commit_item_create(const svn_client_commit_item3_t **item,
                              apr_pool_t *pool)
{
  *item = apr_pcalloc(pool, sizeof(svn_client_commit_item3_t));
  return SVN_NO_ERROR;
}

svn_client_commit_item3_t *
svn_client_commit_item3_dup(const svn_client_commit_item3_t *item,
                            apr_pool_t *pool)
{
  svn_client_commit_item3_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup(pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  if (new_item->copyfrom_url)
    new_item->copyfrom_url = apr_pstrdup(pool, new_item->copyfrom_url);

  if (new_item->incoming_prop_changes)
    new_item->incoming_prop_changes =
      svn_prop_array_dup(new_item->incoming_prop_changes, pool);

  if (new_item->outgoing_prop_changes)
    new_item->outgoing_prop_changes =
      svn_prop_array_dup(new_item->outgoing_prop_changes, pool);

  return new_item;
}

svn_client_commit_item2_t *
svn_client_commit_item2_dup(const svn_client_commit_item2_t *item,
                            apr_pool_t *pool)
{
  svn_client_commit_item2_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup(pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  if (new_item->copyfrom_url)
    new_item->copyfrom_url = apr_pstrdup(pool, new_item->copyfrom_url);

  if (new_item->wcprop_changes)
    new_item->wcprop_changes = svn_prop_array_dup(new_item->wcprop_changes,
                                                  pool);

  return new_item;
}

svn_client_proplist_item_t *
svn_client_proplist_item_dup(const svn_client_proplist_item_t *item,
                             apr_pool_t * pool)
{
  svn_client_proplist_item_t *new_item
    = apr_pcalloc(pool, sizeof(*new_item));

  if (item->node_name)
    new_item->node_name = svn_stringbuf_dup(item->node_name, pool);

  if (item->prop_hash)
    new_item->prop_hash = string_hash_dup(item->prop_hash, pool);

  return new_item;
}

svn_error_t *
svn_client__path_relative_to_root(const char **rel_path,
                                  const char *path_or_url,
                                  const char *repos_root,
                                  svn_ra_session_t *ra_session,
                                  svn_wc_adm_access_t *adm_access,
                                  apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t need_wc_cleanup = FALSE;
  svn_boolean_t is_path = !svn_path_is_url(path_or_url);

  /* Old WCs may not provide the repository URL. */
  assert(repos_root != NULL || ra_session != NULL);

  /* If we have a WC path, transform it into a URL for use in
     calculating its path relative to the repository root.

     If we don't already know the repository root, derive it.  If we
     have a WC path, first look in the entries file.  Fall back to
     asking the RA session. */
  if (is_path && repos_root == NULL)
    {
      const svn_wc_entry_t *entry;

      if (adm_access == NULL)
        {
          SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url,
                                         FALSE, 0, NULL, NULL, pool));
          need_wc_cleanup = TRUE;
        }
      err = svn_wc__entry_versioned(&entry, path_or_url, adm_access, FALSE,
                                    pool);

      if (err)
        goto cleanup;

      path_or_url = entry->url;
      repos_root = entry->repos;
    }
  if (repos_root == NULL)
    {
      /* We may be operating on a URL, or have been otherwise unable
         to determine the repository root. */
      err = svn_ra_get_repos_root(ra_session, &repos_root, pool);
      if (err)
        goto cleanup;
    }

  /* Calculate the path relative to the repository root. */
  *rel_path = svn_path_is_child(repos_root, path_or_url, pool);

  /* Assure that the path begins with a slash, as the path is NULL if
     the URL is the repository root. */
  *rel_path = svn_path_join("/", *rel_path ? *rel_path : "", pool);
  *rel_path = svn_path_uri_decode(*rel_path, pool);

 cleanup:
  if (need_wc_cleanup)
    {
      svn_error_t *err2 = svn_wc_adm_close(adm_access);
      if (! err)
        err = err2;
      else
        svn_error_clear(err2);
    }
  return err;
}

/* --- SVNPATCH CLIENT ROUTINES --- */

/* --- WRITING DATA ITEMS --- */
 
void
svn_client_write_number(svn_stringbuf_t *targetstr,
                        apr_uint64_t number)
{
  svn_stringbuf_appendformat(targetstr, "%" APR_UINT64_T_FMT " ", number);
}

void
svn_client_write_string(svn_stringbuf_t *targetstr,
                        const svn_string_t *str)
{
  /* svn_stringbuf_appendformat doesn't support binary bytes.  Since str->data
     might contain binary stuff, let's make use of appendbytes instead. */
  svn_stringbuf_appendformat(targetstr, "%" APR_SIZE_T_FMT ":", str->len);
  svn_stringbuf_appendbytes(targetstr, str->data, str->len);
  svn_stringbuf_appendbytes(targetstr, " ", 1);
}

void
svn_client_write_cstring(svn_stringbuf_t *targetstr,
                         const char *s)
{
  svn_stringbuf_appendformat(targetstr, "%" APR_SIZE_T_FMT ":%s ",
                             strlen(s), s);
}

void
svn_client_write_word(svn_stringbuf_t *targetstr,
                      const char *word)
{
  svn_stringbuf_appendformat(targetstr, "%s ", word);
}

void
svn_client_write_proplist(svn_stringbuf_t *targetstr,
                          apr_hash_t *props,
                          apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  const char *propname;
  svn_string_t *propval;

  if (props)
    {
      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
        {
          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          propname = key;
          propval = val;          
          svn_client_write_tuple(targetstr, "cs", propname, propval);
        }
      svn_pool_destroy(iterpool);
    }
}

void
svn_client_start_list(svn_stringbuf_t *targetstr)
{
  svn_stringbuf_appendbytes(targetstr, "( ", 2);
}

void
svn_client_end_list(svn_stringbuf_t *targetstr)
{
  svn_stringbuf_appendbytes(targetstr, ") ", 2);
}

/* --- WRITING TUPLES --- */

static void
vwrite_tuple(svn_stringbuf_t *targetstr,
             const char *fmt,
             va_list ap)
{
  svn_boolean_t opt = FALSE;
  svn_revnum_t rev;
  const char *cstr;
  const svn_string_t *str;

  if (*fmt == '!')
    fmt++;
  else
    svn_client_start_list(targetstr);
  for (; *fmt; fmt++)
    {
      if (*fmt == 'n' && !opt)
        svn_client_write_number(targetstr, va_arg(ap, apr_uint64_t));
      else if (*fmt == 'r')
        {
          rev = va_arg(ap, svn_revnum_t);
          assert(opt || SVN_IS_VALID_REVNUM(rev));
          if (SVN_IS_VALID_REVNUM(rev))
            svn_client_write_number(targetstr, rev);
        }
      else if (*fmt == 's')
        {
          str = va_arg(ap, const svn_string_t *);
          assert(opt || str);
          if (str)
            svn_client_write_string(targetstr, str);
        }
      else if (*fmt == 'c')
        {
          cstr = va_arg(ap, const char *);
          assert(opt || cstr);
          if (cstr)
            svn_client_write_cstring(targetstr, cstr);
        }
      else if (*fmt == 'w')
        {
          cstr = va_arg(ap, const char *);
          assert(opt || cstr);
          if (cstr)
            svn_client_write_word(targetstr, cstr);
        }
      else if (*fmt == 'b' && !opt)
        {
          cstr = va_arg(ap, svn_boolean_t) ? "true" : "false";
          svn_client_write_word(targetstr, cstr);
        }
      else if (*fmt == '?')
        opt = TRUE;
      else if (*fmt == '(' && !opt)
        svn_client_start_list(targetstr);
      else if (*fmt == ')')
        {
          svn_client_end_list(targetstr);
          opt = FALSE;
        }
      else if (*fmt == '!' && !*(fmt + 1))
        return;
      else
        abort();
    }
  svn_client_end_list(targetstr);
}

void
svn_client_write_tuple(svn_stringbuf_t *targetstr,
                       const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vwrite_tuple(targetstr, fmt, ap);
  va_end(ap);
}

void
svn_client_write_cmd(svn_stringbuf_t *targetstr,
                     const char *cmdname,
                     const char *fmt, ...)
{
  va_list ap;

  svn_client_start_list(targetstr);
  svn_client_write_word(targetstr, cmdname);
  va_start(ap, fmt);
  vwrite_tuple(targetstr, fmt, ap);
  va_end(ap);
  svn_client_end_list(targetstr);
}
