/*
 * vdelta.c:  vdelta generator.
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
 * software developed by CollabNet (http://www.Collab.Net)."
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
 */


#include "svn_delta.h"
#include "delta.h"


/* ==================================================================== */
/* Hash table for vdelta hashing.

   Each hash bucket is a chain of slots. The index of the slot in
   the slots array is also the index of the key string in the
   current window's data stream. The hash table implements a multimap
   (i.e., hash and key collisions are allowed).

   To store a key->index mapping, just add slot[index] to the slot
   chain tn key's bucket (see store_mapping).

   For a given key, you can traverse the list of match candidates (some
   of which may be hash collisions) like this:

   for (slot = buckets[get_bucket(key)]; slot != NULL; slot = slot->next)
     {
       index = slot - slots;
       ...
     }

  This hash table implementation os based on the description in
  http://subversion.tigris.org/subversion-dev/current/msg00152.html. */


/* Size of a vdelta hash key. */
#define VD_KEY_SIZE 4


/* The window size. */
apr_size_t svn_txdelta__window_size = 16384;

/* Hash slot. */
typedef struct hash_slot_t {
  struct hash_slot_t *next;
} hash_slot_t;

/* Hash table. */
typedef struct hash_table_t {
  int num_buckets;              /* Number of buckets in the table. */
  hash_slot_t **buckets;        /* Bucket array. */
  hash_slot_t *slots;           /* Slots array. */
} hash_table_t;


/* Create a hash table with NUM_SLOTS slots. NUM_SLOTS should be the sum
   of the size of the source and target parts of the delta window.
   Allocate from POOL. */
static hash_table_t *
create_hash_table (apr_size_t num_slots, apr_pool_t *pool)
{
  int i;
  hash_table_t* table = apr_palloc (pool, sizeof (*table));

  /* This should be a reasonable number of buckets ... */
  table->num_buckets = (num_slots / 3) | 1;
  table->buckets = apr_palloc (pool, (table->num_buckets
                                      * sizeof (*table->buckets)));
  for (i = 0; i < table->num_buckets; ++i)
    table->buckets[i] = NULL;

  table->slots = apr_palloc (pool, num_slots * sizeof (*table->slots));
  for (i = 0; i < num_slots; ++i)
    table->slots[i].next = NULL;

  return table;
}


/* Convert a key to a pointer to the key's hash bucket.
   We use a 2-universal multiplicative hash function. */
static APR_INLINE apr_uint32_t
get_bucket (const hash_table_t *table, const char *key)
{
  int i;
  apr_uint32_t hash = 0;
  for (i = 0; i < VD_KEY_SIZE; ++i)
    hash = hash * 97 + *key++ + 41;
  return hash % table->num_buckets;
}


/* Store a key->index mapping into the hash table. */
static APR_INLINE void
store_mapping (hash_table_t *table, const char* key, apr_off_t index)
{
  apr_uint32_t bucket = get_bucket (table, key);;
  table->slots[index].next = table->buckets[bucket];
  table->buckets[bucket] = &table->slots[index];
}



/* ==================================================================== */
/* Vdelta generator.

   The article "Delta Algorithms: An Empirical Analysis" by Hunt,
   Vo and Tichy contains a description of the vdelta algorithm,
   but it's incomplete. Here's a detailed description (see also
   http://subversion.tigris.org/subversion-dev/current/msg00158.html
   in the mailing list archives):
   
     1. Look up the four bytes starting at the current position
        pointer.  If there are no matches for those four bytes,
        output an insert, move the position pointer forward by one,
        and go back to step 1.

     2. Determine which of the candidates yields the longest
        extension.  This will be called the "current match".

     3. Look up the last three bytes of the current match plus one
        unmatched byte.  If there is no match for those four bytes,
        the current match is the best match; go to step 6.

     4. For each candidate, check backwards to see if it matches
        the entire match so far.  If no candidates satisfy that
        constraint, the current match is the best match; go to step 6.

     5. Among the candidates which do satisfy the constraint,
        determine which one yields the longest extension.  This
        will be the new "current match."  Go back to step 3.

     6. Output a block copy instruction, add indexes for the last
        three positions of the matched data, advance the position
        pointer by the length of the match, and go back to step 1.

   Inserts and copies are generated only when the current position
   is within the target data.

   Note that the vdelta algorithm allows copies that cross the
   source/target data boundary. Because our internal delta
   representation has different opcodes for source and terget copies,
   we split them in two. This means that the opcode stream in the
   delta window can contain copies shorter than VD_KEY_SIZE. These
   could be represented by insert ops instead, but we'll leave them
   in, so that we can merge them again when we convert the delta
   window to an external format like vcdiff that supports cross
   -boundary copies. */


/* Fing the end of a match within the data window.
   Note that (match < from && from <= end) must always be true here. */

static APR_INLINE const char *
find_match_end (const char *match, const char *from, const char *end)
{
  const char *here = from;
  while (here < end && *match == *here)
    {
      ++match;
      ++here;
    }
  return match;
}


/* This is the main vdelta generator. */

svn_error_t *
svn_txdelta__vdelta (svn_txdelta_window_t *window,
                     const char *const start,
                     apr_size_t source_len,
                     apr_size_t target_len,
                     apr_pool_t *pool)
{
  /* Mark the beginning of the target data and the end of the buffer. */
  const char *const target = start + source_len;
  const char *const end = target + target_len;

  const char *here = start;     /* Current position in the buffer. */
  const char *insert_from = NULL; /* Start of byte range for insertion. */

  hash_table_t *table = create_hash_table (end - start, pool);

  for (;;)
    {
      const char *current_match = NULL;
      const char *current_match_end = NULL;
      apr_size_t current_match_len = 0;
      hash_slot_t* slot;

      /* If we're near the end, just insert the last few bytes. */
      if (end - here < VD_KEY_SIZE)
        {
          const char *from = ((insert_from != NULL) ? insert_from : here);

          if (from < end)       /* Don't generate zero-length inserts. */
            return svn_txdelta__insert_op (window, svn_txdelta_new,
                                           0, end - from, from);
          else
            return SVN_NO_ERROR;
        }

      /* Find the longest match at this position, or insert this byte
         if there is no match here.  */
      for (slot = table->buckets[get_bucket (table, here)];
           slot != NULL;
           slot = slot->next)
        {
          const char *match = &start[slot - table->slots];
          const char *match_end = find_match_end (match, here, end);
          apr_size_t match_len = match_end - match;

          if (match_len >= VD_KEY_SIZE && match_len > current_match_len)
            {
              current_match = match;
              current_match_end = match_end;
              current_match_len = match_len;
            }
        }

      if (current_match == NULL)
        {
          store_mapping (table, here, here - start);
          if (here >= target && insert_from == NULL)
            insert_from = here;
          ++here;
          continue;
        }
      
      /* If the current match doesn't extend to the end of the data window,
         check for match candidates at the end of the current match to see
         if a longer metch is possible. */
      while (end > here + current_match_len)
        {
          const char *extension_end = here + current_match_len;
          const char *extension_key = extension_end - (VD_KEY_SIZE - 1);

          apr_size_t partial_match_len = current_match_len - (VD_KEY_SIZE - 1);

          const char *match = NULL;
          const char *match_end = NULL;

          for (slot = table->buckets[get_bucket (table, extension_key)];
               slot != NULL;
               slot = slot->next)
          {
            const char *candidate = &start[slot - table->slots];
            const char *candidate_end = NULL;

            /* Ignore this candidate if its too close to the start of the
               data window. */
            if ((candidate - start) < partial_match_len)
              continue;

            /* Also ignore any hash collisions. */
            if (0 != memcmp (candidate, extension_key, VD_KEY_SIZE))
              continue;

            /* And ignore it if it doesn't fit the current match. */
            if (0 != memcmp (candidate - partial_match_len,
                             current_match, partial_match_len))
              continue;

            /* Remember this candidate if it yeilds a longer extension
               than the one we have now. */
            candidate_end = find_match_end (candidate + VD_KEY_SIZE,
                                            extension_key + VD_KEY_SIZE,
                                            end);
            if (candidate_end - candidate > extension_end - extension_key)
              {
                match = candidate - partial_match_len;
                match_end = candidate_end;
              }
          }

          /* Adjust the current match if we found a better candidate. */
          if (match != NULL)
            {
              current_match = match;
              current_match_end = match_end;
              current_match_len = match_end - match;
            }
          else
            break;
        }

      /* We have a best match, Commit this copy. */
      if (here >= target)
        {
          svn_error_t *err = SVN_NO_ERROR;

          /* First, commit the pending insert. */
          if (insert_from != NULL)
            {
              err = svn_txdelta__insert_op (window, svn_txdelta_new,
                                            0, here - insert_from,
                                            insert_from);
              if (err != SVN_NO_ERROR)
                return err;
              insert_from = NULL;
            }

          /* If the source of the copy straddles the boundary, we need to
             generate two copies, because we can't represent a mixed copy
             in the window. */
          if (current_match < target && current_match_end > target)
            {
              err = svn_txdelta__insert_op (window, svn_txdelta_source,
                                            current_match - start,
                                            target - current_match,
                                            NULL);
              if (!err)
                err = svn_txdelta__insert_op (window, svn_txdelta_target,
                                              0 /* Yes, that's right! */,
                                              current_match_end - target,
                                              NULL);
            }
          else if (current_match_end <= target) /* Copy from source. */
            err = svn_txdelta__insert_op (window, svn_txdelta_source,
                                          current_match - start,
                                          current_match_len,
                                          NULL);
          else                  /* Copy from target */
            err = svn_txdelta__insert_op (window, svn_txdelta_target,
                                          current_match - target,
                                          current_match_len,
                                          NULL);

          if (err != SVN_NO_ERROR)
            return err;
        }

      /* Adjust the current position and insert mappings for the
         last three bytes of the match. */
      here += current_match_len;
      if (end - here >= VD_KEY_SIZE)
        {
          char const *last = here - (VD_KEY_SIZE - 1);
          for (; last < here; ++last)
            store_mapping (table, last, last - start);
        }
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

