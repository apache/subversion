/*
 * paths.c:   a path manipulation library using svn_stringbuf_t
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
#include <assert.h>

#include <apr_file_info.h>

#include "svn_string.h"
#include "svn_path.h"
#include "svn_private_config.h"         /* for SVN_PATH_LOCAL_SEPARATOR */
#include "svn_utf.h"


/* The canonical empty path.  Can this be changed?  Well, change the empty
   test below and the path library will work, not so sure about the fs/wc
   libraries. */
#define SVN_EMPTY_PATH ""

/* TRUE if s is the canonical empty path, FALSE otherwise */
#define SVN_PATH_IS_EMPTY(s) ((s)[0] == '\0')

/* TRUE if s,n is the platform's empty path ("."), FALSE otherwise. Can
   this be changed?  Well, the path library will work, not so sure about
   the OS! */
#define SVN_PATH_IS_PLATFORM_EMPTY(s,n) ((n) == 1 && (s)[0] == '.')


const char *
svn_path_internal_style (const char *path, apr_pool_t *pool)
{
  if ('/' != SVN_PATH_LOCAL_SEPARATOR)
    {
      char *p = apr_pstrdup (pool, path);
      path = p;

      /* Convert all local-style separators to the canonical ones. */
      for (; *p != '\0'; ++p)
        if (*p == SVN_PATH_LOCAL_SEPARATOR)
          *p = '/';
    }

  return svn_path_canonicalize (path, pool);
  /* FIXME: Should also remove trailing /.'s, if the style says so. */
}


const char *
svn_path_local_style (const char *path, apr_pool_t *pool)
{
  path = svn_path_canonicalize (path, pool);
  /* FIXME: Should also remove trailing /.'s, if the style says so. */

  if ('/' != SVN_PATH_LOCAL_SEPARATOR)
    {
      char *p = apr_pstrdup (pool, path);
      path = p;

      /* Convert all canonical separators to the local-style ones. */
      for (; *p != '\0'; ++p)
        if (*p == '/')
          *p = SVN_PATH_LOCAL_SEPARATOR;
    }

  return path;
}



/* Identify trailing '/' and "/." suffix of PATH.  LEN is the number of
   characters in PATH.  Returns the number characters without the
   suffix. */
static apr_size_t
discount_trailing_dot_slash (const char *path,
                             apr_size_t len)
{
  while (len > 0)
    if (path[len - 1] == '/')
      --len;
    else if (len > 1
             && SVN_PATH_IS_PLATFORM_EMPTY(path + len - 1, 1)
             && path[len - 2] == '/')
      len -= 2;
    else
      break;

  return len;
}

const char *
svn_path_canonicalize (const char *path, apr_pool_t *pool)
{
  /* At some point this could eliminate redundant components.
     For now, it just makes sure there is no trailing slashes
     or dots, and converts "." to "".  */

  apr_size_t len, orig_len;

  orig_len = strlen (path);
  len = discount_trailing_dot_slash (path, orig_len);
  if (len == 0 && orig_len > 0 && path[0] == '/')
    len = 1;

  if (SVN_PATH_IS_PLATFORM_EMPTY (path, len))
    return SVN_EMPTY_PATH;      /* the canonical empty path */

  if (len == orig_len)
    return path;

  return apr_pstrmemdup (pool, path, len);
}


static svn_boolean_t
is_canonical (const char *path,
              apr_size_t len)
{
  return (! SVN_PATH_IS_PLATFORM_EMPTY (path, len)
          && (len <= 1 || path[len-1] != '/'));
}


char *svn_path_join (const char *base,
                     const char *component,
                     apr_pool_t *pool)
{
  apr_size_t blen = strlen (base);
  apr_size_t clen = strlen (component);
  char *path;

  assert (is_canonical (base, blen));
  assert (is_canonical (component, clen));

  /* If the component is absolute, then return it.  */
  if (*component == '/')
    return apr_pmemdup (pool, component, clen + 1);

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY (base))
    return apr_pmemdup (pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY (component))
    return apr_pmemdup (pool, base, blen + 1);

  if (blen == 1 && base[0] == '/')
    blen = 0; /* Ignore base, just return separator + component */

  /* Construct the new, combined path. */
  path = apr_palloc (pool, blen + 1 + clen + 1);
  memcpy (path, base, blen);
  path[blen] = '/';
  memcpy (path + blen + 1, component, clen + 1);

  return path;
}

char *svn_path_join_many (apr_pool_t *pool, const char *base, ...)
{
#define MAX_SAVED_LENGTHS 10
  apr_size_t saved_lengths[MAX_SAVED_LENGTHS];
  apr_size_t total_len;
  int nargs;
  va_list va;
  const char *s;
  apr_size_t len;
  char *path;
  char *p;
  svn_boolean_t base_is_empty = FALSE, base_is_root = FALSE;
  int base_arg = 0;

  total_len = strlen (base);

  assert (is_canonical (base, total_len));

  if (total_len == 1 && *base == '/')
    base_is_root = TRUE;
  else if (SVN_PATH_IS_EMPTY (base))
    {
      total_len = sizeof (SVN_EMPTY_PATH) - 1;
      base_is_empty = TRUE;
    }

  saved_lengths[0] = total_len;

  /* Compute the length of the resulting string. */

  nargs = 0;
  va_start (va, base);
  while ((s = va_arg (va, const char *)) != NULL)
    {
      len = strlen (s);

      assert (is_canonical (s, len));

      if (SVN_PATH_IS_EMPTY (s))
        continue;

      if (nargs++ < MAX_SAVED_LENGTHS)
        saved_lengths[nargs] = len;

      if (*s == '/')
        {
          /* an absolute path. skip all components to this point and reset
             the total length. */
          total_len = len;
          base_arg = nargs;
          base_is_root = len == 1;
          base_is_empty = FALSE;
        }
      else if (nargs == base_arg
               || (nargs == base_arg + 1 && base_is_root)
               || base_is_empty)
        {
          /* if we have skipped everything up to this arg, then the base
             and all prior components are empty. just set the length to
             this component; do not add a separator.  If the base is empty
             we can now ignore it. */
          if (base_is_empty)
            {
              base_is_empty = FALSE;
              total_len = 0;
            }
          total_len += len;
        }
      else
        {
          total_len += 1 + len;
        }
    }
  va_end (va);

  /* base == "/" and no further components. just return that. */
  if (base_is_root && total_len == 1)
    return apr_pmemdup (pool, "/", 2);

  /* we got the total size. allocate it, with room for a NUL character. */
  path = p = apr_palloc (pool, total_len + 1);

  /* if we aren't supposed to skip forward to an absolute component, and if
     this is not an empty base that we are skipping, then copy the base
     into the output. */
  if (base_arg == 0 && ! (SVN_PATH_IS_EMPTY (base) && ! base_is_empty))
    {
      if (SVN_PATH_IS_EMPTY (base))
        memcpy(p, SVN_EMPTY_PATH, len = saved_lengths[0]);
      else
        memcpy(p, base, len = saved_lengths[0]);
      p += len;
    }

  nargs = 0;
  va_start (va, base);
  while ((s = va_arg (va, const char *)) != NULL)
    {
      if (SVN_PATH_IS_EMPTY (s))
        continue;

      if (++nargs < base_arg)
        continue;

      if (nargs < MAX_SAVED_LENGTHS)
        len = saved_lengths[nargs];
      else
        len = strlen (s);

      /* insert a separator if we aren't copying in the first component
         (which can happen when base_arg is set). also, don't put in a slash
         if the prior character is a slash (occurs when prior component
         is "/"). */
      if (p != path && p[-1] != '/')
        *p++ = '/';

      /* copy the new component and advance the pointer */
      memcpy (p, s, len);
      p += len;
    }
  va_end (va);

  *p = '\0';
  assert ((apr_size_t)(p - path) == total_len);

  return path;
}



void
svn_path_add_component (svn_stringbuf_t *path, 
                        const char *component)
{
  apr_size_t len = strlen (component);

  assert (is_canonical (path->data, path->len));
  assert (is_canonical (component, len));

  /* Append a dir separator, but only if this path is neither empty
     nor consists of a single dir separator already. */
  if ((! SVN_PATH_IS_EMPTY (path->data))
      && (! ((path->len == 1) && (*(path->data) == '/'))))
    {
      char dirsep = '/';
      svn_stringbuf_appendbytes (path, &dirsep, sizeof (dirsep));
    }

  svn_stringbuf_appendbytes (path, component, len);
}


void
svn_path_remove_component (svn_stringbuf_t *path)
{
  apr_size_t len;

  assert (is_canonical (path->data, path->len));

  while (path->len > 0 && path->data[path->len - 1] != '/')
    --path->len;

  path->data[path->len] = '\0';

  /* ### Right now the input path could contain redundant components,
     since svn_path_canonicalize doesn't remove them.  So we have to
     check and strip those off. */
  len = discount_trailing_dot_slash (path->data, path->len);
  if (len == 0 && path->len > 0 && path->data[0] == '/')
    len = 1;

  if (SVN_PATH_IS_PLATFORM_EMPTY (path->data, len))
    svn_stringbuf_set (path, SVN_EMPTY_PATH);
  else
    {
      path->len = len;
      path->data[path->len] = '\0';
    }
}


char *
svn_path_dirname (const char *path, apr_pool_t *pool)
{
  apr_size_t canonical_len, len = strlen (path);

  assert (is_canonical (path, len));

  while (len > 0 && path[len - 1] != '/')
    --len;

  /* ### Should canonicalization strip "//" and "/./" substrings? */
  canonical_len = discount_trailing_dot_slash (path, len);

  if (canonical_len == 0 && len > 0 && path[0] == '/')
    canonical_len = 1;

  if (SVN_PATH_IS_PLATFORM_EMPTY (path, canonical_len))
    return apr_pmemdup (pool, SVN_EMPTY_PATH, sizeof (SVN_EMPTY_PATH));

  return apr_pstrmemdup (pool, path, canonical_len);
}


char *
svn_path_basename (const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen (path);
  apr_size_t start;

  assert (is_canonical (path, len));

  if (len == 1 && path[0] == '/')
    start = 0;
  else
    {
      start = len;
      while (start > 0 && path[start - 1] != '/')
        --start;
    }

  return apr_pstrmemdup (pool, path + start, len - start);
}



void
svn_path_split (const char *path,
                const char **dirpath,
                const char **base_name,
                apr_pool_t *pool)
{
  assert (dirpath != base_name);

  if (dirpath)
    *dirpath = svn_path_dirname (path, pool);

  if (base_name)
    *base_name = svn_path_basename (path, pool);
}


int
svn_path_is_empty (const char *path)
{
  /* assert (is_canonical (path, strlen (path))); ### Expensive strlen */

  if (SVN_PATH_IS_EMPTY (path))
    return 1;

  return 0;
}


int
svn_path_compare_paths (const char *path1,
                        const char *path2)
{
  /* ### This code is inherited from the svn_stringbuf_t version of
     the function and is inefficient on plain strings, because it does
     strlen() on both strings up front.  Recode and/or abstract to
     share with svn_path_compare_paths (if that other function stays
     around). */

  int path1_len = strlen (path1);
  int path2_len = strlen (path2);

  apr_size_t min_len = ((path1_len < path2_len) ? path1_len : path2_len);
  apr_size_t i = 0;

  assert (is_canonical (path1, path1_len));
  assert (is_canonical (path2, path2_len));

  /* Skip past common prefix. */
  while (i < min_len && path1[i] == path2[i])
    ++i;

  /* Are the paths exactly the same? */
  if ((path1_len == path2_len) && (i >= min_len))
    return 0;    

  /* Children of paths are greater than their parents, but less than
     greater siblings of their parents. */
  if ((path1[i] == '/') && (path2[i] == 0))
    return 1;
  if ((path2[i] == '/') && (path1[i] == 0))
    return -1;
  if (path1[i] == '/')
    return -1;
  if (path2[i] == '/')
    return 1;

  /* Common prefix was skipped above, next character is compared to
     determine order */
  return path1[i] < path2[i] ? -1 : 1;
}


char *
svn_path_get_longest_ancestor (const char *path1,
                               const char *path2,
                               apr_pool_t *pool)
{
  char *common_path;
  apr_size_t path1_len, path2_len;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;
  
  path1_len = strlen (path1);
  path2_len = strlen (path2);

  if (SVN_PATH_IS_EMPTY (path1) || SVN_PATH_IS_EMPTY (path2))
    return NULL;

  while (path1[i] == path2[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1[i] == '/')
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1_len) || (i == path2_len))
        break;
    }

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1_len) && (path2[i] == '/'))
      || ((i == path2_len) && (path1[i] == '/'))
      || ((i == path1_len) && (i == path2_len)))
    common_path = apr_pstrmemdup (pool, path1, i);
  else
    common_path = apr_pstrmemdup (pool, path1, last_dirsep);

  return common_path;
}


const char *
svn_path_is_child (const char *path1,
                   const char *path2,
                   apr_pool_t *pool)
{
  apr_size_t i;

  /* assert (is_canonical (path1, strlen (path1)));  ### Expensive strlen */
  /* assert (is_canonical (path2, strlen (path2)));  ### Expensive strlen */

  /* Allow "" and "foo" to be parent/child */
  if (SVN_PATH_IS_EMPTY (path1)                /* "" is the parent  */
      &&
      ! (SVN_PATH_IS_EMPTY (path2)             /* "" not a child    */
         || path2[0] == '/'))                  /* "/foo" not a child */
    return apr_pstrdup (pool, path2);

  if (path1[0] != '/' && path2[0] == '/')
    return NULL;

  /* Reach the end of at least one of the paths. */
  for (i = 0; path1[i] && path2[i]; i++)
    {
      if (path1[i] != path2[i])
        return NULL;
    }

  /* Now run through the possibilities. */

  if (path1[i] && (! path2[i]))
    {
      return NULL;
    }
  else if ((! path1[i]) && path2[i])
    {
      if (path2[i] == '/')
        return apr_pstrdup (pool, path2 + i + 1);
      else
        return NULL;
    }
  else  /* both ended */
    {
      return NULL;
    }

  return NULL;
}


apr_array_header_t *
svn_path_decompose (const char *path,
                    apr_pool_t *pool)
{
  apr_size_t i, oldi;

  apr_array_header_t *components = 
    apr_array_make (pool, 1, sizeof(const char *));

  /* assert (is_canonical (path, strlen (path)));  ### Expensive strlen */

  if (SVN_PATH_IS_EMPTY (path))
    return components;  /* ### Should we return a "" component? */

  /* If PATH is absolute, store the '/' as the first component. */
  i = oldi = 0;
  if (path[i] == '/')
    {
      char dirsep = '/';

      *((const char **) apr_array_push (components))
        = apr_pstrmemdup (pool, &dirsep, sizeof (dirsep));

      i++;
      oldi++;
      if (path[i] == '\0') /* path is a single '/' */
        return components;
    }

  do
    {
      if ((path[i] == '/') || (path[i] == '\0'))
        {
          if (SVN_PATH_IS_PLATFORM_EMPTY (path + oldi, i - oldi))
            /* ### Should canonicalization strip "//" and "/./" substrings? */
            *((const char **) apr_array_push (components)) = SVN_EMPTY_PATH;
          else
            *((const char **) apr_array_push (components))
              = apr_pstrmemdup (pool, path + oldi, i - oldi);

          i++;
          oldi = i;  /* skipping past the dirsep */
          continue;
        }
      i++;
    }
  while (path[i-1]);

  return components;
}


svn_boolean_t
svn_path_is_single_path_component (const char *name)
{
  /* assert (is_canonical (name, strlen (name)));  ### Expensive strlen */

  /* Can't be empty or `..'  */
  if (SVN_PATH_IS_EMPTY (name)
      || (name[0] == '.' && name[1] == '.' && name[2] == '\0'))
    return FALSE;

  /* Slashes are bad, m'kay... */
  if (strchr (name, '/') != NULL)
    return FALSE;

  /* It is valid.  */
  return TRUE;
}



/*** URI Stuff ***/


svn_boolean_t 
svn_path_is_url (const char *path)
{
  apr_size_t j;
  apr_size_t len = strlen (path);

  /* ### Taking strlen() initially is inefficient.  It's a holdover
     from svn_stringbuf_t days. */

  /* ### This function is reaaaaaaaaaaaaaally stupid right now.
     We're just going to look for:
 
        (scheme)://(optional_stuff)

     Where (scheme) has no ':' or '/' characters.

     Someday it might be nice to have an actual URI parser here.
  */

  /* Make sure we have enough characters to even compare. */
  if (len < 4)
    return FALSE;

  /* Look for the sequence '://' */
  for (j = 0; j < len - 3; j++)
    {
      /* We hit a '/' before finding the sequence. */
      if (path[j] == '/')
        return FALSE;

      /* Skip stuff up to the first ':'. */
      if (path[j] != ':')
        continue;

      /* Current character is a ':' now.  It better not be the first
         character. */
      if (j == 0)
        return FALSE;

      /* Expecting the next two chars to be '/' */

      if ((path[j + 1] == '/')
          && (path[j + 2] == '/'))
        return TRUE;
      
      return FALSE;
    }
     
  return FALSE;
}



/* Here is the BNF for path components in a URI. "pchar" is a
   character in a path component.

      pchar       = unreserved | escaped | 
                    ":" | "@" | "&" | "=" | "+" | "$" | ","
      unreserved  = alphanum | mark
      mark        = "-" | "_" | "." | "!" | "~" | "*" | "'" | "(" | ")"

   Note that "escaped" doesn't really apply to what users can put in
   their paths, so that really means the set of characters is:

      alphanum | mark | ":" | "@" | "&" | "=" | "+" | "$" | "," 
*/
static const int uri_char_validity[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 1, 0, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 1, 0, 0,

  /* 64 */
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,

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

svn_boolean_t 
svn_path_is_uri_safe (const char *path)
{
  apr_size_t i;

  for (i = 0; path[i]; i++)
    if (! uri_char_validity[((unsigned char)path[i])])
      return FALSE;

  return TRUE;
}
  

const char *
svn_path_uri_encode (const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i, copied = 0;
  int c;

  if (! path)
    return NULL;

  retstr = svn_stringbuf_create ("", pool);
  for (i = 0; path[i]; i++)
    {
      c = (unsigned char)path[i];
      if (uri_char_validity[c])
        continue;

      /* If we got here, we're looking at a character that isn't
         supported by the (or at least, our) URI encoding scheme.  We
         need to escape this character.  */

      /* First things first, copy all the good stuff that we haven't
         yet copied into our output buffer. */
      if (i - copied)
        svn_stringbuf_appendbytes (retstr, path + copied, 
                                   i - copied);
      
      /* Now, sprintf() in our escaped character, making sure our
         buffer is big enough to hold the '%' and two digits.  We cast
         the C to unsigned char here because the 'X' format character
         will be tempted to treat it as an unsigned int...which causes
         problem when messing with 0x80-0xFF chars.  */
      svn_stringbuf_ensure (retstr, retstr->len + 3);
      sprintf (retstr->data + retstr->len, "%%%02X", (unsigned char)c);
      retstr->len += 3;

      /* Finally, update our copy counter. */
      copied = i + 1;
    }

  /* Anything left to copy? */
  if (i - copied)
    svn_stringbuf_appendbytes (retstr, path + copied, i - copied);

  /* Null-terminate this bad-boy. */
  svn_stringbuf_ensure (retstr, retstr->len + 1);
  retstr->data[retstr->len] = 0;

  return retstr->data;
}


const char *
svn_path_uri_decode (const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i;
  svn_boolean_t query_start = FALSE;

  if (! path)
    return NULL;

  retstr = svn_stringbuf_create ("", pool);

  /* avoid repeated realloc */
  svn_stringbuf_ensure (retstr, strlen (path) + 1); 

  retstr->len = 0;
  for (i = 0; path[i]; i++)
    {
      char c = path[i];

      if (c == '?')
        {
          /* Mark the start of the query string, if it exists. */
          query_start = TRUE;
        }
      else if (c == '+' && query_start)
        {
          /* Only do this if we are into the query string.
           * RFC 2396, section 3.3  */
          c = ' ';
        }
      else if (c == '%')
        {
          char digitz[3];
          digitz[0] = path[++i];
          digitz[1] = path[++i];
          digitz[2] = '\0';
          c = (char)(strtol (digitz, NULL, 16));
        }

      retstr->data[retstr->len++] = c;
    }

  /* Null-terminate this bad-boy. */
  retstr->data[retstr->len] = 0;

  return retstr->data;
}


const char *
svn_path_url_add_component (const char *url,
                            const char *component,
                            apr_pool_t *pool)
{
  /* URL can have trailing '/' */
  url = svn_path_canonicalize (url, pool);

  return svn_path_join (url, svn_path_uri_encode (component, pool), pool);
}


svn_error_t *
svn_path_get_absolute(const char **pabsolute,
                      const char *relative,
                      apr_pool_t *pool)
{
  /* We call svn_path_canonicalize() on the input data, rather
     than the output, so that `buffer' can be returned directly
     without const vs non-const issues. */

  char * buffer;
  apr_status_t apr_err;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8
           (&path_native, svn_path_canonicalize (relative, pool), pool));

  apr_err = apr_filepath_merge(&buffer, NULL,
                               path_native,
                               (APR_FILEPATH_NOTRELATIVE
                                | APR_FILEPATH_TRUENAME),
                               pool);

  if (apr_err)
    return svn_error_createf(SVN_ERR_BAD_FILENAME, apr_err, NULL,
                             "Couldn't determine absolute path of %s.", 
                             relative);

  return svn_utf_cstring_to_utf8 (pabsolute,
                                  svn_path_canonicalize (buffer, pool),
                                  NULL, pool);
}


svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory,
                       const char **pfile,
                       apr_pool_t *pool)
{
  apr_finfo_t finfo;
  svn_error_t *err;

  /* assert (is_canonical (path, strlen (path)));  ### Expensive strlen */

  err = svn_io_stat(&finfo, path, APR_FINFO_TYPE, pool);
  if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
    return err;

  if (err || finfo.filetype == APR_REG)
    {
      if (err)
        svn_error_clear (err);
      svn_path_split(path, pdirectory, pfile, pool);
    }
  else if (finfo.filetype == APR_DIR)
    {
      *pdirectory = path;
      *pfile = SVN_EMPTY_PATH;
    }
  else 
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, 0, NULL,
                               "%s is neither a file nor a directory name.",
                               path);
    }

  return SVN_NO_ERROR;
}
