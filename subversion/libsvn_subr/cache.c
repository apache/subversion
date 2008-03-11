/*
 * cache.c: in-memory caching for Subversion
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

#include <assert.h>

#include "svn_cache.h"

/* The cache object. */
struct svn_cache_t {
  /* Maps from a key (of size CACHE->KLEN) to a struct cache_entry. */
  apr_hash_t *hash;
  apr_ssize_t klen;

  /* Used to copy values in and out of the cache. */
  svn_cache_dup_func_t *dup_func;

  /* The number of pages we're allowed to allocate before having to
   * try to reuse one. */
  apr_int64_t unallocated_pages;
  /* Number of cache entries stored on each page.  Must be at least 1. */
  apr_int64_t items_per_page;

  /* A dummy cache_page serving as the head of a circular doubly
   * linked list of cache_pages.  SENTINEL->NEXT is the most recently
   * used page, and SENTINEL->PREV is the least recently used page.
   * All pages in this list are "full"; the page currently being
   * filled (PARTIAL_PAGE) is not in the list. */
  struct cache_page *sentinel;

  /* A page currently being filled with entries, or NULL if there's no
   * partially-filled page.  This page is not in SENTINEL's list. */
  struct cache_page *partial_page;
  /* If PARTIAL_PAGE is not null, this is the number of entries
   * currently on PARTIAL_PAGE. */
  apr_int64_t partial_page_number_filled;

  /* The pool that the svn_cache_t itself, HASH, and SENTINEL are
   * allocated in; subpools of this pool are used for the cache_page
   * and cache_entry structs, as well as the dup'd values and hash
   * keys.
   */
  apr_pool_t *cache_pool;
};

/* A cache page; all items on the page are allocated from the same
 * pool. */
struct cache_page {
  /* Pointers for the LRU list anchored at the cache's SENTINEL.
   * (NULL for the PARTIAL_PAGE.) */
  struct cache_page *prev;
  struct cache_page *next;

  /* The pool in which cache_entry structs, hash keys, and dup'd
   * values are allocated. */
  apr_pool_t *page_pool;

  /* A singly linked list of the entries on this page; used to remove
   * them from the cache's HASH before reusing the page. */
  struct cache_entry *first_entry;
};

/* An cache entry. */
struct cache_entry {
  const void *key;
  void *value;

  /* The page it's on (needed so that the LRU list can be
   * maintained). */
  struct cache_page *page;

  /* Next entry on the page. */
  struct cache_entry *next_entry;
};


svn_error_t *
svn_cache_create(svn_cache_t **cache_p,
                 svn_cache_dup_func_t *dup_func,
                 apr_ssize_t klen,
                 apr_int64_t pages,
                 apr_int64_t items_per_page,
                 svn_boolean_t thread_safe,
                 apr_pool_t *pool)
{
  svn_cache_t *cache = apr_pcalloc(pool, sizeof(*cache));

  cache->hash = apr_hash_make(pool);
  cache->klen = klen;

  cache->dup_func = dup_func;

  assert(pages >= 1);
  cache->unallocated_pages = pages;
  assert(items_per_page >= 1);
  cache->items_per_page = items_per_page;

  cache->sentinel = apr_pcalloc(pool, sizeof(*(cache->sentinel)));
  cache->sentinel->prev = cache->sentinel;
  cache->sentinel->next = cache->sentinel;
  /* The sentinel doesn't need a pool.  (We're happy to crash if we
   * accidentally try to treat it like a real page.) */

  /* ### TODO: mutex */

  cache->cache_pool = pool;
  return SVN_NO_ERROR;
}

/* Removes PAGE from the doubly-linked list it is in (leaving its PREV
 * and NEXT fields undefined). */
static void
remove_page_from_list(struct cache_page *page)
{
  page->prev->next = page->next;
  page->next->prev = page->prev;
}

/* Inserts PAGE immediately behind PRED. */
static void
insert_page(struct cache_page *page,
            struct cache_page *pred)
{
  page->prev = pred;
  page->next = pred->next;
  page->prev->next = page;
  page->next->prev = page;
}

/* If PAGE is in the circularly linked list (eg, its NEXT isn't NULL),
 * move it to the front of the list. */
static void
move_page_to_front(svn_cache_t *cache,
                   struct cache_page *page)
{
  assert(page != cache->sentinel);

  if (! page->next)
    return;

  remove_page_from_list(page);
  insert_page(page, cache->sentinel);
}

/* Uses CACHE->dup_func to copy VALUE into *VALUE_P inside POOL, or
   just sets *VALUE_P to NULL if VALUE is NULL. */
static svn_error_t *
duplicate(void **value_p,
          svn_cache_t *cache,
          void *value,
          apr_pool_t *pool)
{
  if (value)
    SVN_ERR((cache->dup_func)(value_p, value, pool));
  else
    *value_p = NULL;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cache_get(void **value_p,
              svn_boolean_t *found,
              svn_cache_t *cache,
              const void *key,
              apr_pool_t *pool)
{
  /* ### TODO: mutex */

  void *entry_void = apr_hash_get(cache->hash, key, cache->klen);
  struct cache_entry *entry;

  if (! entry_void)
    {
      *found = FALSE;
      return SVN_NO_ERROR;
    }

  entry = entry_void;

  move_page_to_front(cache, entry->page);

  SVN_ERR(duplicate(value_p, cache, entry->value, pool));
  *found = TRUE;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_cache_set(svn_cache_t *cache,
              const void *key,
              void *value,
              apr_pool_t *pool)
{
  /* ### TODO: implement */
  return SVN_NO_ERROR;
}
