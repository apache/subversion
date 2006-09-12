/*
 * xdelta.c:  xdelta generator.
 *
 * ====================================================================
 * Copyright (c) 2000-2006 CollabNet.  All rights reserved.
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
};

/* Feed C into the adler32 checksum.  */

static APR_INLINE void
adler32_in(struct adler32 *ad, const char c)
{
  ad->s1 += ((apr_uint32_t) (c)) & ADLER32_CHAR_MASK;
  ad->s1 &= ADLER32_MASK;
  ad->s2 += ad->s1;
  ad->s2 &= ADLER32_MASK;
  ad->len++;
}

/* Remove the result of byte C from the adler32 checksum.  */

static APR_INLINE void
adler32_out(struct adler32 *ad, const char c)
{
  ad->s1 -= ((apr_uint32_t) (c)) & ADLER32_CHAR_MASK;
  ad->s1 &= ADLER32_MASK;
  ad->s2 -= (ad->len * (((apr_uint32_t) c) & ADLER32_CHAR_MASK)) + 1;
  ad->s2 &= ADLER32_MASK;
  --ad->len;
}

/* Return the current adler32 checksum in the adler32 structure.  */

static APR_INLINE apr_uint32_t
adler32_sum(const struct adler32 *ad)
{
  return (ad->s2 << 16) | (ad->s1);
}

/* Initialize an adler32 checksum structure with DATA, which has length
   DATALEN.  Return the initialized structure.  */

static APR_INLINE struct adler32 *
init_adler32(struct adler32 *ad, const char *data, apr_uint32_t datalen)
{
  ad->s1 = 1;
  ad->s2 = 0;
  ad->len = 0;
  while (datalen--)
    adler32_in(ad, *(data++));
  return ad;
}

/* Size of the blocks we compute checksums for. This was chosen out of
   thin air.  Monotone used 64, xdelta1 used 64, rsync uses 128.  */
#define MATCH_BLOCKSIZE 64

/* Information for a block of the delta source.  The length of the
   block is the smaller of MATCH_BLOCKSIZE and the difference between
   the size of the source data and the position of this block. */
struct block
{
  apr_uint32_t adlersum;
  apr_size_t pos;
};

/* A hash table, using open addressing, of the blocks of the source. */
struct blocks
{
  /* The largest valid index of slots. */
  apr_size_t max;
  /* The vector of blocks.  A pos value of (apr_size_t)-1 represents an unused
     slot. */
  struct block *slots;
};


/* Return a hash value calculated from the adler32 SUM, suitable for use with
   our hash table. */
static apr_size_t hash_func(apr_uint32_t sum)
{
  /* Since the adl32 checksum have a bad distribution for the 11th to 16th
     bits when used for our small block size, we add some bits from the
     other half of the checksum. */
  return sum ^ (sum >> 12);
}

/* Insert a block with the checksum ADLERSUM at position POS in the source data
   into the table BLOCKS.  Ignore duplicates. */
static void
add_block(struct blocks *blocks, apr_uint32_t adlersum, apr_size_t pos)
{
  apr_size_t h = hash_func(adlersum) & blocks->max;

  /* This will terminate, since we know that we will not fill the table. */
  while (blocks->slots[h].pos != (apr_size_t)-1)
    {
      /* No duplicates! */
      if (blocks->slots[h].adlersum == adlersum)
        return;
      h = (h + 1) & blocks->max;
    }
  blocks->slots[h].adlersum = adlersum;
  blocks->slots[h].pos = pos;
}

/* Find a block in BLOCKS with the checksum ADLERSUM, returning its position
   in the source data.  If there is no such block, return (apr_size_t)-1. */
static apr_size_t
find_block(const struct blocks *blocks, apr_uint32_t adlersum)
{
  apr_size_t h = hash_func(adlersum) & blocks->max;

  while (blocks->slots[h].adlersum != adlersum
         && blocks->slots[h].pos != (apr_size_t)-1)
    h = (h + 1) & blocks->max;

  return blocks->slots[h].pos;
}
      
/* Initialize the matches table from DATA of size DATALEN.  This goes
   through every block of MATCH_BLOCKSIZE bytes in the source and
   checksums it, inserting the result into the BLOCKS table.  */
static void
init_blocks_table(const char *data,
                   apr_size_t datalen,
                  struct blocks *blocks,
                   apr_pool_t *pool)
{
  apr_size_t i;
  struct adler32 adler;
  apr_size_t nblocks;
  apr_size_t nslots = 1;

  /* Be pesimistic about the block count. */
  nblocks = datalen / MATCH_BLOCKSIZE + 1;
  /* Find nearest larger power of two. */
  while (nslots <= nblocks)
    nslots *= 2;
  /* Double the number of slots to avoid a too high load. */
  nslots *= 2;
  blocks->max = nslots - 1;
  blocks->slots = apr_palloc(pool, nslots * sizeof(*(blocks->slots)));
  for (i = 0; i < nslots; ++i)
    {
      /* Avoid using an indeterminate value in the lookup. */
      blocks->slots[i].adlersum = 0;
      blocks->slots[i].pos = (apr_size_t)-1;
    }

  for (i = 0; i < datalen; i += MATCH_BLOCKSIZE)
    {
      /* If this is the last piece, it may be blocksize large */
      apr_uint32_t step =
        ((i + MATCH_BLOCKSIZE) >= datalen) ? (datalen - i) : MATCH_BLOCKSIZE;
      apr_uint32_t adlersum =
        adler32_sum(init_adler32(&adler, data + i, step));
      add_block(blocks, adlersum, i);
    }
}

/* Try to find a match for the target data B in BLOCKS, and then
   extend the match as long as data in A and B at the match position
   continues to match.  We set the position in a we ended up in (in
   case we extended it backwards) in APOSP, the length of the match in
   ALENP, and the amount to advance B in BADVANCEP.
   *PENDING_INSERT_LENP is the length of the last insert operation that
   has not been committed yet to the delta stream, or 0 if there is no
   pending insert.  This is used when extending the match backwards,
   in which case *PENDING_INSERT_LENP is adjusted, possibly
   alleviating the need for the insert entirely.  Return TRUE if the
   lookup found a match, regardless of length.  Return FALSE
   otherwise.  */
static svn_boolean_t
find_match(const struct blocks *blocks,
           const struct adler32 *rolling,
           const char *a,
           apr_size_t asize,
           const char *b,
           apr_size_t bsize,
           apr_size_t bpos,
           apr_size_t *aposp,
           apr_size_t *alenp,
           apr_size_t *badvancep,
           apr_size_t *pending_insert_lenp)
{
  apr_uint32_t sum = adler32_sum(rolling);
  apr_size_t alen, badvance, apos;
  apr_size_t tpos, tlen;

  tpos = find_block(blocks, sum);

  /* See if we have a match.  */
  if (tpos == (apr_size_t)-1)
    return FALSE;

  tlen = ((tpos + MATCH_BLOCKSIZE) >= asize)
    ? (asize - tpos) : MATCH_BLOCKSIZE;

  /* Make sure it's not a false match.  */
  if (memcmp(a + tpos, b + bpos, tlen) != 0)
    return FALSE;

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
  while (apos > 0
         && bpos > 0
         && a[apos - 1] == b[bpos - 1]
         && *pending_insert_lenp > 0)
    {
      --(*pending_insert_lenp);
      --apos;
      --bpos;
      ++alen;
    }

  *aposp = apos;
  *alenp = alen;
  *badvancep = badvance;
  return TRUE;
}


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
compute_delta(svn_txdelta__ops_baton_t *build_baton,
              const char *a,
              apr_uint32_t asize,
              const char *b,
              apr_uint32_t bsize,
              apr_pool_t *pool)
{
  struct blocks blocks;
  struct adler32 rolling;
  apr_size_t sz, lo, hi, pending_insert_start = 0, pending_insert_len = 0;

  /* If the size of the target is smaller than the match blocksize, just
     insert the entire target.  */
  if (bsize < MATCH_BLOCKSIZE)
    {
      svn_txdelta__insert_op(build_baton, svn_txdelta_new,
                             0, bsize, b, pool);
      return;
    }

  /* Initialize the matches table.  */
  init_blocks_table(a, asize, &blocks, pool);

  /* Initialize our rolling checksum.  */
  init_adler32(&rolling, b, MATCH_BLOCKSIZE);
  for (sz = bsize, lo = 0, hi = MATCH_BLOCKSIZE; lo < sz;)
    {
      apr_size_t apos = 0;
      apr_size_t alen = 1;
      apr_size_t badvance = 1;
      apr_size_t next;
      svn_boolean_t match;

      match = find_match(&blocks, &rolling, a, asize, b, bsize, lo, &apos, 
                         &alen, &badvance, &pending_insert_len);

      /* If we didn't find a real match, insert the byte at the target
         position into the pending insert.  */
      if (! match)
        ++pending_insert_len;
      else
        {
          if (pending_insert_len > 0)
            {
              svn_txdelta__insert_op(build_baton, svn_txdelta_new,
                                     0, pending_insert_len,
                                     b + pending_insert_start, pool);
              pending_insert_len = 0;
            }
          /* Reset the pending insert start to immediately after the
             match. */
          pending_insert_start = lo + badvance;
          svn_txdelta__insert_op(build_baton, svn_txdelta_source,
                                 apos, alen, NULL, pool);
        }
      next = lo;
      for (; next < lo + badvance; ++next)
        {
          adler32_out(&rolling, b[next]);
          if (next + MATCH_BLOCKSIZE < bsize)
            adler32_in(&rolling, b[next + MATCH_BLOCKSIZE]);
        }
      lo = next;
      hi = lo + MATCH_BLOCKSIZE;
    }

  /* If we still have an insert pending at the end, throw it in.  */
  if (pending_insert_len > 0)
    {
      svn_txdelta__insert_op(build_baton, svn_txdelta_new,
                             0, pending_insert_len,
                             b + pending_insert_start, pool);
    }
}

void
svn_txdelta__xdelta(svn_txdelta__ops_baton_t *build_baton,
                    const char *data,
                    apr_size_t source_len,
                    apr_size_t target_len,
                    apr_pool_t *pool)
{
  /*  We should never be asked to compute something when the source_len is 0,
      because it should have asked vdelta or some other compressor.  */
  assert(source_len != 0);
  compute_delta(build_baton, data, source_len,
                data + source_len, target_len,
                pool);
}
