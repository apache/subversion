/* id.c : operations on node and node revision ID's
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include <string.h>
#include <stdlib.h>

#include "svn_fs.h"
#include "id.h"
#include "key-gen.h"
#include "validate.h"


/* Copying ID's.  */

svn_fs_id_t *
svn_fs__id_copy (const svn_fs_id_t *id, apr_pool_t *pool)
{
  svn_fs_id_t *new_id = apr_pmemdup (pool, id, sizeof (*id));
}



/* Comparing node ID's.  */

int
svn_fs__id_eq (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  if (a == b)
    return 1;
  return (memcmp (a, b, sizeof (*a)) ? 0 : 1);
}


int
svn_fs_check_related (const svn_fs_id_t *id1, const svn_fs_id_t *id2)
{
  return (id1->node_id == id2->node_id);
}


int
svn_fs__id_is_ancestor (const svn_fs_id_t *a, const svn_fs_id_t *b)
{
  int i = 0;

  for (;;)
    {
      /* Invariant: i is even, and for all j < i, a[j] == b[j].
         Keep in mind: every even-numbered entry in A or B is a
         node/branch number; every odd-numbered entry is a revision
         number.  So i is pointing at a node/branch number.  */

      /* If we've reached the end of A, then either A and B are equal,
         or A is a prefix of B, so A is an ancestor of B.  Examples:
         100.20 vs. 100.20; 100.20 vs. 100.20.3.2  */
      if (a->digits[i] == -1)
        return 1;

      /* If the node/branch numbers differ, then they're unrelated.
         Examples: 100.20 vs. 101.20; 100.20.2.3 vs. 100.20.3.1;

         This also catches the case where B has ended before A ---
         they're related, but A isn't B's ancestor.
         Example: 100.20.3.2 vs 100.20.  */
      if (a->digits[i] != b->digits[i])
        return 0;

      /* If A's revision number is greater than B's, then A is not B's
         ancestor.  Examples: 100.30 vs 100.20; 2.3.4.5 vs 2.3.4.4.  */
      if (a->digits[i+1] > b->digits[i+1])
        return 0;

      /* If A's revision number is less than B's, then A is an ancestor
         iff its ID ends now.  Examples: 100.30 vs 100.31; 100.30 vs
         100.32.2.4.  */
      if (a->digits[i+1] < b->digits[i+1])
        return (a->digits[i+2] == -1);

      /* Otherwise, we've established that the node/branch numbers and
         revision numbers are equal, so go around again.  */
      i += 2;
    }
}



int
svn_fs__id_is_parent (const svn_fs_id_t *parent,
                      const svn_fs_id_t *child)
{
  int i;

  for (i = 0; parent->digits[i] == child->digits[i]; i++)
    {
      /* If they're completely identical, then CHILD isn't a direct
         child of PARENT.  */
      if (parent->digits[i] == -1)
        return 0;
    }

  /* Is CHILD the next revision of PARENT?  */
  if ((i & 1) == 1
      && child->digits[i] == parent->digits[i] + 1
      && child->digits[i + 1] == -1
      && parent->digits[i + 1] == -1)
    return 1;

  /* Is CHILD the first revision of any branch from PARENT?  */
  if ((i & 1) == 0
      && parent->digits[i] == -1
      && child->digits[i + 1] != -1
      && child->digits[i + 2] == 1
      && child->digits[i + 3] == -1)
    return 1;

  /* Anything else is no good.  */
  return 0;
}



/* Parsing and unparsing node ID's.  */

svn_fs_id_t *
svn_fs_parse_id (const char *data,
                 apr_size_t data_len,
                 apr_pool_t *pool)
{
  svn_fs_id_t *id;
  int id_len;
  
  /* Count the number of components in the ID, and check its syntax.  */
  id_len = svn_fs__count_id_components (data, data_len);
  if (id_len == 0)
      return 0;

  /* Allocate the ID array.  Note that if pool is zero, apr_palloc
     just calls malloc, which meets our promised interface.  */
  if (pool)
    {
      id = apr_palloc (pool, sizeof (*id));
      id->digits = apr_palloc (pool, sizeof (*(id->digits)) * (id_len + 1));
    }
  else
    {
      id = malloc (sizeof (*id));
      if (! id)
        abort(); /* couldn't malloc */
      id->digits = malloc (sizeof (*(id->digits)) * (id_len + 1));
      if (! (id->digits))
        abort(); /* couldn't malloc */
    }

  {
    int i = 0;
    const char *end = data + data_len;

    for (;;)
      {
        const char *next;
        id->digits[i++] = svn_fs__getsize (data, end - data, &next, 100000000);
        if (next == end)
          break;
        if (! next
            || *next != '.')
          {
            if (! pool) free (id);
            return 0;
          }

        data = next + 1;
      }

    id->digits[i] = -1;
  }

  return id;
}


svn_stringbuf_t *
svn_fs_unparse_id (const svn_fs_id_t *id,
                   apr_pool_t *pool)
{
  svn_stringbuf_t *unparsed = svn_stringbuf_ncreate (0, 0, pool);
  int i;

  for (i = 0; id->digits[i] != -1; i++)
    {
      char buf[200];
      int len = svn_fs__putsize (buf, sizeof (buf), id->digits[i]);

      if (len == 0)
        abort ();

      if (id->digits[i + 1] != -1)
        buf[len++] = '.';

      svn_stringbuf_appendbytes (unparsed, buf, len);
    }

  return unparsed;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
