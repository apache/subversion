/* key-gen.c --- manufacturing sequential keys for some db tables
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
#include <string.h>
#include <stdlib.h>
#include <apr.h>
#include "key-gen.h"


/*** Keys for reps and strings. ***/

void
svn_fs_fs__add_keys(const char *key1, const char *key2, char *result)
{
  int i1 = strlen(key1) - 1;
  int i2 = strlen(key2) - 1;
  int i3 = 0;
  int val;
  int carry = 0;
  char buf[MAX_KEY_SIZE + 2];

  while ((i1 >= 0) || (i2 >= 0) || (carry > 0))
    {
      val = carry;
      if (i1>=0)
        val += (key1[i1] <= '9') ? (key1[i1] - '0') : (key1[i1] - 'a' + 10);

      if (i2>=0)
        val += (key2[i2] <= '9') ? (key2[i2] - '0') : (key2[i2] - 'a' + 10);

      carry = val / 36;
      val = val % 36;
      
      buf[i3++] = (val <= 9) ? (val + '0') : (val - 10 + 'a');

      if (i1>=0)
        i1--;
      if (i2>=0)
        i2--;
    }

  /* Now reverse the resulting string and NULL terminate it. */
  for (i1 = 0; i1 < i3; i1++)
    result[i1] = buf[i3 - i1 - 1];

  result[i1] = '\0';
}


void
svn_fs_fs__next_key(const char *this, apr_size_t *len, char *next)
{
  apr_size_t olen = *len;     /* remember the first length */
  int i = olen - 1;           /* initial index; we work backwards */
  char c;                     /* current char */
  svn_boolean_t carry = TRUE; /* boolean: do we have a carry or not?
                                 We start with a carry, because we're
                                 incrementing the number, after all. */
  
  /* Leading zeros are not allowed, except for the string "0". */
  if ((*len > 1) && (this[0] == '0'))
    {
      *len = 0;
      return;
    }
  
  for (i = (olen - 1); i >= 0; i--)
    {
      c = this[i];

      /* Validate as we go. */
      if (! (((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'z'))))
        {
          *len = 0;
          return;
        }

      if (carry)
        {
          if (c == 'z')
            next[i] = '0';
          else
            {
              carry = FALSE;
              
              if (c == '9')
                next[i] = 'a';
              else
                next[i] = c + 1;
            }
        }
      else
        next[i] = c;
    }

  /* The new length is OLEN, plus 1 if there's a carry out of the
     leftmost digit. */
  *len = olen + (carry ? 1 : 0);

  /* Ensure that we haven't overrun the (ludicrous) bound on key length.
     Note that MAX_KEY_SIZE is a bound on the size *including*
     the trailing null byte. */
  assert(*len < MAX_KEY_SIZE);

  /* Now we know it's safe to add the null terminator. */
  next[*len] = '\0';

  /* Handle any leftover carry. */
  if (carry)
    {
      memmove(next+1, next, olen);
      next[0] = '1';
    }
}


int
svn_fs_fs__key_compare(const char *a, const char *b)
{
  int a_len = strlen(a);
  int b_len = strlen(b);
  int cmp;

  if (a_len > b_len)
    return 1;
  if (b_len > a_len)
    return -1;
  cmp = strcmp(a, b);
  return (cmp ? (cmp / abs(cmp)) : 0);
}
