/* Hashing interface for a vdelta implementation.
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
 * software developed by CollabNet (http://www.CollabNet/)."
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
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



/* *********************** Data Structures ************************** */



#include <stdlib.h>
#include <stddef.h>



/* One entry in a hash table. */
typedef struct hash_entry_t
{
  /* Notice that this doesn't point to a chain of hash buckets.
   * That's right -- we clobber on collision.  It's a time-space
   * tradeoff, and optimizing for time is faster to implement.
   *
   * An in-between solution is to keep `pos1', `pos2' ... `posN',
   * hardcoded in the data type here, and try all of them for the
   * longest available match.  I think N == 4 would be good, on no
   * basis whatsoever.
   *
   * The best solution, for optimizing delta size, is to be a regular
   * hash table with an extendable bucket chain.  But vdelta might run
   * real slow that way. :-)
   */

  long int pos;        /* Where was this string in the input? */
} hash_entry_t;


/* A hash table is basically an array of hash_entries. */
typedef struct hash_table_t
{
  size_t size;
  hash_entry_t **table;
} hash_table_t;


/* *********************** Functions ************************** */

hash_table_t *make_hash_table (size_t size);

void free_hash_table (hash_table_t *table);

/* Return the position associated with the match, if any, else -1. 
   Put STR into the hash_table in any case. */
hash_entry_t *try_match (char *str, size_t len, size_t pos, hash_table_t *t);



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
