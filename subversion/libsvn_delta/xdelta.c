/*
 * xdelta.c:  xdelta generator.
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


#include <assert.h>

#include <apr_general.h>        /* for APR_INLINE */
#include <apr_hash.h>

#include "svn_delta.h"
#include "delta.h"

/* This is pseudo-adler32. It is adler32 without the prime modulus.
   The idea is borrowed from monotone, and is a translation of the C++
   code.  Graydon Hoare, the author of the original code, gave his
   explicit permission to use it under these terms at 8:02pm on
   Friday, February 11th, 2005.  */

#define ADLER32_MASK      0x0000ffff
#define ADLER32_CHAR_MASK 0x000000ff

/* Structure to store the state of our adler32 checksum.  */
struct adler32
{
  apr_uint32_t s1;
  apr_uint32_t s2;
  apr_uint32_t len;
  apr_uint32_t mask;
};

/* Feed C into the adler32 checksum.  */

static APR_INLINE void
adler32_in (struct adler32 *ad, const char c)
{
  ad->s1 += ((apr_uint32_t) (c)) & ADLER32_CHAR_MASK;
  ad->s1 &= ADLER32_MASK;
  ad->s2 += ad->s1;
  ad->s2 &= ADLER32_MASK;
  ad->len++;
}

/* Remove the result of byte C from the adler32 checksum.  */

static APR_INLINE void
adler32_out (struct adler32 *ad, const char c)
{
  ad->s1 -= ((apr_uint32_t) (c)) & ADLER32_CHAR_MASK;
  ad->s1 &= ADLER32_MASK;
  ad->s2 -= (ad->len * (((apr_uint32_t) c) & ADLER32_CHAR_MASK)) + 1;
  ad->s2 &= ADLER32_MASK;
  --ad->len;
}

/* Return the current adler32 checksum in the adler32 structure.  */

static APR_INLINE apr_uint32_t
adler32_sum (const struct adler32 *ad)
{
  return (ad->s2 << 16) | (ad->s1);
}

/* Initialize an adler32 checksum structure with DATA, which has length
   DATALEN.  Return the initialized structure.  */

static APR_INLINE struct adler32 *
init_adler32 (struct adler32 *ad, const char *data, apr_uint32_t datalen)
{
  ad->s1 = 1;
  ad->s2 = 0;
  ad->mask = ADLER32_MASK;
  ad->len = 0;
  while (datalen--)
    adler32_in (ad, *(data++));
  return ad;
}

/* Match structure used in the xdelta matches hashtable.  This contains the
   position and length of the initial match.  */

struct match
{
  apr_uint32_t pos;
  apr_uint32_t len;
};

/* Initialize the matches table from DATA.  This goes through every
   block of BLOCKSIZE bytes in the source and checksums it, inserting the
   result into the MATCHES table.  */

static void
init_matches_table (const char *data,
                    apr_uint32_t datalen,
                    apr_uint32_t blocksize,
                    apr_hash_t *matches,
                    apr_pool_t *pool)
{
  apr_uint32_t i = 0;
  struct adler32 adler;
  for (i = 0; i < datalen; i += blocksize)
    {
      /* If this is the last piece, it may be blocksize large */
      apr_uint32_t step =
        ((i + blocksize) >= datalen) ? (datalen - i) : blocksize;
      apr_uint32_t adlersum =
        adler32_sum (init_adler32 (&adler, data + i, step));
      if (apr_hash_get (matches, &adlersum, sizeof (adlersum)) == NULL)
        {
          struct match *newmatch = apr_palloc (pool, sizeof *newmatch);
          apr_uint32_t *key = apr_palloc (pool, sizeof *key);
          newmatch->pos = i;
          newmatch->len = step;
          *key = adlersum;
          apr_hash_set (matches, key, sizeof (*key), newmatch);
        }
    }
}

/* Try to find a match for the target data B in MATCHES, and then extend the
   match as long as data in A and B at the match position continues to match.
   We set the position in a we ended up in (in case we extended it backwards)
   in APOSP, the length of the match in ALENP, and the amount to advance B in
   BADVANCEP.  PENDING_INSERT is a pointer to a stringbuf pointer that is the
   last insert operation that has not been committed yet to the delta stream,
   if any.  This is used when extending the matches backwards, possibly
   alleviating the need for the insert entirely.  */

static void
find_match (apr_hash_t *matches,
            const struct adler32 *rolling,
            const char *a,
            apr_uint32_t asize,
            const char *b,
            apr_uint32_t bsize,
            apr_uint32_t bpos,
            apr_uint32_t *aposp,
            apr_uint32_t *alenp,
            apr_uint32_t *badvancep,
            svn_stringbuf_t **pending_insert)
{
  apr_uint32_t sum = adler32_sum (rolling);
  apr_uint32_t alen, badvance, apos;
  struct match *match;
  apr_uint32_t tpos, tlen;

  /* See if we have a match.  */
  match = apr_hash_get (matches, &sum, sizeof (sum));
  if (match == NULL)
    return;

  /* See where our match started.  */
  tpos = match->pos;
  tlen = match->len;

  /* Make sure it's not a false match.  */
  if (memcmp (a + tpos, b + bpos, tlen) != 0)
    return;

  apos = tpos;
  alen = tlen;
  badvance = tlen;

  /* Extend the match forward as far as possible */
  while ((apos + alen < asize)
         && (bpos + badvance < bsize)
         && (a[apos + alen] == b[bpos + badvance]))
    {
      ++alen;
      ++badvance;
    }

  /* See if we can extend backwards into a previous insert hunk.  */
  if (*pending_insert)
    {
      while (apos > 0
             && bpos > 0
             && a[apos - 1] == b[bpos - 1]
             && (*pending_insert)->len != 0)
        {
          svn_stringbuf_chop (*pending_insert, 1);
          --apos;
          --bpos;
          ++alen;
        }
      /* If we completely consumed the entire insert, delete it.  */
      if ((*pending_insert)->len == 0)
        *pending_insert = NULL;
    }

  *aposp = apos;
  *alenp = alen;
  *badvancep = badvance;
}

/* Size of the blocks we compute checksums for. This was chosen out of
   thin air.  Monotone used 64, xdelta1 used 64, rsync uses 128.  */
#define MATCH_BLOCKSIZE 64


/* Compute a delta from A to B using xdelta.

   The basic xdelta algorithm is as follows:

   1. Go through the source data, checksumming every MATCH_BLOCKSIZE
      block of bytes using adler32, and inserting the checksum into a
      match table with the position of the match.
   2. Go through the target byte by byte, seeing if that byte starts a
      match that we have in the match table.
      2a. If so, try to extend the match as far as possible both
          forwards and backwards, and then insert a source copy
          operation into the delta ops builder for the match.
      2b. If not, insert the byte as new data using an insert delta op.

   Our implementation doesn't immediately insert "insert" operations,
   it waits until we have another copy, or we are done.  The reasoning
   is twofold:

   1. Otherwise, we would just be building a ton of 1 byte insert
      operations
   2. So that we can extend a source match backwards into a pending
     insert operation, and possibly remove the need for the insert
     entirely.  This can happen due to stream alignment.
*/
static void
compute_delta (svn_txdelta__ops_baton_t *build_baton,
               const char *a,
               apr_uint32_t asize,
               const char *b,
               apr_uint32_t bsize,
               apr_pool_t *pool)
{
  apr_hash_t *matches = apr_hash_make(pool);
  struct adler32 rolling;
  apr_uint32_t sz, lo, hi;
  svn_stringbuf_t *pending_insert = NULL;

  /* Initialize the matches table.  */
  init_matches_table (a, asize, MATCH_BLOCKSIZE, matches, pool);

  /* If the size of the target is smaller than the match blocksize, just
     insert the entire target.  */
  if (bsize < MATCH_BLOCKSIZE)
    {
      svn_txdelta__insert_op (build_baton, svn_txdelta_new,
                              0, bsize, b, pool);
      return;
    }

  /* Initialize our rolling checksum.  */
  init_adler32 (&rolling, b, MATCH_BLOCKSIZE);
  for (sz = bsize, lo = 0, hi = MATCH_BLOCKSIZE; lo < sz;)
    {
      apr_uint32_t apos = 0;
      apr_uint32_t alen = 1;
      apr_uint32_t badvance = 1;
      apr_uint32_t next;

      find_match (matches, &rolling, a, asize, b, bsize, lo, &apos, &alen,
                  &badvance, &pending_insert);

      /* If we didn't find a real match, insert the byte at the target
         position into the pending insert.  */
      if (alen < MATCH_BLOCKSIZE &&
          (apos + alen < asize))
        {
          if (pending_insert != NULL)
            svn_stringbuf_appendbytes (pending_insert, b + lo, 1);
          else
            pending_insert = svn_stringbuf_ncreate (b + lo, 1, pool);
        }
      else
        {
          if (pending_insert)
            {
              svn_txdelta__insert_op (build_baton, svn_txdelta_new,
                                      0, pending_insert->len,
                                      pending_insert->data, pool);
              pending_insert = NULL;
            }
          svn_txdelta__insert_op (build_baton, svn_txdelta_source,
                                  apos, alen, NULL, pool);
        }
      next = lo;
      for (; next < lo + badvance; ++next)
        {
          adler32_out (&rolling, b[next]);
          if (next + MATCH_BLOCKSIZE < bsize)
            adler32_in (&rolling, b[next + MATCH_BLOCKSIZE]);
        }
      lo = next;
      hi = lo + MATCH_BLOCKSIZE;
    }

  /* If we still have an insert pending at the end, throw it in.  */
  if (pending_insert)
    {
      svn_txdelta__insert_op (build_baton, svn_txdelta_new,
                              0, pending_insert->len,
                              pending_insert->data, pool);
      pending_insert = NULL;
    }
}

void
svn_txdelta__xdelta (svn_txdelta__ops_baton_t *build_baton,
                     const char *data,
                     apr_size_t source_len,
                     apr_size_t target_len,
                     apr_pool_t *pool)
{
  /*  We should never be asked to compute something when the source_len is 0,
      because it should have asked vdelta or some other compressor.  */
  assert (source_len != 0);
  compute_delta (build_baton, data, source_len,
                 data + source_len, target_len,
                 pool);
}
