/*
 * keysort.c:   convert a hash into a sorted array
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



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <string.h>       /* for strncmp() */
#include <stdlib.h>       /* for qsort()   */
#include "svn_hash.h"



/*** apr_get_sorted_keys() ***/

/* (Should this be a permanent part of APR?)

   OK, folks, here's what's going on.  APR hash tables hash on
   key/klen objects, and store associated generic values.  They work
   great, but they have no ordering.

   The point of this exercise is to somehow arrange a hash's keys into
   an "ordered list" of some kind -- in this case, a nicely sorted
   one.

   We're using APR arrays, therefore, because that's what they are:
   ordered lists.  However, what "keys" should we put in the array?
   Clearly, (const char *) objects aren't general enough.  Or rather,
   they're not as general as APR's hash implementation, which stores
   (void *)/length as keys.  We don't want to lose this information.

   Therefore, it makes sense to store pointers to {void *, size_t}
   structures in our array.  No such apr object
   exists... BUT... there's a type in apr_tables.h called `apr_item_t'
   which contains {char *, size_t, void *}.  If store these objects in
   our array (rather than define our own new type), then we get the
   hash value *for free*.  When looping over the final array, we don't
   need to call apr_hash_get().  Major bonus!

 */


/* Use strncmp() as the qsort comparison function, because this is the
   same level of detail that apr_hash_t uses for storing keys.  */
static int
counted_length_compare (const void *obj1, const void *obj2)
{
  apr_item_t *item1, *item2;
  int retval;
  size_t smaller_size;

  item1 =  *((apr_item_t **) obj1);
  item2 =  *((apr_item_t **) obj2);

  smaller_size = ((item1->size) < (item2->size)) ? item1->size : item2->size;
  retval =  strncmp (item1->key, item2->key, smaller_size);

  printf ("%d:  `%s'  `%s'\n", retval, item1->key, item2->key);

  return retval;
}


/* Grab the keys (and values) in apr_hash HT and return them in an a
   sorted apr_array_header_t ARRAY allocated from POOL.  The array
   will contain pointers of type (apr_item_t *).  */
apr_array_header_t *
apr_get_sorted_keys (apr_hash_t *ht, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary;

  /* allocate an array with only one element to begin with. */
  ary = apr_make_array (pool, 1, sizeof(apr_item_t *));

  /* loop over hash table and push all keys into the array */
  for (hi = apr_hash_first (ht); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t klen;
      void *value;
      apr_item_t **receiver;
      apr_item_t *item = apr_pcalloc (pool, sizeof(*item));

      apr_hash_this (hi, &key, &klen, &value);
      item->key = (char *) key;
      item->size = klen;
      item->data = value;
      
      receiver = (apr_item_t **)apr_push_array (ary);
      *receiver = item;
    }
  
  /* now quicksort the array.  */
  qsort (ary->elts, ary->nelts, ary->elt_size, counted_length_compare);

  return ary;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
