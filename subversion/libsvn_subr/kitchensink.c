/*
 * kitchensink.c :  When no place else seems to fit...
 *
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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

#include <apr_pools.h>
#include <apr_uuid.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_mergeinfo.h"
#include "svn_private_config.h"

svn_error_t *
svn_revnum_parse(svn_revnum_t *rev,
                 const char *str,
                 const char **endptr)
{
  char *end;

  svn_revnum_t result = strtol(str, &end, 10);

  if (endptr)
    *endptr = end;

  if (str == end)
    return svn_error_createf(SVN_ERR_REVNUM_PARSE_FAILURE, NULL,
                             _("Invalid revision number found parsing '%s'"),
                             str);

  if (result < 0)
    {
      /* The end pointer from strtol() is valid, but a negative revision
         number is invalid, so move the end pointer back to the
         beginning of the string. */
      if (endptr)
        *endptr = str;

      return svn_error_createf(SVN_ERR_REVNUM_PARSE_FAILURE, NULL,
                               _("Negative revision number found parsing '%s'"),
                               str);
    }

  *rev = result;

  return SVN_NO_ERROR;
}

const char *
svn_uuid_generate(apr_pool_t *pool)
{
  apr_uuid_t uuid;
  char *uuid_str = apr_pcalloc(pool, APR_UUID_FORMATTED_LENGTH + 1);
  apr_uuid_get(&uuid);
  apr_uuid_format(uuid_str, &uuid);
  return uuid_str;
}

const char *
svn_depth_to_word(svn_depth_t depth)
{
  switch (depth)
    {
    case svn_depth_exclude:
      return "exclude";
    case svn_depth_unknown:
      return "unknown";
    case svn_depth_empty:
      return "empty";
    case svn_depth_files:
      return "files";
    case svn_depth_immediates:
      return "immediates";
    case svn_depth_infinity:
      return "infinity";
    default:
      return "INVALID-DEPTH";
    }
}


svn_depth_t
svn_depth_from_word(const char *word)
{
  if (strcmp(word, "exclude") == 0)
    return svn_depth_exclude;
  if (strcmp(word, "unknown") == 0)
    return svn_depth_unknown;
  if (strcmp(word, "empty") == 0)
    return svn_depth_empty;
  if (strcmp(word, "files") == 0)
    return svn_depth_files;
  if (strcmp(word, "immediates") == 0)
    return svn_depth_immediates;
  if (strcmp(word, "infinity") == 0)
    return svn_depth_infinity;
  /* There's no special value for invalid depth, and no convincing
     reason to make one yet, so just fall back to unknown depth.
     If you ever change that convention, check callers to make sure
     they're not depending on it (e.g., option parsing in main() ).
  */
  return svn_depth_unknown;
}

const char *
svn_inheritance_to_word(svn_mergeinfo_inheritance_t inherit)
{
  switch (inherit)
    {
    case svn_mergeinfo_inherited:
      return "inherited";
    case svn_mergeinfo_nearest_ancestor:
      return "nearest-ancestor";
    default:
      return "explicit";
    }
}


svn_mergeinfo_inheritance_t
svn_inheritance_from_word(const char *word)
{
  if (strcmp(word, "inherited") == 0)
    return svn_mergeinfo_inherited;
  if (strcmp(word, "nearest-ancestor") == 0)
    return svn_mergeinfo_nearest_ancestor;
  return svn_mergeinfo_explicit;
}

const char *
svn_node_kind_to_word(svn_node_kind_t kind)
{
  switch (kind)
    {
    case svn_node_none:
      return "none";
    case svn_node_file:
      return "file";
    case svn_node_dir:
      return "dir";
    case svn_node_unknown:
    default:
      return "unknown";
    }
}


svn_node_kind_t
svn_node_kind_from_word(const char *word)
{
  if (word == NULL)
    return svn_node_unknown;

  if (strcmp(word, "none") == 0)
    return svn_node_none;
  else if (strcmp(word, "file") == 0)
    return svn_node_file;
  else if (strcmp(word, "dir") == 0)
    return svn_node_dir;
  else
    /* This also handles word == "unknown" */
    return svn_node_unknown;
}
