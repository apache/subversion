/*
 * paths.c:   a path manipulation library using svn_stringbuf_t
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
      if (path->data[path->len - 1] == SVN_PATH_SEPARATOR)
          path->data[--path->len] = '\0';
    }
}


svn_stringbuf_t *
svn_path_last_component (const svn_stringbuf_t *path,
                         apr_pool_t *pool)
{
  apr_size_t i = svn_stringbuf_find_char_backward (path, SVN_PATH_SEPARATOR);

  if (i < path->len)
    {
      i += 1;  /* Get past the separator char. */
      return svn_stringbuf_ncreate (path->data + i, (path->len - i), pool);
    }
  else
    return svn_stringbuf_dup (path, pool);
}



void
svn_path_split (const svn_stringbuf_t *path, 
                svn_stringbuf_t **dirpath,
                svn_stringbuf_t **basename,
                apr_pool_t *pool)
{
  svn_stringbuf_t *n_dirpath, *n_basename = NULL;

  assert (dirpath != basename);

  n_dirpath = svn_stringbuf_dup (path, pool);
  svn_path_canonicalize (n_dirpath);

  if (basename)
    n_basename = svn_path_last_component (n_dirpath, pool);

  if (dirpath)
    svn_path_remove_component (n_dirpath);

  if (dirpath)
    *dirpath = n_dirpath;

  if (basename)
    *basename = n_basename;
}


int
svn_path_is_thisdir (const svn_stringbuf_t *path)
{
  if ((path->len == 1) && (path->data[0] == '.'))
    return 1;

  if ((path->len == 2) && (path->data[0] == '.')
      && path->data[1] == SVN_PATH_SEPARATOR)
    return 1;

  return 0;
}


int
svn_path_is_empty (const svn_stringbuf_t *path)
{
  return ((path == NULL)
          || (svn_stringbuf_isempty (path))
          || (svn_path_is_thisdir (path)));
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

  if ((path1->len == path2->len) && (i >= min_len))
    return 0;     /* the paths are the same */
  if (path1->data[i] == SVN_PATH_SEPARATOR)
    return 1;     /* path1 child of path2, parent always comes before child */
  if (path2->data[i] == SVN_PATH_SEPARATOR)
    return -1;    /* path2 child of path1, parent always comes before child */

  /* Common prefix was skipped above, next character is compared to
     determine order */
  return path1->data[i] < path2->data[i] ? -1 : 1;
}



svn_stringbuf_t *
svn_path_get_longest_ancestor (const svn_stringbuf_t *path1,
                               const svn_stringbuf_t *path2,
                               apr_pool_t *pool)
{
  svn_stringbuf_t *common_path;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;

  /* If either string is NULL or empty, we must go no further. */
  
  if ((! path1) || (! path2)
      || (svn_stringbuf_isempty (path1)) || (svn_stringbuf_isempty (path2)))
    return NULL;
  
  while (path1->data[i] == path2->data[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1->data[i] == SVN_PATH_SEPARATOR)
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1->len) || (i == path2->len))
        break;
    }

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1->len) && (path2->data[i] == SVN_PATH_SEPARATOR))
      || ((i == path2->len) && (path1->data[i] == SVN_PATH_SEPARATOR))
      || ((i == path1->len) && (i == path2->len)))
    common_path = svn_stringbuf_ncreate (path1->data, i, pool);
  else
    common_path = svn_stringbuf_ncreate (path1->data, last_dirsep, pool);
    
  svn_path_canonicalize (common_path);

  return common_path;
}


/* Test if PATH2 is a child of PATH1.

   If not, return NULL.
   If so, return the "remainder" path.  (The substring which, when
   appended to PATH1, yields PATH2.) */
svn_stringbuf_t *
svn_path_is_child (const svn_stringbuf_t *path1,
                   const svn_stringbuf_t *path2,
                   apr_pool_t *pool)
{
  apr_size_t i = 0;
      
  /* If either path is empty, return NULL. */
  if ((! path1) || (! path2)
      || (svn_stringbuf_isempty (path1)) || (svn_stringbuf_isempty (path2)))
    return NULL;
  
  /* If path2 isn't longer than path1, return NULL.  */
  if (path2->len <= path1->len)
    return NULL;

  /* Run through path1, and if it ever differs from path2, return
     NULL. */
  while (i < path1->len)
    {
      if (path1->data[i] != path2->data[i])
        return NULL;

      i++;
    }

  /* If we get all the way to the end of path1 with the contents the
     same as in path2, and either path1 ends in a directory separator,
     or path2's next character is a directory separator followed by
     more pathy stuff, then path2 is a child of path1. */
  if (i == path1->len)
    {
      if (path1->data[i - 1] == SVN_PATH_SEPARATOR)
        return svn_stringbuf_ncreate (path2->data + i,
                                      path2->len - i,
                                      pool);
      else if (path2->data[i] == SVN_PATH_SEPARATOR)
        return svn_stringbuf_ncreate (path2->data + i + 1,
                                      path2->len - i - 1,
                                      pool);
    }

  return NULL;
}


/* helper for svn_path_decompose, because apr arrays are so darn ugly. */
static void
store_component (apr_array_header_t *array,
                 const char *bytes,
                 apr_size_t len,
                 apr_pool_t *pool)
{
  svn_stringbuf_t **receiver;
  
  svn_stringbuf_t *component = svn_stringbuf_ncreate (bytes, len, pool);

  receiver = (svn_stringbuf_t **) apr_array_push (array);
  *receiver = component;
}


apr_array_header_t *
svn_path_decompose (const svn_stringbuf_t *path,
                    apr_pool_t *pool)
{
  apr_size_t i, oldi;

  apr_array_header_t *components = 
    apr_array_make (pool, 1, sizeof(svn_stringbuf_t *));

  i = oldi = 0;

  if (svn_path_is_empty (path))
    return components;

  /* If PATH is absolute, store the '/' as the first component. */
  if (path->data[i] == SVN_PATH_SEPARATOR)
    {
      char dirsep = SVN_PATH_SEPARATOR;
      store_component (components, &dirsep, sizeof (dirsep), pool);
      i++;
      oldi++;
    }

  while (i <= path->len)
    {
      if ((path->data[i] == SVN_PATH_SEPARATOR) || (path->data[i] == '\0'))
        {
          store_component (components, path->data + oldi, i - oldi, pool);
          i++;
          oldi = i;  /* skipping past the dirsep */
          continue;
        }
      i++;
    }

  return components;
}


svn_boolean_t
svn_path_is_single_path_component (svn_stringbuf_t *path)
{
  /* Can't be NULL */
  if (! path)
    return 0;

  /* Can't be empty */
  if (*path->data == '\0')
    return 0;

  /* Can't be `.' or `..' */
  if (path->data[0] == '.'
      && (path->data[1] == '\0'
          || (path->data[1] == '.' && path->data[2] == '\0')))
    return 0;

  /* slashes are bad, m'kay... */
  if (strchr (path->data, SVN_PATH_SEPARATOR) != NULL)
    return 0;

  /* it is valid */
  return 1;
}



/*** URI Stuff ***/


svn_boolean_t 
svn_path_is_url (const svn_string_t *path)
{
  apr_size_t j;

  /* ### This function is reaaaaaaaaaaaaaally stupid right now.
     We're just going to look for:
 
        (scheme)://(optional_servername)/(optional_stuff)

     Where (scheme) has no ':' or '/' characters.

     Someday it might be nice to have an actual URI parser here.
  */

  /* Make sure we have enough characters to even compare. */
  if (path->len < 5)
    return FALSE;

  /* Look for the sequence '://' */
  for (j = 0; j < path->len - 3; j++)
    {
      /* We hit a '/' before finding the sequence. */
      if (path->data[j] == '/')
        return FALSE;

      /* Skip stuff up to the first ':'. */
      if (path->data[j] != ':')
        continue;

      /* Current character is a ':' now.  It better not be the first
         character. */
      if (j == 0)
        return FALSE;

      /* Expecting the next two chars to be '/', and somewhere
         thereafter another '/'. */
      if ((path->data[j + 1] == '/')
          && (path->data[j + 2] == '/')
          && (strchr (path->data + j + 3, '/') != NULL))
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
svn_path_is_uri_safe (const svn_string_t *path)
{
  apr_size_t i;

  for (i = 0; i < path->len; i++)
    if (! char_is_uri_safe (path->data[i]))
      return FALSE;

  return TRUE;
}
  

svn_stringbuf_t *
svn_path_uri_encode (const svn_string_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i;
  int copied = 0;
  char c;

  if ((! path) || (! path->data))
    return NULL;

  retstr = svn_stringbuf_create ("", pool);
  for (i = 0; i < path->len; i++)
    {
      c = path->data[i];
      if (char_is_uri_safe (c))
        continue;

      /* If we got here, we're looking at a character that isn't
         supported by the (or at least, our) URI encoding scheme.  We
         need to escape this character.  */

      /* First things first, copy all the good stuff that we haven't
         yet copied into our output buffer. */
      if (i - copied)
        svn_stringbuf_appendbytes (retstr, path->data + copied, 
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
    svn_stringbuf_appendbytes (retstr, path->data + copied, i - copied);

  /* Null-terminate this bad-boy. */
  svn_stringbuf_ensure (retstr, retstr->len + 1);
  retstr->data[retstr->len] = 0;

  return retstr;
}


svn_stringbuf_t *
svn_path_uri_decode (const svn_string_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i;

  if ((! path) || (! path->data))
    return NULL;

  retstr = svn_stringbuf_create ("", pool);
  svn_stringbuf_ensure (retstr, path->len);
  retstr->len = 0;
  for (i = 0; i < path->len; i++)
    {
      char c = path->data[i];
      if (c == '+') /* _encode() doesn't do this, but it's easy...whatever. */
        c = ' ';
      else if (c == '%')
        {
          char digitz[3];
          digitz[0] = path->data[++i];
          digitz[1] = path->data[++i];
          digitz[2] = '\0';
          c = (char)(strtol (digitz, NULL, 16));
        }

      retstr->data[retstr->len++] = c;
    }

  /* Null-terminate this bad-boy. */
  retstr->data[retstr->len] = 0;

  return retstr;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
