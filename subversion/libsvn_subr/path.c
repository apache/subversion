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
#include "svn_string.h"
#include "svn_path.h"
#include "svn_private_config.h"         /* for SVN_PATH_LOCAL_SEPARATOR */
#include "svn_utf.h"
#include "apr_file_info.h"


/* todo: Though we have a notion of different types of separators for
 * the local path style, there currently is no logic in place to
 * account for cases where the separator for one system is a valid
 * non-separator character for others.  For example, a backslash (\)
 * character is a legal member of a Unix filename, but is the
 * separator character for Windows platforms (the *file* foo\bar.c on
 * Unix machine runs the risk of being interpreted by a Windows box as
 * file bar.c in a directory foo). */

/* kff todo: hey, it looks like APR may handle some parts of path
   portability for us, and we just get to use `/' everywhere.  Check
   up on this. */

/* Working on exactly that --xbc */

/* Path separator defines. */
/* SVN_PATH_LOCAL_SEPARATOR (the local filesystem path separator)
   _should_ have been defined external this file by the build stuffs */
#define SVN_PATH_SEPARATOR  '/' /* internal path separator */



void
svn_path_internal_style (svn_stringbuf_t *path)
{
  if (SVN_PATH_SEPARATOR != SVN_PATH_LOCAL_SEPARATOR)
    {
      /* Convert all local-style separators to the canonical ones. */
      char *p;
      for (p = path->data; *p != '\0'; ++p)
        if (*p == SVN_PATH_LOCAL_SEPARATOR)
          *p = SVN_PATH_SEPARATOR;
    }

  svn_path_canonicalize (path);
  /* FIXME: Should also remove trailing /.'s, if the style says so. */
}


void
svn_path_local_style (svn_stringbuf_t *path)
{
  svn_path_canonicalize (path);
  /* FIXME: Should also remove trailing /.'s, if the style says so. */

  if (SVN_PATH_SEPARATOR != SVN_PATH_LOCAL_SEPARATOR)
    {
      /* Convert all canonical separators to the local-style ones. */
      char *p;
      for (p = path->data; *p != '\0'; ++p)
        if (*p == SVN_PATH_SEPARATOR)
          *p = SVN_PATH_LOCAL_SEPARATOR;
    }
}



void
svn_path_canonicalize (svn_stringbuf_t *path)
{
  /* At some point this could eliminate redundant components.
     For now, it just makes sure there is no trailing slash. */

  /* kff todo: maybe should be implemented with a new routine in
     libsvn_string. */

  /* Remove trailing separators from the end of the path. */
  while ((path->len > 0)
         && path->data[(path->len - 1)] == SVN_PATH_SEPARATOR)
    {
      path->data[(path->len - 1)] = '\0';
      path->len--;
    }
}


const char *
svn_path_canonicalize_nts (const char *path, apr_pool_t *pool)
{
  /* At some point this could eliminate redundant components.
     For now, it just makes sure there is no trailing slash. */

  apr_size_t len = strlen (path);
  apr_size_t orig_len = len;

  /* Remove trailing separators from the end of the path. */
  while ((len > 0) && (path[len - 1] == SVN_PATH_SEPARATOR))
     len--;

  if (len != orig_len)
    return apr_pstrndup (pool, path, len);
  else
    return path;
}



char *svn_path_join (const char *base,
                     const char *component,
                     apr_pool_t *pool)
{
  apr_size_t blen = strlen (base);
  apr_size_t clen = strlen (component);
  char *path;

  /* If either component is the empty string, copy and return the other.
     If the component is absolute, then return it.  */
  if (blen == 0 || *component == '/')
    return apr_pmemdup (pool, component, clen + 1);
  if (clen == 0)
    return apr_pmemdup (pool, base, blen + 1);

  /* If the base ends with a slash, then don't copy it.

     Note: we don't account for multiple trailing slashes. Callers should
     pass reasonably-normalized paths.  */
  if (base[blen - 1] == '/')
    --blen;

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
  svn_boolean_t base_is_root = 0;
  int base_arg = 0;

  total_len = strlen (base);
  if (total_len == 0)
    {
      /* if the base is empty, then skip it */
      base_arg = 1;
    }
  else if (base[total_len - 1] == '/')
    {
      if (total_len == 1)
        base_is_root = 1;
      else
        --total_len;
    }
  saved_lengths[0] = total_len;

  /* Compute the length of the resulting string. */

  nargs = 0;
  va_start (va, base);
  while ((s = va_arg (va, const char *)) != NULL)
    {
      len = strlen (s);

      if (len > 1 && s[len - 1] == '/')
        --len;
      if (nargs++ < MAX_SAVED_LENGTHS)
        saved_lengths[nargs] = len;

      /* if this component isn't being added, then continue so we don't
         count an additional separator. */
      if (!len)
        {
          /* if we have not added anything yet, then skip this argument */
          if (total_len == 0)
            base_arg = nargs + 1;
          continue;
        }

      if (*s == '/')
        {
          /* an absolute path. skip all components to this point and reset
             the total length. */
          total_len = len;
          base_arg = nargs;
          base_is_root = len == 1;
        }
      else if (nargs == base_arg || (nargs == base_arg + 1 && base_is_root))
        {
          /* if we have skipped everything up to this arg, then the base
             and all prior components are empty. just set the length to
             this component; do not add a separator.

             if the base is the root ("/"), then do not add a separator.
          */
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

  /* if we aren't supposed to skip forward to an absolute component, then
     copy the base into the output. */
  if (base_arg == 0)
    {
      memcpy(p, base, len = saved_lengths[0]);
      p += len;
    }

  nargs = 0;
  va_start (va, base);
  while ((s = va_arg (va, const char *)) != NULL)
    {
      if (++nargs < base_arg)
        continue;

      if (nargs < MAX_SAVED_LENGTHS)
        len = saved_lengths[nargs];
      else
        {
          len = strlen (s);
          if (len > 1 && s[len - 1] == '/')
            --len;
        }
      if (!len)
        continue;

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



static void
add_component_internal (svn_stringbuf_t *path,
                        const char *component,
                        size_t len)
{
  /* Check if we're trying to add a trailing "." */
  /* FIXME: *Should* we remove trailing /.'s from internal paths, or not? */
  /*
  if (ctx->strip_slashdot
      && len == 1 && component[0] == '.')
    return;
  */

  /* Append a dir separator, but only if this path is neither empty
     nor consists of a single dir separator already. */
  if ((! svn_stringbuf_isempty (path))
      && (! ((path->len == 1) && (*(path->data) == SVN_PATH_SEPARATOR))))
    {
      char dirsep = SVN_PATH_SEPARATOR;
      svn_stringbuf_appendbytes (path, &dirsep, sizeof (dirsep));
    }

  svn_stringbuf_appendbytes (path, component, len);
  svn_path_canonicalize (path);
}


void
svn_path_add_component_nts (svn_stringbuf_t *path, 
                            const char *component)
{
  add_component_internal (path, component, strlen (component));
}


void
svn_path_add_component (svn_stringbuf_t *path, 
                        const svn_stringbuf_t *component)
{
  add_component_internal (path, component->data, component->len);
}


void
svn_path_remove_component (svn_stringbuf_t *path)
{
  svn_path_canonicalize (path);

  if (! svn_stringbuf_chop_back_to_char (path, SVN_PATH_SEPARATOR))
    svn_stringbuf_setempty (path);
  else
    {
      if (path->len && path->data[path->len - 1] == SVN_PATH_SEPARATOR)
          path->data[--path->len] = '\0';
    }
}


char *
svn_path_remove_component_nts (const char *path, apr_pool_t *pool)
{
  /* ### We could chop away at the return value directly here.  Not
     sure that slight efficiency improvement is worth it, though. */

  const char *newpath = svn_path_canonicalize_nts (path, pool);
  int i;

  for (i = (strlen(newpath) - 1); i >= 0; i--)
    {
      if (path[i] == SVN_PATH_SEPARATOR)
        break;
    }

  return apr_pstrndup (pool, path, (i < 0) ? 0 : i);
}


char *
svn_path_basename (const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen (path);
  const char *p;

  /* if an empty is passed, then return an empty string */
  if (len == 0)
    return apr_pcalloc (pool, 1);

  /* "remove" trailing slashes */
  while (len && path[len - 1] == '/')
    --len;

  /* if there is nothing left, then the path was 'root' (possibly as
     multiple slashes). just return a copy of "/".  */
  if (len == 0)
    return apr_pmemdup (pool, "/", 2);

  /* back up to find the previous slash character.

     note that p can actually end up at (path-1), but we make sure to not
     deref that (the location may not be mapped; it is even possible that
     some systems cannot compute path-1, but I don't know any).

     the point is that we have to distinguish between stopping the loop
     at *p == '/' or stopping because we hit the start of the string. it
     is easiest to say we stop "one character before the start of the
     resulting basename."  */
  p = path + len - 1;
  while (p >= path && *p != '/')
    --p;

  /* copy the substring and null-terminate it */
  return apr_pstrmemdup (pool, p + 1, len - 1 - (p - path));
}



void
svn_path_split (const svn_stringbuf_t *path, 
                svn_stringbuf_t **dirpath,
                svn_stringbuf_t **base_name,
                apr_pool_t *pool)
{
  assert (dirpath != base_name);

  if (dirpath)
    {
      svn_stringbuf_t *n_dirpath;

      n_dirpath = svn_stringbuf_dup (path, pool);
      svn_path_canonicalize (n_dirpath);
      svn_path_remove_component (n_dirpath);

      *dirpath = n_dirpath;
    }

  if (base_name)
    {
      *base_name =
        svn_stringbuf_create (svn_path_basename (path->data, pool), pool);
    }
}



void
svn_path_split_nts (const char *path,
                    const char **dirpath,
                    const char **base_name,
                    apr_pool_t *pool)
{
  assert (dirpath != base_name);

  if (dirpath)
    *dirpath = svn_path_remove_component_nts (path, pool);

  if (base_name)
    *base_name = svn_path_basename (path, pool);
}


int
svn_path_is_empty (const svn_stringbuf_t *path)
{
  if (path == NULL || svn_stringbuf_isempty (path))
    return 1;

  if ((path->len == 1) && (path->data[0] == '.'))
    return 1;

  if ((path->len == 2) && (path->data[0] == '.')
      && path->data[1] == SVN_PATH_SEPARATOR)
    return 1;

  return 0;
}


int
svn_path_is_empty_nts (const char *path)
{
  if (path == NULL)
    return 1;

  if ((path[0] == '\0')
      || ((path[0] == '.') && ((path[1] == '\0')
                               || ((path[1] == SVN_PATH_SEPARATOR)
                                   && (path[2] == '\0')))))
    return 1;

  return 0;
}


int
svn_path_compare_paths (const svn_stringbuf_t *path1,
                        const svn_stringbuf_t *path2)
{
  apr_size_t min_len = ((path1->len < path2->len) ? path1->len : path2->len);
  apr_size_t i = 0;
  
  /* Skip past common prefix. */
  while (i < min_len && path1->data[i] == path2->data[i])
    ++i;

  /* Are the paths exactly the same? */
  if ((path1->len == path2->len) && (i >= min_len))
    return 0;    

  /* Children of paths are greater than their parents, but less than
     greater siblings of their parents. */
  if ((path1->data[i] == SVN_PATH_SEPARATOR) && (path2->data[i] == 0))
    return 1;
  if ((path2->data[i] == SVN_PATH_SEPARATOR) && (path1->data[i] == 0))
    return -1;
  if (path1->data[i] == SVN_PATH_SEPARATOR)
    return -1;
  if (path2->data[i] == SVN_PATH_SEPARATOR)
    return 1;

  /* Common prefix was skipped above, next character is compared to
     determine order */
  return path1->data[i] < path2->data[i] ? -1 : 1;
}


int
svn_path_compare_paths_nts (const char *path1,
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
  
  /* Skip past common prefix. */
  while (i < min_len && path1[i] == path2[i])
    ++i;

  /* Are the paths exactly the same? */
  if ((path1_len == path2_len) && (i >= min_len))
    return 0;    

  /* Children of paths are greater than their parents, but less than
     greater siblings of their parents. */
  if ((path1[i] == SVN_PATH_SEPARATOR) && (path2[i] == 0))
    return 1;
  if ((path2[i] == SVN_PATH_SEPARATOR) && (path1[i] == 0))
    return -1;
  if (path1[i] == SVN_PATH_SEPARATOR)
    return -1;
  if (path2[i] == SVN_PATH_SEPARATOR)
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
  const char *common_path;
  apr_size_t path1_len, path2_len;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;
  
  /* If either string is NULL or empty, we need go no further. */
  if ((! path1) || (! path2) || (path1[0] == '\0') || (path2[0] == '\0'))
    return NULL;
  
  path1_len = strlen (path1);
  path2_len = strlen (path2);

  while (path1[i] == path2[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1[i] == SVN_PATH_SEPARATOR)
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1_len) || (i == path2_len))
        break;
    }

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1_len) && (path2[i] == SVN_PATH_SEPARATOR))
      || ((i == path2_len) && (path1[i] == SVN_PATH_SEPARATOR))
      || ((i == path1_len) && (i == path2_len)))
    common_path = apr_pstrndup (pool, path1, i);
  else
    common_path = apr_pstrndup (pool, path1, last_dirsep);
    
  /* ### If svn_path_canonicalize_nts() didn't return const, we could
     lose this pitiful double allocation.  But that's a bit tricky,
     so leaving the pity for now. */
  return apr_pstrdup (pool, svn_path_canonicalize_nts (common_path, pool));
}


const char *
svn_path_is_child (const char *path1,
                   const char *path2,
                   apr_pool_t *pool)
{
  apr_size_t i;

  /* If either path is empty, return NULL. */
  if ((! path1) || (! path2) || (path1[0] == '\0') || (path2[0] == '\0'))
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
      /* ### Since we "know" path2 is in canonical form, the second
         and third clauses are technically unnecessary.  But I'd like
         to inspect callers before getting rid of this... */
      if ((path2[i] == SVN_PATH_SEPARATOR)
          && (path2[i + 1] != SVN_PATH_SEPARATOR)
          && (path2[i + 1] != '\0'))
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

  if (svn_path_is_empty_nts (path))
    return components;

  /* If PATH is absolute, store the '/' as the first component. */
  i = oldi = 0;
  if (path[i] == SVN_PATH_SEPARATOR)
    {
      char dirsep = SVN_PATH_SEPARATOR;

      *((const char **) apr_array_push (components))
        = apr_pstrndup (pool, &dirsep, sizeof (dirsep));

      i++;
      oldi++;
      if (path[i] == '\0') /* path is a single '/' */
        return components;
    }

  do
    {
      if ((path[i] == SVN_PATH_SEPARATOR) || (path[i] == '\0'))
        {
          *((const char **) apr_array_push (components))
            = apr_pstrndup (pool, path + oldi, i - oldi);

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
  /* Can't be empty.  */
  if (! (name && *name))
    return 0;

  /* Can't be `.' or `..'  */
  if (name[0] == '.'
      && (name[1] == '\0'
          || (name[1] == '.' && name[2] == '\0')))
    return 0;

  /* Slashes are bad, m'kay... */
  if (strchr (name, '/') != NULL)
    return 0;

  /* It is valid.  */
  return 1;
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
 
        (scheme)://(optional_servername)/(optional_stuff)

     Where (scheme) has no ':' or '/' characters.

     Someday it might be nice to have an actual URI parser here.
  */

  /* Make sure we have enough characters to even compare. */
  if (len < 5)
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

      /* Expecting the next two chars to be '/', and somewhere
         thereafter another '/'. */
      if ((path[j + 1] == '/')
          && (path[j + 2] == '/')
          && (strchr (path + j + 3, '/') != NULL))
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
static svn_boolean_t
char_is_uri_safe (char c)
{
  /* Is this an alphanumeric character? */
  if (((c >= 'A') && (c <='Z'))
      || ((c >= 'a') && (c <='z'))
      || ((c >= '0') && (c <='9')))
    return TRUE;

  /* Is this a supported non-alphanumeric character? (these are sorted
     by estimated usage, most-to-least commonly used) */
  if (strchr ("/:.-_!~'()@=+$,&*", c) != NULL)
    return TRUE;

  return FALSE;
}


svn_boolean_t 
svn_path_is_uri_safe (const char *path)
{
  apr_size_t i;

  for (i = 0; path[i]; i++)
    if (! char_is_uri_safe (path[i]))
      return FALSE;

  return TRUE;
}
  

const char *
svn_path_uri_encode (const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i, copied = 0;
  char c;

  if (! path)
    return NULL;

  retstr = svn_stringbuf_create ("", pool);
  for (i = 0; path[i]; i++)
    {
      c = path[i];
      if (char_is_uri_safe (c))
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
         buffer is big enough to hold the '%' and two digits. */
      svn_stringbuf_ensure (retstr, retstr->len + 3);
      sprintf (retstr->data + retstr->len, "%%%02X", c);
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


svn_error_t *
svn_path_get_absolute(const char **pabsolute,
                      const char *relative,
                      apr_pool_t *pool)
{
  /* We call svn_path_canonicalize_nts() on the input data, rather
     than the output, so that `buffer' can be returned directly
     without const vs non-const issues. */

  char * buffer;
  apr_status_t apr_err;
  const char *path_native;

  SVN_ERR (svn_utf_cstring_from_utf8
           (svn_path_canonicalize_nts (relative, pool), &path_native, pool));

  apr_err = apr_filepath_merge(&buffer, NULL,
                               path_native,
                               (APR_FILEPATH_NOTRELATIVE
                                | APR_FILEPATH_TRUENAME),
                               pool);

  if (apr_err)
    return svn_error_createf(SVN_ERR_BAD_FILENAME, apr_err, NULL, pool,
                             "Couldn't determine absolute path of %s.", 
                             relative);

  return svn_utf_cstring_to_utf8 (buffer, pabsolute, pool);
}


svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory,
                       const char **pfile,
                       apr_pool_t *pool)
{
  apr_finfo_t finfo;
  svn_error_t *err;

  err = svn_io_stat(&finfo, path, APR_FINFO_TYPE, pool);

  if (err != SVN_NO_ERROR)
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, 0, err, pool,
                               "Couldn't determine if %s was "
                               "a file or directory.",
                               path);
    }
  else
    {
      if (finfo.filetype == APR_DIR)
        {
          *pdirectory = path;
          *pfile = "";
        }
      else if (finfo.filetype == APR_REG)
        {
          svn_path_split_nts(path, pdirectory, pfile, pool);
        }
      else 
        {
          return svn_error_createf(SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                                  "%s is neither a file nor a directory name.",
                                  path);
        }
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
