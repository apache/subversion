/* id.c : operations on node and node version ID's
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

#include "svn_fs.h"
#include "id.h"
#include "fs.h"
#include "convert-size.h"


/* Finding the length of an ID.  */


int
svn_fs_id_length (svn_fs_id_t *id)
{
  int len;

  for (len = 0; id[len] != -1; len++)
    ;

  return len;
}


/* Comparing node ID's.  */

int
svn_fs_id_eq (svn_fs_id_t *a, svn_fs_id_t *b)
{
  int i;

  for (i = 0; a[i] == b[i]; i++)
    if (a[i] == -1)
      return 1;

  return 0;
}


int
svn_fs_id_is_ancestor (svn_fs_id_t *a, svn_fs_id_t *b)
{
  int i = 0;

  for (;;)
    {
      /* Invariant: i is even, and for all j < i, a[j] == b[j].
         Keep in mind: every even-numbered entry in A or B is a
         node/branch number; every odd-numbered entry is a version
         number.  So i is pointing at a node/branch number.  */

      /* If we've reached the end of A, then either A and B are equal,
	 or A is a prefix of B, so A is an ancestor of B.  Examples:
	 100.20 vs. 100.20; 100.20 vs. 100.20.3.2  */
      if (a[i] == -1)
	return 1;

      /* If the node/branch numbers differ, then they're unrelated.
	 Examples: 100.20 vs. 101.20; 100.20.2.3 vs. 100.20.3.1;

	 This also catches the case where B has ended before A ---
	 they're related, but A isn't B's ancestor.
	 Example: 100.20.3.2 vs 100.20.  */
      if (a[i] != b[i])
	return 0;

      /* If A's version number is greater than B's, then A is not B's
         ancestor.  Examples: 100.30 vs 100.20; 2.3.4.5 vs 2.3.4.4.  */
      if (a[i+1] > b[i+1])
	return 0;

      /* If A's version number is less than B's, then A is an ancestor
	 iff its ID ends now.  Examples: 100.30 vs 100.31; 100.30 vs
	 100.32.2.4.  */
      if (a[i+1] < b[i+1])
	return (a[i+2] == -1);

      /* Otherwise, we've established that the node/branch numbers and
         version numbers are equal, so go around again.  */
      i += 2;
    }
}


/* Compute the distance from the node version A to the node version
   identified by the first PREFIX elements of A.  In other words, this
   is the distance from a node version to some branch of the node
   version.  */
static int
distance_from_prefix (svn_fs_id_t *a, int prefix)
{
  int i;
  int d = 0;

  for (i = 0; a[prefix + i] != -1; i++)
    d += a[prefix + i + 1] - 1;

  return d;
}


static int
abs (int n)
{
  return (n < 0) ? -n : n;
}


int
svn_fs_id_distance (svn_fs_id_t *a, svn_fs_id_t *b)
{
  int i;

  /* Are they completely unrelated?  */
  if (a[0] != b[0])
    return -1;

  /* Skip any common prefix.  */
  for (i = 0; a[i] == b[i] && a[i+1] == b[i+1]; i += 2)
    /* If they're completely identical, then the distance is zero.  */
    if (a[i] == -1)
      return 0;

  /* Are they (branches off) different versions of the same node?
     Account for the distance between the two versions.  */
  if (a[i] == b[i])
    return (distance_from_prefix (a, i+2)
	    + distance_from_prefix (b, i+2)
	    + abs (a[i+1] - b[i+1]));
  else
    /* Or two branches off the same node version?  */
    return (distance_from_prefix (a, i)
	    + distance_from_prefix (b, i));
}



/* Parsing and unparsing node ID's.  */

svn_fs_id_t *
svn_fs__parse_id (char *data,
		  apr_size_t data_len,
		  apr_pool_t *pool)
{
  svn_fs_id_t *id;
  int id_len;
  
  /* Count the number of components in the ID, and check its syntax.  */
  {
    int i;
    int last_start;

    id_len = 1;
    last_start = 0;

    for (i = 0; i < data_len; i++)
      if (data[i] == '.')
	{
	  /* There must be at least one digit before and after each dot.  */
	  if (i == last_start)
	    return 0;
	  last_start = i + 1;
	  id_len++;
	}
      else if ('0' <= data[i] && data[i] <= '9')
	;
      else
	return 0;

    /* Make sure there was at least one digit in the last number.  */
    if (i == last_start)
      return 0;
  }

  /* Allocate the ID array.  */
  id = NEWARRAY (pool, svn_fs_id_t, id_len + 1);

  for (;;)
    {
      int i = 0;
      char *next = data;
      char *end = data + data_len;

      for (;;)
	{
	  id[i++] = svn_fs__getsize (data,
				     end - data,
				     &next,
				     100000000);
	  if (! next)
	    return 0;
	  if (next == end)
	    break;
	  if (*next != '.')
	    return 0;
	  next++;
	}

      id[i] = -1;
    }

  return id;
}


/* Set UNPARSED to the unparsed form of the node id ID.
   Allocate the buffer for the unparsed form in POOL.  */
svn_string_t *
svn_fs__unparse_id (svn_fs_id_t *id,
		    apr_pool_t *pool)
{
  svn_string_t *unparsed = svn_string_ncreate (0, 0, pool);
  int i;

  for (i = 0; id[i] != -1; i++)
    {
      char buf[200];
      int len = svn_fs__putsize (buf, sizeof (buf), id[i]);

      if (len == 0)
	abort ();

      if (id[i + 1] != -1)
	buf[len++] = '.';

      svn_string_appendbytes (unparsed, buf, len, pool);
    }

  return unparsed;
}



/* Copying ID's.  */

svn_fs_id_t *
svn_fs_copy_id (svn_fs_id_t *id, apr_pool_t *pool)
{
  apr_size_t id_size = (svn_fs_id_length (id) + 1) * sizeof (id[0]);
  svn_fs_id_t *copy = apr_pcalloc (pool, id_size);
  memcpy (copy, id, id_size);
  
  return copy;
}

