/* proplist.c : property list operations
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
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
 * individuals on behalf of Collab.Net.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_string.h"
#include "svn_fs.h"
#include "fs.h"
#include "skel.h"
#include "proplist.h"



/* Building property list objects.  */


svn_fs_proplist_t *
svn_fs__make_proplist (skel_t *proplist, apr_pool_t *pool)
{
  svn_fs_proplist_t *p;
  skel_t *elt;

  if (proplist->is_atom)
    return 0;

  p = NEW (pool, svn_fs_proplist_t);
  memset (p, 0, sizeof (*p));
  p->hash = apr_make_hash (pool);
  p->pool = pool;

  /* Walk the property list two elements at a time.  */
  for (elt = proplist->children; elt; elt = elt->next->next)
    {
      skel_t *name = elt;
      skel_t *value = elt->next;
      char *name_copy;
      svn_string_t *value_copy;

      /* A correctly formed proplist is a series of pairs of atoms.  */
      if (! name->is_atom
	  || ! value
	  || ! value->is_atom)
	return 0;

      /* Make a copy of the name and value in this pool, and store
         them in the hash table, using the name as the key.  */
      name_copy = NEWARRAY (pool, char, name->len);
      memcpy (name_copy, name->data, name->len);
      value_copy = svn_string_ncreate (value->data, value->len, pool);
      apr_hash_set (p->hash, name_copy, name->len, value_copy);
    }

  return p;
}



/* The public property list access functions.  */

svn_error_t *
svn_fs_proplist_get (svn_string_t **value_p,
		     svn_fs_proplist_t *proplist,
		     svn_string_t *name,
		     apr_pool_t *pool)
{
  /* Since Subversion strings are null-terminated, this is correct
     even when name->len is zero.  */
  svn_string_t *value = apr_hash_get (proplist->hash, name->data, name->len);

  if (value)
    {
      if (! pool)
	pool = proplist->pool;

      *value_p = svn_string_dup (value, pool);
    }
  else
    *value_p = 0;

  return 0;
}


static int
name_list_compare (const void *a, const void *b)
{
  return svn_fs_compare_prop_names (* (svn_string_t **) a,
				    * (svn_string_t **) b);
}


svn_error_t *
svn_fs_proplist_names (svn_string_t ***names_p,
		       svn_fs_proplist_t *proplist,
		       apr_pool_t *pool)
{
  svn_string_t **names;
  apr_hash_index_t *hi;
  int count, i;

  if (! pool)
    pool = proplist->pool;

  count = 0;
  for (hi = apr_hash_first (proplist->hash); hi; hi = apr_hash_next (hi))
    count++;

  names = NEWARRAY (pool, svn_string_t *, count + 1);

  i = 0;
  for (hi = apr_hash_first (proplist->hash); hi; hi = apr_hash_next (hi))
    {
      const void *name;
      size_t len;
      
      apr_hash_this (hi, &name, &len, 0);
      names[i++] = svn_string_ncreate (name, len, pool);
    }
  names[i++] = 0;

  /* Sort the list.  */
  qsort (names, count, sizeof (names[0]), name_list_compare);

  *names_p = names;

  return 0;
}


svn_error_t *
svn_fs_proplist_hash_table (apr_hash_t **table_p,
			    svn_fs_proplist_t *proplist,
			    apr_pool_t *pool)
{
  apr_hash_t *copy;
  apr_hash_index_t *hi;

  if (! pool)
    pool = proplist->pool;

  /* Make a copy of our hash table, so the caller can tweak it as they
     please.  */
  copy = apr_make_hash (pool);
  for (hi = apr_hash_first (proplist->hash); hi; hi = apr_hash_next (hi))
    {
      const void *name;
      size_t name_len;
      void *value;
      void *name_copy;
      svn_string_t *value_copy;

      apr_hash_this (hi, &name, &name_len, &value);

      /* Copy the name and the value.  */
      value_copy = svn_string_dup ((svn_string_t *) value, pool);
      name_copy = NEWARRAY (pool, char, name_len);
      memcpy (name_copy, name, name_len);

      /* Add them to the new hash table.  */
      apr_hash_set (copy, name_copy, name_len, value_copy);
    }

  *table_p = copy;

  return 0;
}


int
svn_fs_compare_prop_names (svn_string_t *a, svn_string_t *b)
{
  if (a == b)
    return 0;
  else if (! a)
    return 1;
  else if (! b)
    return -1;
  else
    {
      int cmp = memcmp (a->data, b->data, 
			a->len > b->len ? b->len : a->len);

      if (cmp)
	return cmp;
      else
	return a->len - b->len;
    }
}
