/*
 * svn_string.h:  routines to manipulate counted-length strings
 *                (svn_stringbuf_t and svn_string_t) and C strings.
 *                
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



#include <string.h>      /* for memcpy(), memcmp(), strlen() */
#include <apr_lib.h>     /* for apr_isspace() */
#include "svn_string.h"  /* loads "svn_types.h" and <apr_pools.h> */



/* Our own realloc, since APR doesn't have one.  Note: this is a
   generic realloc for memory pools, *not* for strings. */
static void *
my__realloc (char *data, const apr_size_t oldsize, const apr_size_t request, 
             apr_pool_t *pool)
{
  void *new_area;

  /* kff todo: it's a pity APR doesn't give us this -- sometimes it
     could realloc the block merely by extending in place, sparing us
     a memcpy(), but only the pool would know enough to be able to do
     this.  We should add a realloc() to APR if someone hasn't
     already. */

  /* malloc new area */
  new_area = apr_palloc (pool, request);

  /* copy data to new area */
  memcpy (new_area, data, oldsize);

  /* I'm NOT freeing old area here -- cuz we're using pools, ugh. */
  
  /* return new area */
  return new_area;
}


/* svn_string functions */

static svn_string_t *
create_string (const char *data, apr_size_t size,
               apr_pool_t *pool)
{
  svn_string_t *new_string;

  new_string = (svn_string_t *) apr_palloc (pool, sizeof (*new_string)); 

  new_string->data = data;
  new_string->len = size;

  return new_string;
}

svn_string_t *
svn_string_ncreate (const char *bytes, const apr_size_t size, 
                    apr_pool_t *pool)
{
  char *data;

  data = apr_palloc (pool, size + 1);
  memcpy (data, bytes, size);

  /* Null termination is the convention -- even if we suspect the data
     to be binary, it's not up to us to decide, it's the caller's
     call.  Heck, that's why they call it the caller! */
  data[size] = '\0';

  /* wrap an svn_string_t around the new data */
  return create_string (data, size, pool);
}


svn_string_t *
svn_string_create (const char *cstring, apr_pool_t *pool)
{
  return svn_string_ncreate (cstring, strlen (cstring), pool);
}


svn_string_t *
svn_string_create_from_buf (const svn_stringbuf_t *strbuf, apr_pool_t *pool)
{
  return svn_string_ncreate (strbuf->data, strbuf->len, pool);
}


svn_string_t *
svn_string_createv (apr_pool_t *pool, const char *fmt, va_list ap)
{
  char *data = apr_pvsprintf (pool, fmt, ap);

  /* wrap an svn_string_t around the new data */
  return create_string (data, strlen (data), pool);
}


svn_string_t *
svn_string_createf (apr_pool_t *pool, const char *fmt, ...)
{
  svn_string_t *str;

  va_list ap;
  va_start (ap, fmt);
  str = svn_string_createv (pool, fmt, ap);
  va_end (ap);

  return str;
}


svn_boolean_t
svn_string_isempty (const svn_string_t *str)
{
  return (str->len == 0);
}


svn_string_t *
svn_string_dup (const svn_string_t *original_string, apr_pool_t *pool)
{
  return (svn_string_ncreate (original_string->data,
                              original_string->len, pool));
}



svn_boolean_t
svn_string_compare (const svn_string_t *str1, const svn_string_t *str2)
{
  /* easy way out :)  */
  if (str1->len != str2->len)
    return FALSE;

  /* now that we know they have identical lengths... */
  
  if (memcmp (str1->data, str2->data, str1->len))
    return FALSE;
  else
    return TRUE;
}



apr_size_t
svn_string_first_non_whitespace (const svn_string_t *str)
{
  apr_size_t i;

  for (i = 0; i < str->len; i++)
    {
      if (! apr_isspace (str->data[i]))
        {
          return i;
        }
    }

  /* if we get here, then the string must be entirely whitespace */
  return (-1);  
}


void
svn_string_strip_whitespace (svn_string_t *str)
{
  apr_size_t i;

  /* Find first non-whitespace character */
  apr_size_t offset = svn_string_first_non_whitespace (str);

  /* Go ahead!  Waste some RAM, we've got pools! :)  */
  str->data += offset;
  str->len -= offset;

  /* Now that we've chomped whitespace off the front, search backwards
     from the end for the first non-whitespace. */

  for (i = (str->len - 1); i >= 0; i--)
    {
      if (! apr_isspace (str->data[i]))
        {
          break;
        }
    }
  
  /* Mmm, waste some more RAM */
  str->len = i + 1;

  /* ### In svn_stringbuf_strip_whitespace, we reset the null
     terminator here.  But svn_string_t can have const data, so I
     don't think we can do that, unfortunately.  */
}


apr_size_t
svn_string_find_char_backward (const svn_string_t *str, char ch)
{
  int i;        /* signed! */

  for (i = (str->len - 1); i >= 0; i--)
    {
      if (str->data[i] == ch)
        return i;
    }

  return str->len;
}



/* svn_stringbuf functions */

static svn_stringbuf_t *
create_stringbuf (char *data, apr_size_t size, apr_pool_t *pool)
{
  svn_stringbuf_t *new_string;

  new_string = (svn_stringbuf_t *) apr_palloc (pool, sizeof (*new_string)); 

  new_string->data = data;
  new_string->len = size;
  new_string->blocksize = size + 1;	/* we know there is a null-term */
  new_string->pool = pool;

  return new_string;
}

svn_stringbuf_t *
svn_stringbuf_ncreate (const char *bytes, const apr_size_t size, 
                       apr_pool_t *pool)
{
  char *data;

  data = apr_palloc (pool, size + 1);
  memcpy (data, bytes, size);

  /* Null termination is the convention -- even if we suspect the data
     to be binary, it's not up to us to decide, it's the caller's
     call.  Heck, that's why they call it the caller! */
  data[size] = '\0';

  /* wrap an svn_stringbuf_t around the new data */
  return create_stringbuf (data, size, pool);
}


svn_stringbuf_t *
svn_stringbuf_create (const char *cstring, apr_pool_t *pool)
{
  return svn_stringbuf_ncreate (cstring, strlen (cstring), pool);
}


svn_stringbuf_t *
svn_stringbuf_create_from_string (const svn_string_t *str, apr_pool_t *pool)
{
  return svn_stringbuf_ncreate (str->data, str->len, pool);
}


svn_stringbuf_t *
svn_stringbuf_createv (apr_pool_t *pool, const char *fmt, va_list ap)
{
  char *data = apr_pvsprintf (pool, fmt, ap);

  /* wrap an svn_stringbuf_t around the new data */
  return create_stringbuf (data, strlen (data), pool);
}


svn_stringbuf_t *
svn_stringbuf_createf (apr_pool_t *pool, const char *fmt, ...)
{
  svn_stringbuf_t *str;

  va_list ap;
  va_start (ap, fmt);
  str = svn_stringbuf_createv (pool, fmt, ap);
  va_end (ap);

  return str;
}


void 
svn_stringbuf_fillchar (svn_stringbuf_t *str, const unsigned char c)
{
  memset (str->data, c, str->len);
}


void
svn_stringbuf_set (svn_stringbuf_t *str, const char *value)
{
  apr_size_t amt = strlen (value);

  svn_stringbuf_ensure (str, amt + 1);
  memcpy (str->data, value, amt + 1);
  str->len = amt;
}

void
svn_stringbuf_setempty (svn_stringbuf_t *str)
{
  if (str->len > 0)
    str->data[0] = '\0';

  str->len = 0;
}


void
svn_stringbuf_chop (svn_stringbuf_t *str, apr_size_t nbytes)
{
  if (nbytes > str->len)
    str->len = 0;
  else
    str->len -= nbytes;

  str->data[str->len] = '\0';
}


svn_boolean_t
svn_stringbuf_isempty (const svn_stringbuf_t *str)
{
  return (str->len == 0);
}


void
svn_stringbuf_ensure (svn_stringbuf_t *str, apr_size_t minimum_size)
{
  /* Keep doubling capacity until have enough. */
  if (str->blocksize < minimum_size)
    {
      if (str->blocksize == 0)
        str->blocksize = minimum_size;
      else
        while (str->blocksize < minimum_size)
          str->blocksize *= 2;

      str->data = (char *) my__realloc (str->data, 
                                        str->len,
                                        str->blocksize,
                                        str->pool); 
    }
}


void
svn_stringbuf_appendbytes (svn_stringbuf_t *str, const char *bytes, 
                           const apr_size_t count)
{
  apr_size_t total_len;
  void *start_address;

  total_len = str->len + count;  /* total size needed */

  /* +1 for null terminator. */
  svn_stringbuf_ensure (str, (total_len + 1));

  /* get address 1 byte beyond end of original bytestring */
  start_address = (str->data + str->len);

  memcpy (start_address, (void *) bytes, count);
  str->len = total_len;

  str->data[str->len] = '\0';  /* We don't know if this is binary
                                  data or not, but convention is
                                  to null-terminate. */
}


void
svn_stringbuf_appendstr (svn_stringbuf_t *targetstr, 
                         const svn_stringbuf_t *appendstr)
{
  svn_stringbuf_appendbytes (targetstr, appendstr->data, appendstr->len);
}


void
svn_stringbuf_appendcstr (svn_stringbuf_t *targetstr, const char *cstr)
{
  svn_stringbuf_appendbytes (targetstr, cstr, strlen(cstr));
}




svn_stringbuf_t *
svn_stringbuf_dup (const svn_stringbuf_t *original_string, apr_pool_t *pool)
{
  return (svn_stringbuf_ncreate (original_string->data,
                              original_string->len, pool));
}



svn_boolean_t
svn_stringbuf_compare (const svn_stringbuf_t *str1, 
                       const svn_stringbuf_t *str2)
{
  /* easy way out :)  */
  if (str1->len != str2->len)
    return FALSE;

  /* now that we know they have identical lengths... */
  
  if (memcmp (str1->data, str2->data, str1->len))
    return FALSE;
  else
    return TRUE;
}



apr_size_t
svn_stringbuf_first_non_whitespace (const svn_stringbuf_t *str)
{
  apr_size_t i;

  for (i = 0; i < str->len; i++)
    {
      if (! apr_isspace (str->data[i]))
        {
          return i;
        }
    }

  /* if we get here, then the string must be entirely whitespace */
  return (-1);  
}


void
svn_stringbuf_strip_whitespace (svn_stringbuf_t *str)
{
  apr_size_t i;

  /* Find first non-whitespace character */
  apr_size_t offset = svn_stringbuf_first_non_whitespace (str);

  /* Go ahead!  Waste some RAM, we've got pools! :)  */
  str->data += offset;
  str->len -= offset;
  str->blocksize -= offset;

  /* Now that we've chomped whitespace off the front, search backwards
     from the end for the first non-whitespace. */

  for (i = (str->len - 1); i >= 0; i--)
    {
      if (! apr_isspace (str->data[i]))
        {
          break;
        }
    }
  
  /* Mmm, waste some more RAM */
  str->len = i + 1;
  str->data[str->len] = '\0';
}


apr_size_t
svn_stringbuf_find_char_backward (const svn_stringbuf_t *str, char ch)
{
  int i;        /* signed! */

  for (i = (str->len - 1); i >= 0; i--)
    {
      if (str->data[i] == ch)
        return i;
    }

  return str->len;
}


apr_size_t
svn_stringbuf_chop_back_to_char (svn_stringbuf_t *str, char ch)
{
  apr_size_t i = svn_stringbuf_find_char_backward (str, ch);

  if (i < str->len)
    {
      apr_size_t nbytes = (str->len - i);
      svn_stringbuf_chop (str, nbytes);
      return nbytes;
    }

  return 0;
}


svn_boolean_t
svn_string_compare_stringbuf (const svn_string_t *str1,
                              const svn_stringbuf_t *str2)
{
  /* easy way out :)  */
  if (str1->len != str2->len)
    return FALSE;

  /* now that we know they have identical lengths... */

  if (memcmp (str1->data, str2->data, str1->len))
    return FALSE;
  else
    return TRUE;
}



/*** C string stuff. ***/

apr_array_header_t *
svn_cstring_split (const char *input,
                   char sep_char,
                   svn_boolean_t chop_whitespace,
                   apr_pool_t *pool)
{
  const char *b = input, *e = input;
  svn_boolean_t one_last_time = (! *e);
  apr_array_header_t *substrings = apr_array_make (pool, 1, sizeof (input));

  while (1)
    {
      if ((*e == sep_char) || (*e == '\0'))
        {
          const char *b2 = b, *e2 = e;
          
          if (chop_whitespace)
            {
              while (isspace (*b2))
                b2++;

              if (e2 != b2)
                {
                  while (isspace (*(--e2)))
                    ;
                  e2++;
                }
            }

          *((char **) (apr_array_push (substrings)))
            = apr_pstrmemdup (pool, b2, e2 - b2);

          b = ++e;
        }
      else
        e++;

      if (one_last_time)
        break;
      if (*e == '\0')
        one_last_time = TRUE;
    }

  return NULL;
}




/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

