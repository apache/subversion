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

/* Path separator defines. */
/* SVN_PATH_LOCAL_SEPARATOR (the local filesystem path separator)
   _should_ have been defined external this file by the build stuffs */
#define SVN_PATH_REPOS_SEPARATOR '/' /* repository separators */
#define SVN_PATH_URL_SEPARATOR   '/' /* url separators */


typedef struct path_style_context_t
{
  /* The path separator used by this style. */
  char dirsep;

  /* An alternate separator which is accepted, but converted to the
     primary separator. Should be 0 if unused. */
  char alt_dirsep;

  /* Whether the trailing "/." in a path should be stripped. This is
     necessary, e.g., on Win32, where some API functions barf if the
     "/." is present. */
  svn_boolean_t strip_slashdot;
} path_style_context_t;


static const path_style_context_t *
get_path_style_context (enum svn_path_style style)
{
  static const path_style_context_t local_style = {
    SVN_PATH_LOCAL_SEPARATOR,
    SVN_PATH_ALTERNATE_SEPARATOR,
    SVN_PATH_STRIP_TRAILING_SLASHDOT
  };

  /* FIXME: Should we strip trailing /.'s in repos and url paths? */
  static const path_style_context_t repos_style = {
    SVN_PATH_REPOS_SEPARATOR,
    0,                          /* alt_dirsep */
    0                           /* strip_slashdot */
  };
  static const path_style_context_t url_style = {
    SVN_PATH_URL_SEPARATOR,
    0,                          /* alt_dirsep */
    0                           /* strip_slashdot */
  };

  switch (style)
    {
    case svn_path_local_style:
      /* local style - used by local filesystem */
      return &local_style;

    case svn_path_url_style:
      /* url style - used in urls */
      return &url_style;

    case svn_path_repos_style:
      /* repos style - used in repository paths */
      return &repos_style;

    default:
      /* default case - error (we should never hit this...) */
      assert (!"Our paths are too stylish.");
      return NULL;
    }
}
 

/* Check if CH is a directory separator in CTX */
static APR_INLINE int
char_is_dirsep (char ch, const path_style_context_t *ctx)
{
  return (ch == ctx->dirsep
          || (ctx->alt_dirsep && ch == ctx->alt_dirsep));
}



void
svn_path_canonicalize (svn_stringbuf_t *path, enum svn_path_style style)
{
  const path_style_context_t *ctx = get_path_style_context (style);

  /* At some point this could eliminiate redundant components.
     For now, it just makes sure there is no trailing slash. */

  /* kff todo: maybe should be implemented with a new routine in
     libsvn_string. */

  /* Convert alternate separators in the path to primary separators. */
  if (ctx->alt_dirsep)
    {
      char *p;
      for (p = path->data; *p != '\0'; ++p)
        if (*p == ctx->alt_dirsep)
          *p = ctx->dirsep;
    }

  /* FIXME: Should also remove trailing /.'s, if the style says so. */
  /* Remove trailing separators from the end of the path. */
  while ((path->len > 0)
         && path->data[(path->len - 1)] == ctx->dirsep)
    {
      path->data[(path->len - 1)] = '\0';
      path->len--;
    }
}


static void
add_component_internal (svn_stringbuf_t *path,
                        const char *component,
                        size_t len,
                        enum svn_path_style style)
{
  const path_style_context_t *ctx = get_path_style_context (style);

  /* Check if we're trying to add a trailing "." */
  if (ctx->strip_slashdot
      && len == 1 && component[0] == '.')
    return;

  if (! svn_stringbuf_isempty (path))
    svn_stringbuf_appendbytes (path, &ctx->dirsep, sizeof (ctx->dirsep));

  svn_stringbuf_appendbytes (path, component, len);
  svn_path_canonicalize (path, style);
}


void
svn_path_add_component_nts (svn_stringbuf_t *path, 
                            const char *component,
                            enum svn_path_style style)
{
  add_component_internal (path, component, strlen (component), style);
}


void
svn_path_add_component (svn_stringbuf_t *path, 
                        const svn_stringbuf_t *component,
                        enum svn_path_style style)
{
  add_component_internal (path, component->data, component->len, style);
}


void
svn_path_remove_component (svn_stringbuf_t *path, enum svn_path_style style)
{
  const path_style_context_t *ctx = get_path_style_context (style);

  svn_path_canonicalize (path, style);

  if (! svn_stringbuf_chop_back_to_char (path, ctx->dirsep))
    svn_stringbuf_setempty (path);
  else
    {
      if (path->data[path->len - 1] == ctx->dirsep)
          path->data[--path->len] = '\0';
    }
}


svn_stringbuf_t *
svn_path_last_component (const svn_stringbuf_t *path,
                         enum svn_path_style style,
                         apr_pool_t *pool)
{
  const path_style_context_t *ctx = get_path_style_context (style);
  apr_size_t i = svn_stringbuf_find_char_backward (path, ctx->dirsep);

  if (ctx->alt_dirsep)
    {
      /* Must check if we skipped an alternate separator. */
      apr_size_t alt_i =
        svn_stringbuf_find_char_backward (path, ctx->alt_dirsep);
      if (i < alt_i && alt_i < path->len)
        i = alt_i;
    }

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
                enum svn_path_style style,
                apr_pool_t *pool)
{
  svn_stringbuf_t *n_dirpath, *n_basename;

  assert (dirpath != basename);

  n_dirpath = svn_stringbuf_dup (path, pool);
  svn_path_canonicalize (n_dirpath, style);

  if (basename)
    n_basename = svn_path_last_component (n_dirpath, style, pool);

  if (dirpath)
    svn_path_remove_component (n_dirpath, style);

  if (dirpath)
    *dirpath = n_dirpath;

  if (basename)
    *basename = n_basename;
}


int
svn_path_is_thisdir (const svn_stringbuf_t *path, enum svn_path_style style)
{
  const path_style_context_t *ctx = get_path_style_context (style);

  if ((path->len == 1) && (path->data[0] == '.'))
    return 1;

  if ((path->len == 2) && (path->data[0] == '.')
      && char_is_dirsep (path->data[1], ctx))
    return 1;

  return 0;
}


int
svn_path_is_empty (const svn_stringbuf_t *path, enum svn_path_style style)
{
  return ((path == NULL)
          || (svn_stringbuf_isempty (path))
          || (svn_path_is_thisdir (path, style)));
}


int
svn_path_compare_paths (const svn_stringbuf_t *path1,
                        const svn_stringbuf_t *path2,
                        enum svn_path_style style)
{
  const path_style_context_t *ctx = get_path_style_context (style);
  apr_size_t min_len = ((path1->len < path2->len) ? path1->len : path2->len);
  apr_size_t i = 0;
  
  /* Skip past common prefix. */
  while (i < min_len
         && (path1->data[i] == path2->data[i]
             || (char_is_dirsep (path1->data[i], ctx)
                 && char_is_dirsep (path2->data[i], ctx))))
    ++i;

  if ((path1->len == path2->len) && (i >= min_len))
    return 0;     /* the paths are the same */
  if (char_is_dirsep (path1->data[i], ctx))
    return 1;     /* path1 child of path2, parent always comes before child */
  if (char_is_dirsep (path2->data[i], ctx))
    return -1;    /* path2 child of path1, parent always comes before child */

  /* Common prefix was skipped above, next character is compared to
     determine order */
  return path1->data[i] < path2->data[i] ? -1 : 1;
}



svn_stringbuf_t *
svn_path_get_longest_ancestor (const svn_stringbuf_t *path1,
                               const svn_stringbuf_t *path2,
                               enum svn_path_style style,
                               apr_pool_t *pool)
{
  const path_style_context_t *ctx = get_path_style_context (style);
  svn_stringbuf_t *common_path;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;

  /* If either string is NULL or empty, we must go no further. */
  
  if ((! path1) || (! path2)
      || (svn_stringbuf_isempty (path1)) || (svn_stringbuf_isempty (path2)))
    return NULL;
  
  while (path1->data[i] == path2->data[i]
         || (char_is_dirsep (path1->data[i], ctx)
             && char_is_dirsep (path2->data[i], ctx)))
    {
      /* Keep track of the last directory separator we hit. */
      if (char_is_dirsep (path1->data[i], ctx))
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1->len) || (i == path2->len))
        break;
    }

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1->len) && char_is_dirsep (path2->data[i], ctx))
      || ((i == path2->len) && char_is_dirsep (path1->data[i], ctx))
      || ((i == path1->len) && (i == path2->len)))
    common_path = svn_stringbuf_ncreate (path1->data, i, pool);
  else
    common_path = svn_stringbuf_ncreate (path1->data, last_dirsep, pool);
    
  svn_path_canonicalize (common_path, style);

  return common_path;
}


/* Test if PATH2 is a child of PATH1.

   If not, return NULL.
   If so, return the "remainder" path.  (The substring which, when
   appended to PATH1, yields PATH2.) */
svn_stringbuf_t *
svn_path_is_child (const svn_stringbuf_t *path1,
                   const svn_stringbuf_t *path2,
                   enum svn_path_style style,
                   apr_pool_t *pool)
{
  const path_style_context_t *ctx = get_path_style_context (style);
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
      if (path1->data[i] != path2->data[i]
          && !char_is_dirsep (path1->data[i], ctx)
          && !char_is_dirsep (path2->data[i], ctx))
        return NULL;

      i++;
    }

  /* If we get all the way to the end of path1 with the contents the
     same as in path2, and either path1 ends in a directory separator,
     or path2's next character is a directory separator followed by
     more pathy stuff, then path2 is a child of path1. */
  if (i == path1->len)
    {
      if (char_is_dirsep (path1->data[i - 1], ctx))
        return svn_stringbuf_ncreate (path2->data + i, 
                                   path2->len - i, 
                                   pool);
      else if (char_is_dirsep (path2->data[i], ctx))
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
                    enum svn_path_style style,
                    apr_pool_t *pool)
{
  const path_style_context_t *ctx = get_path_style_context (style);
  apr_size_t i, oldi;

  apr_array_header_t *components = 
    apr_array_make (pool, 1, sizeof(svn_stringbuf_t *));

  i = oldi = 0;

  if (svn_path_is_empty (path, style))
    return components;

  /* If PATH is absolute, store the '/' as the first component. */
  if (char_is_dirsep (path->data[i], ctx))
    {
      store_component (components, &ctx->dirsep, 1, pool);
      i++;
      oldi++;
    }

  while (i <= path->len)
    {
      if (char_is_dirsep (path->data[i], ctx) || (path->data[i] == '\0'))
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
svn_path_is_single_path_component (svn_stringbuf_t *path,
                                   enum svn_path_style style)
{
  const path_style_context_t *ctx = get_path_style_context (style);

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
  if (strchr (path->data, ctx->dirsep) != NULL
      || (ctx->alt_dirsep
          && strchr (path->data, ctx->alt_dirsep) != NULL))
    return 0;

  /* it is valid */
  return 1;
}



/*** URI Stuff ***/

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
char_is_uri_safe (const unsigned char c)
{
  unsigned char other[18] = "/:.-_!~'()@=+$,&*"; /* sorted by estimated use? */
  int i;

  /* Is this an alphanumeric character? */
  if (((c >= 'A') && (c <='Z'))
      || ((c >= 'a') && (c <='z'))
      || ((c >= '0') && (c <='9')))
    return TRUE;

  /* Is this a supported non-alphanumeric character? */
  for (i = 0; i < sizeof (other); i++)
    {
      if (c == other[i])
        return TRUE;
    }

  return FALSE;
}


svn_boolean_t 
svn_path_is_uri_safe (const svn_stringbuf_t *path)
{
  int i;

  for (i = 0; i < path->len; i++)
    {
      if (! char_is_uri_safe (path->data[i]))
        return FALSE;
    }

  return TRUE;
}
  

svn_stringbuf_t *
svn_path_uri_encode (const svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  int i;
  int copied = 0;
  unsigned char c;

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

  return retstr;
}


svn_stringbuf_t *
svn_path_uri_decode (const svn_stringbuf_t *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  int i;
  unsigned char c;

  if ((! path) || (! path->data))
    return NULL;

  retstr = svn_stringbuf_create ("", pool);
  svn_stringbuf_ensure (retstr, path->len);
  for (i = 0; i < path->len; i++)
    {
      c = path->data[i];
      if (c == '+') /* _encode() doesn't do this, but it's easy...whatever. */
        c = ' ';
      else if (c == '%')
        {
          unsigned char digitz[3];
          digitz[0] = path->data[++i];
          digitz[1] = path->data[++i];
          digitz[2] = '\0';
          c = (unsigned char)(strtol (digitz, NULL, 16));
        }

      retstr->data[retstr->len++] = c;
    }

  return retstr;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
