/* skel.c --- parsing and unparsing skeletons
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

#include <string.h>
#include "svn_string.h"
#include "skel.h"
#include "../key-gen.h"


/* Parsing skeletons.  */

enum char_type {
  type_nothing = 0,
  type_space = 1,
  type_digit = 2,
  type_paren = 3,
  type_name = 4
};


/* We can't use the <ctype.h> macros here, because they are locale-
   dependent.  The syntax of a skel is specified directly in terms of
   byte values, and is independent of locale.  */

static const enum char_type skel_char_type[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 1, 1, 0, 1, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0,   3, 3, 0, 0, 0, 0, 0, 0,
  2, 2, 2, 2, 2, 2, 2, 2,   2, 2, 0, 0, 0, 0, 0, 0,

  /* 64 */
  0, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 3, 0, 3, 0, 0,
  0, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,   4, 4, 4, 0, 0, 0, 0, 0,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  /* 192 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};


static skel_t *parse(const char *data, apr_size_t len,
                     apr_pool_t *pool);
static skel_t *list(const char *data, apr_size_t len,
                    apr_pool_t *pool);
static skel_t *implicit_atom(const char *data, apr_size_t len,
                             apr_pool_t *pool);
static skel_t *explicit_atom(const char *data, apr_size_t len,
                             apr_pool_t *pool);


skel_t *
svn_fs_base__parse_skel(const char *data,
                        apr_size_t len,
                        apr_pool_t *pool)
{
  return parse(data, len, pool);
}


/* Parse any kind of skel object --- atom, or list.  */
static skel_t *
parse(const char *data,
      apr_size_t len,
      apr_pool_t *pool)
{
  char c;

  /* The empty string isn't a valid skel.  */
  if (len <= 0)
    return 0;

  c = *data;

  /* Is it a list, or an atom?  */
  if (c == '(')
    return list(data, len, pool);

  /* Is it a string with an implicit length?  */
  if (skel_char_type[(unsigned char) c] == type_name)
    return implicit_atom(data, len, pool);

  /* Otherwise, we assume it's a string with an explicit length;
     svn_fs_base__getsize will catch the error.  */
  else
    return explicit_atom(data, len, pool);
}


static skel_t *
list(const char *data,
     apr_size_t len,
     apr_pool_t *pool)
{
  const char *end = data + len;
  const char *list_start;

  /* Verify that the list starts with an opening paren.  At the
     moment, all callers have checked this already, but it's more
     robust this way.  */
  if (data >= end || *data != '(')
    return 0;

  /* Mark where the list starts.  */
  list_start = data;

  /* Skip the opening paren.  */
  data++;

  /* Parse the children.  */
  {
    skel_t *children = 0;
    skel_t **tail = &children;

    for (;;)
      {
        skel_t *element;

        /* Skip any whitespace.  */
        while (data < end
               && skel_char_type[(unsigned char) *data] == type_space)
          data++;

        /* End of data, but no closing paren?  */
        if (data >= end)
          return 0;

        /* End of list?  */
        if (*data == ')')
          {
            data++;
            break;
          }

        /* Parse the next element in the list.  */
        element = parse(data, end - data, pool);
        if (! element)
          return 0;

        /* Link that element into our list.  */
        element->next = 0;
        *tail = element;
        tail = &element->next;

        /* Advance past that element.  */
        data = element->data + element->len;
      }

    /* Construct the return value.  */
    {
      skel_t *s = apr_pcalloc(pool, sizeof(*s));

      s->is_atom = FALSE;
      s->data = list_start;
      s->len = data - list_start;
      s->children = children;

      return s;
    }
  }
}


/* Parse an atom with implicit length --- one that starts with a name
   character, terminated by whitespace, '(', ')', or end-of-data.  */
static skel_t *
implicit_atom(const char *data,
              apr_size_t len,
              apr_pool_t *pool)
{
  const char *start = data;
  const char *end = data + len;
  skel_t *s;

  /* Verify that the atom starts with a name character.  At the
     moment, all callers have checked this already, but it's more
     robust this way.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_name)
    return 0;

  /* Find the end of the string.  */
  while (++data < end
         && skel_char_type[(unsigned char) *data] != type_space
         && skel_char_type[(unsigned char) *data] != type_paren)
    ;

  /* Allocate the skel representing this string.  */
  s = apr_pcalloc(pool, sizeof(*s));
  s->is_atom = TRUE;
  s->data = start;
  s->len = data - start;

  return s;
}


/* Parse an atom with explicit length --- one that starts with a byte
   length, as a decimal ASCII number.  */
static skel_t *
explicit_atom(const char *data,
              apr_size_t len,
              apr_pool_t *pool)
{
  const char *end = data + len;
  const char *next;
  apr_size_t size;
  skel_t *s;

  /* Parse the length.  */
  size = svn_fs_base__getsize(data, end - data, &next, end - data);
  data = next;

  /* Exit if we overflowed, or there wasn't a valid number there.  */
  if (! data)
    return 0;

  /* Skip the whitespace character after the length.  */
  if (data >= end || skel_char_type[(unsigned char) *data] != type_space)
    return 0;
  data++;

  /* Check the length.  */
  if (data + size > end)
    return 0;

  /* Allocate the skel representing this string.  */
  s = apr_pcalloc(pool, sizeof(skel_t));
  s->is_atom = TRUE;
  s->data = data;
  s->len = size;

  return s;
}



/* Unparsing skeletons.  */

static apr_size_t estimate_unparsed_size(skel_t *);
static svn_stringbuf_t *unparse(skel_t *, svn_stringbuf_t *, apr_pool_t *);


svn_stringbuf_t *
svn_fs_base__unparse_skel(skel_t *skel, apr_pool_t *pool)
{
  svn_stringbuf_t *str;

  /* Allocate a string to hold the data.  */
  str = apr_palloc(pool, sizeof(*str));
  str->pool = pool;
  str->blocksize = estimate_unparsed_size(skel) + 200;
  str->data = apr_palloc(pool, str->blocksize);
  str->len = 0;

  return unparse(skel, str, pool);
}


/* Return an estimate of the number of bytes that the external
   representation of SKEL will occupy.  Since reallocing is expensive
   in pools, it's worth trying to get the buffer size right the first
   time.  */
static apr_size_t
estimate_unparsed_size(skel_t *skel)
{
  if (skel->is_atom)
    {
      if (skel->len < 100)
        /* If we have to use the explicit-length form, that'll be
           two bytes for the length, one byte for the space, and
           the contents.  */
        return skel->len + 3;
      else
        return skel->len + 30;
    }
  else
    {
      int total_len;
      skel_t *child;

      /* Allow space for opening and closing parens, and a space
         between each pair of elements.  */
      total_len = 2;
      for (child = skel->children; child; child = child->next)
        total_len += estimate_unparsed_size(child) + 1;

      return total_len;
    }
}


/* Return non-zero iff we should use the implicit-length form for SKEL.
   Assume that SKEL is an atom.  */
static svn_boolean_t
use_implicit(skel_t *skel)
{
  /* If it's null, or long, we should use explicit-length form.  */
  if (skel->len == 0
      || skel->len >= 100)
    return FALSE;

  /* If it doesn't start with a name character, we must use
     explicit-length form.  */
  if (skel_char_type[(unsigned char) skel->data[0]] != type_name)
    return FALSE;

  /* If it contains any whitespace or parens, then we must use
     explicit-length form.  */
  {
    apr_size_t i;

    for (i = 1; i < skel->len; i++)
      if (skel_char_type[(unsigned char) skel->data[i]] == type_space
          || skel_char_type[(unsigned char) skel->data[i]] == type_paren)
        return FALSE;
  }

  /* If we can't reject it for any of the above reasons, then we can
     use implicit-length form.  */
  return TRUE;
}


/* Append the concrete representation of SKEL to the string STR.
   Grow S with new space from POOL as necessary.  */
static svn_stringbuf_t *
unparse(skel_t *skel, svn_stringbuf_t *str, apr_pool_t *pool)
{
  if (skel->is_atom)
    {
      /* Append an atom to STR.  */
      if (use_implicit(skel))
        svn_stringbuf_appendbytes(str, skel->data, skel->len);
      else
        {
          /* Append the length to STR.  */
          char buf[200];
          int length_len;

          length_len = svn_fs_base__putsize(buf, sizeof(buf), skel->len);
          if (! length_len)
            abort();

          /* Make sure we have room for the length, the space, and the
             atom's contents.  */
          svn_stringbuf_ensure(str, str->len + length_len + 1 + skel->len);
          svn_stringbuf_appendbytes(str, buf, length_len);
          str->data[str->len++] = ' ';
          svn_stringbuf_appendbytes(str, skel->data, skel->len);
        }
    }
  else
    {
      /* Append a list to STR.  */
      skel_t *child;

      /* Emit an opening parenthesis.  */
      svn_stringbuf_ensure(str, str->len + 1);
      str->data[str->len++] = '(';

      /* Append each element.  Emit a space between each pair of elements.  */
      for (child = skel->children; child; child = child->next)
        {
          unparse(child, str, pool);
          if (child->next)
            {
              svn_stringbuf_ensure(str, str->len + 1);
              str->data[str->len++] = ' ';
            }
        }

      /* Emit a closing parenthesis.  */
      svn_stringbuf_appendbytes(str, ")", 1);
    }

  return str;
}



/* Building skels.  */


skel_t *
svn_fs_base__str_atom(const char *str, apr_pool_t *pool)
{
  skel_t *skel = apr_pcalloc(pool, sizeof(*skel));
  skel->is_atom = TRUE;
  skel->data = str;
  skel->len = strlen(str);

  return skel;
}


skel_t *
svn_fs_base__mem_atom(const void *addr,
                      apr_size_t len,
                      apr_pool_t *pool)
{
  skel_t *skel = apr_pcalloc(pool, sizeof(*skel));
  skel->is_atom = TRUE;
  skel->data = addr;
  skel->len = len;

  return skel;
}


skel_t *
svn_fs_base__make_empty_list(apr_pool_t *pool)
{
  skel_t *skel = apr_pcalloc(pool, sizeof(*skel));
  return skel;
}


void
svn_fs_base__prepend(skel_t *skel, skel_t *list_skel)
{
  /* If list_skel isn't even a list, somebody's not using this
     function properly. */
  if (list_skel->is_atom)
    abort();

  skel->next = list_skel->children;
  list_skel->children = skel;
}


void
svn_fs_base__append(skel_t *skel, skel_t *list_skel)
{
  /* If list_skel isn't even a list, somebody's not using this
     function properly. */
  if (list_skel->is_atom)
    abort();

  /* No kids?  Let's make one. */
  if (! list_skel->children)
    {
      list_skel->children = skel;
    }
  else
    {
      skel_t *tmp = list_skel->children;

      /* Find the last child... */
      while (tmp->next)
        {
          tmp = tmp->next;
        }
      /* ...and then give her a sister. */
      tmp->next = skel;
    }
}



/* Examining skels.  */


svn_boolean_t
svn_fs_base__matches_atom(skel_t *skel, const char *str)
{
  if (skel && skel->is_atom)
    {
      apr_size_t len = strlen(str);

      return ((skel->len == len
               && ! memcmp(skel->data, str, len)) ? TRUE : FALSE);
    }
  return FALSE;
}


int
svn_fs_base__atom_matches_string(skel_t *skel, const svn_string_t *str)
{
  if (skel && skel->is_atom)
    {
      return ((skel->len == str->len
               && ! memcmp(skel->data, str->data, skel->len)) ? TRUE : FALSE);
    }
  return FALSE;
}


int
svn_fs_base__list_length(skel_t *skel)
{
  int len = 0;
  skel_t *child;

  if ((! skel) || skel->is_atom)
    return -1;

  for (child = skel->children; child; child = child->next)
    len++;

  return len;
}



/* Comparing skels. */

svn_boolean_t
svn_fs_base__skels_are_equal(skel_t *skel1, skel_t *skel2)
{
  if (skel1 == skel2)
    return TRUE;

  /* Else not `eq', but might still be `equal'. */

  if (skel1->is_atom && skel2->is_atom)
    {
      if ((skel1->len == skel2->len)
          && (! strncmp(skel1->data, skel2->data, skel1->len)))
        return TRUE;
      else
        return FALSE;
    }
  else if (((! skel1->is_atom) && (! skel2->is_atom))
           && ((svn_fs_base__list_length(skel1))
               == (svn_fs_base__list_length(skel2))))
    {
      int len = svn_fs_base__list_length(skel1);
      int i;

      for (i = 0; i < len; i++)
        if (! svn_fs_base__skels_are_equal((skel1->children) + i,
                                           (skel2->children) + i))
          return FALSE;

      return TRUE;
    }
  else
    return FALSE;
}



/* Copying skels.  */


skel_t *
svn_fs_base__copy_skel(skel_t *skel, apr_pool_t *pool)
{
  skel_t *copy = apr_pcalloc(pool, sizeof(*copy));

  if (skel->is_atom)
    {
      apr_size_t len = skel->len;
      char *s = apr_palloc(pool, len);

      memcpy(s, skel->data, len);
      copy->is_atom = TRUE;
      copy->data = s;
      copy->len = len;
    }
  else
    {
      skel_t *skel_child, **copy_child_ptr;

      copy->is_atom = FALSE;
      copy->data = 0;
      copy->len = 0;

      copy_child_ptr = &copy->children;
      for (skel_child = skel->children;
           skel_child;
           skel_child = skel_child->next)
        {
          *copy_child_ptr = svn_fs_base__copy_skel(skel_child, pool);
          copy_child_ptr = &(*copy_child_ptr)->next;
        }
      *copy_child_ptr = 0;
    }

  return copy;
}
