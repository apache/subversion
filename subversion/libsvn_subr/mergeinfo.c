/*
 * mergeinfo.c:  Merge info parsing and handling
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

#include "svn_types.h"
#include "svn_ctype.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_string.h"
#include "svn_mergeinfo.h"

static svn_error_t *
parse_revision(const char **input, const char *end, svn_revnum_t *revision)
{
  const char *curr = *input;
  char *endptr;
  svn_revnum_t result = strtol(curr, &endptr, 10);

  if (curr == endptr)
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL, "Invalid revision number");

  *revision = result;

  *input = endptr;
  return SVN_NO_ERROR;
}

/* pathname -> PATHNAME@REVISION */
static svn_error_t *
parse_pathname(const char **input, const char *end,
               svn_pathrev_pair_t *pathrev, apr_pool_t *pool)
{
  const char *curr = *input;
  svn_stringbuf_t *pathname = svn_stringbuf_create("", pool);

  while (curr < end && *curr != '@')
    {
      svn_stringbuf_appendbytes(pathname, curr, 1);
      curr++;
    }
  if (*curr != '@')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL, "No '@' found in pathname");

  pathrev->path = pathname->data;
  curr++;

  if (curr >= end || !svn_ctype_isdigit(*curr))
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            "Revision number missing after ");

  SVN_ERR(parse_revision(&curr, end, &pathrev->revnum));
  *input = curr;

  return SVN_NO_ERROR;
}


/* revisionlist -> (revisionrange | REVISION)(COMMA revisioneelement)*
   revisionrange -> REVISION "-" REVISION
   revisioneelement -> revisionrange | REVISION
 */
static svn_error_t *
parse_revlist(const char **input, const char *end, apr_array_header_t *revlist,
              apr_pool_t *pool)
{
  const char *curr = *input;

  if (curr == end)
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL, 
                            "No revision list found ");

  while (curr < end)
    {
      svn_merge_info_t *minfo = apr_pcalloc(pool, sizeof(*minfo));
      svn_revnum_t firstrev;

      SVN_ERR(parse_revision(&curr, end, &firstrev));
      if (*curr != '-' && *curr != '\n' && *curr != ',' && curr != end)
        return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                                "Invalid character found in revision list");
      minfo->type = SVN_MERGE_RANGE_SINGLE;
      minfo->u.single.revision = firstrev;

      if (*curr == '-')
        {
          svn_revnum_t secondrev;

          curr++;
          SVN_ERR(parse_revision(&curr, end, &secondrev));
          minfo->type = SVN_MERGE_RANGE_RANGE;
          minfo->u.range.start = firstrev;
          minfo->u.range.end = secondrev;
        }

      /* XXX: Watch empty revision list problem */
      if (*curr == '\n' || curr == end)
        {
          APR_ARRAY_PUSH(revlist, svn_merge_info_t *) = minfo;
          *input = curr;
          return SVN_NO_ERROR;
        }
      else if (*curr == ',')
        {
          APR_ARRAY_PUSH(revlist, svn_merge_info_t *) = minfo;
          curr++;
        }
      else
        {
          return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                                  "Invalid character found in revision list");
        }

    }
  if (*curr != '\n')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                                "Revision list parsing ended before hitting newline");
  *input = curr;
  return SVN_NO_ERROR;
}

/* revisionline -> PATHNAME@REVISION COLON revisionlist */

static svn_error_t *
parse_revision_line(const char **input, const char *end, apr_hash_t *hash, 
                    apr_pool_t *pool)
{
  svn_pathrev_pair_t *pair = apr_pcalloc(pool, sizeof (*pair));
  apr_array_header_t *revlist = apr_array_make(pool, 1, 
                                               sizeof(svn_merge_info_t *));

  SVN_ERR(parse_pathname(input, end, pair, pool));

  if (*(*input) != ':')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL, 
                            "Pathname not terminated by ':'");

  *input = *input + 1;

  SVN_ERR(parse_revlist(input, end, revlist, pool));

  if (*input != end && *(*input) != '\n')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            "Could not find end of line in revision line");

  if (*input != end)
    *input = *input + 1;

  apr_hash_set (hash, pair, sizeof (*pair), revlist);
  
  return SVN_NO_ERROR;
}

/* top -> revisionline (NEWLINE revisionline)*  */
static svn_error_t *
parse_top(const char **input, const char *end, apr_hash_t *hash, 
          apr_pool_t *pool)
{
  while (*input < end)
    SVN_ERR(parse_revision_line(input, end, hash, pool));

  return SVN_NO_ERROR;
}

/* Parse mergeinfo.  */
svn_error_t *
parse_mergeinfo(const char **input, const char *end, apr_hash_t *hash, 
                apr_pool_t *pool)
{
  return parse_top(input, end, hash, pool);
}

