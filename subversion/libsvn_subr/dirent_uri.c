/*
 * dirent_uri.c:   a library to manipulate URIs and directory entries.
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
#include <ctype.h>

#include <apr_uri.h>

#include "svn_string.h"
#include "svn_dirent_uri.h"

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

/* Labels for some commonly used constants */
#define URI TRUE
#define DIRENT FALSE


/**** Internal implementation functions *****/


/* Return the length of substring necessary to encompass the entire
 * previous dirent segment in DIRENT, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length except
 * in the case in which DIRENT is absolute and there are no more
 * previous segments.
 */
static apr_size_t
dirent_previous_segment(const char *dirent,
                        apr_size_t len)
{
  if (len == 0)
    return 0;

  --len;
  while (len > 0 && dirent[len] != '/'
#if defined(WIN32) || defined(__CYGWIN__)
                 && dirent[len] != ':'
#endif /* WIN32 or Cygwin */
        )
    --len;

  /* check if the remaining segment including trailing '/' is a root dirent */
  if (svn_dirent_is_root(dirent, len + 1))
    return len + 1;
  else
    return len;
}


static const char *
canonicalize(svn_boolean_t uri, const char *path, apr_pool_t *pool)
{
  char *canon, *dst;
  const char *src;
  apr_size_t seglen;
  apr_size_t canon_segments = 0;
  apr_uri_t host_uri;

  /* "" is already canonical, so just return it; note that later code
     depends on path not being zero-length.  */
  if (! *path)
    return path;

  dst = canon = apr_pcalloc(pool, strlen(path) + 1);

  /* Try to parse the path as an URI. */
  if (uri && apr_uri_parse(pool, path, &host_uri) == APR_SUCCESS &&
      host_uri.scheme && host_uri.hostname)
    {
      /* convert scheme and hostname to lowercase */
      apr_size_t offset;
      int i;

      for(i = 0; host_uri.scheme[i]; i++)
        host_uri.scheme[i] = tolower(host_uri.scheme[i]);
      for(i = 0; host_uri.hostname[i]; i++)
        host_uri.hostname[i] = tolower(host_uri.hostname[i]);

      /* path will be pointing to a new memory location, so update src to
       * point to the new location too. */
      offset = strlen(host_uri.scheme) + 3; /* "(scheme)://" */
      path = apr_uri_unparse(pool, &host_uri, APR_URI_UNP_REVEALPASSWORD);

      /* skip 3rd '/' in file:/// uri */
      if (path[offset] == '/')
        offset++;

      /* copy src to dst */
      memcpy(dst, path, offset);
      dst += offset;

      src = path + offset;
    }
  else
    {
      src = path;
      /* If this is an absolute path, then just copy over the initial
         separator character. */
      if (*src == '/')
        {
          *(dst++) = *(src++);

#if defined(WIN32) || defined(__CYGWIN__)
          /* On Windows permit two leading separator characters which means an
           * UNC path. */
          if (*src == '/')
            *(dst++) = *(src++);
#endif /* WIN32 or Cygwin */
        }
    }

  while (*src)
    {
      /* Parse each segment, find the closing '/' */
      const char *next = src;
      while (*next && (*next != '/'))
        ++next;

      seglen = next - src;

      if (seglen == 0 || (seglen == 1 && src[0] == '.'))
        {
          /* Noop segment, so do nothing. */
        }
#if defined(WIN32) || defined(__CYGWIN__)
      /* If this is the first path segment of a file:// URI and it contains a
         windows drive letter, convert the drive letter to upper case. */
      else if (uri && canon_segments == 0 && seglen == 2 &&
          strcmp(host_uri.scheme, "file") == 0 &&
          src[0] >= 'a' && src[0] <= 'z' && src[1] == ':')
        {
          *(dst++) = toupper(src[0]);
          *(dst++) = ':';
          if (*next)
            *(dst++) = *next;
          canon_segments++;
        }
#endif /* WIN32 or Cygwin */
      else
        {
          /* An actual segment, append it to the destination path */
          if (*next)
            seglen++;
          memcpy(dst, src, seglen);
          dst += seglen;
          canon_segments++;
        }

      /* Skip over trailing slash to the next segment. */
      src = next;
      if (*src)
        src++;
    }

  /* Remove the trailing slash if necessary. */
  if (*(dst - 1) == '/')
    {
      /* If we had any path components, we always remove the trailing slash. */
      if (canon_segments > 0)
        dst --;
      /* Otherwise, make sure to strip the third slash from URIs which
       * have an empty hostname part, such as http:/// or file:/// */
      else if (uri && host_uri.hostname[0] == '\0' &&
               host_uri.path && host_uri.path[0] == '/')
              dst--;
    }

  *dst = '\0';

#if defined(WIN32) || defined(__CYGWIN__)
  /* Skip leading double slashes when there are less than 2
   * canon segments. UNC paths *MUST* have two segments. */
  if (canon[0] == '/' && canon[1] == '/')
    {
      if (canon_segments < 2)
        return canon + 1;
      else
        {
          /* Now we're sure this is a valid UNC path, convert the server name 
             (the first path segment) to lowercase as Windows treats it as case
             insensitive. 
             Note: normally the share name is treated as case insensitive too,
             but it seems to be possible to configure Samba to treat those as
             case sensitive, so better leave that alone. */
          dst = canon + 2;
          while (*dst && *dst != '/')
            *(dst++) = tolower(*dst);
        }
    }
#endif /* WIN32 or Cygwin */

  return canon;
}


/**** Public API functions ****/

/* We decided against using apr_filepath_root here because of the negative
   performance impact (creating a pool and converting strings ). */
svn_boolean_t
svn_dirent_is_root(const char *dirent, apr_size_t len)
{
  /* directory is root if it's equal to '/' */
  if (len == 1 && dirent[0] == '/')
    return TRUE;

#if defined(WIN32) || defined(__CYGWIN__)
  /* On Windows and Cygwin, 'H:' or 'H:/' (where 'H' is any letter)
     are also root directories */
  if ((len == 2 || len == 3) &&
      (dirent[1] == ':') &&
      ((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
       (dirent[0] >= 'a' && dirent[0] <= 'z')) &&
      (len == 2 || (dirent[2] == '/' && len == 3)))
    return TRUE;

  /* On Windows and Cygwin, both //drive and //drive//share are root
     directories */
  if (len >= 2 && dirent[0] == '/' && dirent[1] == '/'
      && dirent[len - 1] != '/')
    {
      int segments = 0;
      int i;
      for (i = len; i >= 2; i--)
        {
          if (dirent[i] == '/')
            {
              segments ++;
              if (segments > 1)
                return FALSE;
            }
        }
      return (segments <= 1);
    }
#endif /* WIN32 or Cygwin */

  return FALSE;
}

char *svn_dirent_join(const char *base,
                      const char *component,
                      apr_pool_t *pool)
{
  apr_size_t blen = strlen(base);
  apr_size_t clen = strlen(component);
  char *dirent;
  int add_separator;

  assert(svn_dirent_is_canonical(base, pool));
  assert(svn_dirent_is_canonical(component, pool));

  /* If the component is absolute, then return it.  */
  if (svn_dirent_is_absolute(component, clen))
    return apr_pmemdup(pool, component, clen + 1);

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY(base))
    return apr_pmemdup(pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY(component))
    return apr_pmemdup(pool, base, blen + 1);

  /* if last character of base is already a separator, don't add a '/' */
  add_separator = 1;
  if (base[blen - 1] == '/'
#if defined(WIN32) || defined(__CYGWIN__)
       || base[blen - 1] == ':'
#endif /* WIN32 or Cygwin */
        )
          add_separator = 0;

  /* Construct the new, combined dirent. */
  dirent = apr_palloc(pool, blen + add_separator + clen + 1);
  memcpy(dirent, base, blen);
  if (add_separator)
    dirent[blen] = '/';
  memcpy(dirent + blen + add_separator, component, clen + 1);

  return dirent;
}

char *svn_dirent_join_many(apr_pool_t *pool, const char *base, ...)
{
#define MAX_SAVED_LENGTHS 10
  apr_size_t saved_lengths[MAX_SAVED_LENGTHS];
  apr_size_t total_len;
  int nargs;
  va_list va;
  const char *s;
  apr_size_t len;
  char *dirent;
  char *p;
  int add_separator;
  int base_arg = 0;

  total_len = strlen(base);

  assert(svn_dirent_is_canonical(base, pool));

  /* if last character of base is already a separator, don't add a '/' */
  add_separator = 1;
  if (total_len == 0 
       || base[total_len - 1] == '/'
#if defined(WIN32) || defined(__CYGWIN__)
       || base[total_len - 1] == ':'
#endif /* WIN32 or Cygwin */
        )
          add_separator = 0;

  saved_lengths[0] = total_len;

  /* Compute the length of the resulting string. */

  nargs = 0;
  va_start(va, base);
  while ((s = va_arg(va, const char *)) != NULL)
    {
      len = strlen(s);

      assert(svn_dirent_is_canonical(s, pool));

      if (SVN_PATH_IS_EMPTY(s))
        continue;

      if (nargs++ < MAX_SAVED_LENGTHS)
        saved_lengths[nargs] = len;

      if (svn_dirent_is_absolute(s, len))
        {
          /* an absolute dirent. skip all components to this point and reset
             the total length. */
          total_len = len;
          base_arg = nargs;
          add_separator = 1;
          if (s[len - 1] == '/'
        #if defined(WIN32) || defined(__CYGWIN__)
               || s[len - 1] == ':'
        #endif /* WIN32 or Cygwin */
                )
                  add_separator = 0;
        }
      else if (nargs == base_arg + 1)
        {
          total_len += add_separator + len;
        }
      else
        {
          total_len += 1 + len;
        }
    }
  va_end(va);

  /* base == "/" and no further components. just return that. */
  if (add_separator == 0 && total_len == 1)
    return apr_pmemdup(pool, "/", 2);

  /* we got the total size. allocate it, with room for a NULL character. */
  dirent = p = apr_palloc(pool, total_len + 1);

  /* if we aren't supposed to skip forward to an absolute component, and if
     this is not an empty base that we are skipping, then copy the base
     into the output. */
  if (base_arg == 0 && ! (SVN_PATH_IS_EMPTY(base)))
    {
      if (SVN_PATH_IS_EMPTY(base))
        memcpy(p, SVN_EMPTY_PATH, len = saved_lengths[0]);
      else
        memcpy(p, base, len = saved_lengths[0]);
      p += len;
    }

  nargs = 0;
  va_start(va, base);
  while ((s = va_arg(va, const char *)) != NULL)
    {
      if (SVN_PATH_IS_EMPTY(s))
        continue;

      if (++nargs < base_arg)
        continue;

      if (nargs < MAX_SAVED_LENGTHS)
        len = saved_lengths[nargs];
      else
        len = strlen(s);

      /* insert a separator if we aren't copying in the first component
         (which can happen when base_arg is set). also, don't put in a slash
         if the prior character is a slash (occurs when prior component
         is "/"). */
      if (p != dirent &&
          ( ! (nargs - 1 == base_arg) || add_separator))
        *p++ = '/';

      /* copy the new component and advance the pointer */
      memcpy(p, s, len);
      p += len;
    }
  va_end(va);

  *p = '\0';
  assert((apr_size_t)(p - dirent) == total_len);

  return dirent;
}

char *
svn_dirent_dirname(const char *dirent, apr_pool_t *pool)
{
  apr_size_t len = strlen(dirent);

  assert(svn_dirent_is_canonical(dirent, pool));

  if (svn_dirent_is_root(dirent, len))
    return apr_pstrmemdup(pool, dirent, len);
  else
    return apr_pstrmemdup(pool, dirent, dirent_previous_segment(dirent, len));
}

char *
svn_dirent_basename(const char *dirent, apr_pool_t *pool)
{
  apr_size_t len = strlen(dirent);
  apr_size_t start;

  assert(svn_dirent_is_canonical(dirent, pool));

  if (svn_dirent_is_root(dirent, len))
    start = 0;
  else
    {
      start = len;
      while (start > 0 && dirent[start - 1] != '/'
#if defined(WIN32) || defined(__CYGWIN__)
             && dirent[start - 1] != ':'
#endif /* WIN32 or Cygwin */
            )
        --start;
    }

  return apr_pstrmemdup(pool, dirent + start, len - start);
}

void
svn_dirent_split(const char *dirent,
                 const char **dirpath,
                 const char **base_name,
                 apr_pool_t *pool)
{
  assert(dirpath != base_name);

  if (dirpath)
    *dirpath = svn_dirent_dirname(dirent, pool);

  if (base_name)
    *base_name = svn_dirent_basename(dirent, pool);
}

svn_boolean_t
svn_dirent_is_absolute(const char *dirent, apr_size_t len)
{
  /* dirent is absolute if it starts with '/' */
  if (len > 0 && dirent[0] == '/')
    return TRUE;
 
  /* On Windows, dirent is also absolute when it starts with 'H:' or 'H:/' 
     where 'H' is any letter. */
#if defined(WIN32) || defined(__CYGWIN__)
  if (len >= 2 && 
      (dirent[1] == ':') &&
      ((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
       (dirent[0] >= 'a' && dirent[0] <= 'z')))
     return TRUE;
#endif /* WIN32 or Cygwin */
 
  return FALSE;
}

const char *
svn_dirent_canonicalize(const char *dirent, apr_pool_t *pool)
{
  const char *dst = canonicalize(DIRENT, dirent, pool);;

#if defined(WIN32) || defined(__CYGWIN__)
  /* Handle a specific case on Windows where path == "X:/". Here we have to 
     append the final '/', as svn_path_canonicalize will chop this of. */
  if (((dirent[0] >= 'A' && dirent[0] <= 'Z') ||
        (dirent[0] >= 'a' && dirent[0] <= 'z')) &&
        dirent[1] == ':' && dirent[2] == '/' &&
        dst[3] == '\0')
    {
      char *dst_slash = apr_pcalloc(pool, 4);
      dst_slash[0] =  dirent[0];
      dst_slash[1] = ':';
      dst_slash[2] = '/';
      dst_slash[3] = '\0';

      return dst_slash;
    }
#endif /* WIN32 or Cygwin */

  return dst;
}

svn_boolean_t
svn_dirent_is_canonical(const char *dirent, apr_pool_t *pool)
{
  return (strcmp(dirent, svn_dirent_canonicalize(dirent, pool)) == 0);
}
