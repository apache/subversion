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
#include "svn_private_config.h"

static svn_error_t *
parse_revision(const char **input, const char *end, svn_revnum_t *revision)
{
  const char *curr = *input;
  char *endptr;
  svn_revnum_t result = strtol(curr, &endptr, 10);

  if (curr == endptr)
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            _("Invalid revision number"));

  *revision = result;

  *input = endptr;
  return SVN_NO_ERROR;
}

/* pathname -> PATHNAME */
static svn_error_t *
parse_pathname(const char **input, const char *end,
               svn_stringbuf_t **pathname, apr_pool_t *pool)
{
  const char *curr = *input;
  *pathname = svn_stringbuf_create("", pool);

  while (curr < end && *curr != ':')
    {
      svn_stringbuf_appendbytes(*pathname, curr, 1);
      curr++;
    }

  *input = curr;

  return SVN_NO_ERROR;
}


/* revisionlist -> (revisionrange | REVISION)(COMMA revisioneelement)*
   revisionrange -> REVISION "-" REVISION
   revisioneelement -> revisionrange | REVISION
 */
static svn_error_t *
parse_revlist(const char **input, const char *end,
              apr_array_header_t *revlist, apr_pool_t *pool)
{
  const char *curr = *input;

  if (curr == end)
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            _("No revision list found "));

  while (curr < end)
    {
      svn_merge_range_t *mrange = apr_pcalloc(pool, sizeof(*mrange));
      svn_revnum_t firstrev;

      SVN_ERR(parse_revision(&curr, end, &firstrev));
      if (*curr != '-' && *curr != '\n' && *curr != ',' && curr != end)
        return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                                _("Invalid character found in revision list"));
      mrange->start = firstrev;
      mrange->end = firstrev;
      
      if (*curr == '-')
        {
          svn_revnum_t secondrev;

          curr++;
          SVN_ERR(parse_revision(&curr, end, &secondrev));
          mrange->end = secondrev;
        }

      /* XXX: Watch empty revision list problem */
      if (*curr == '\n' || curr == end)
        {
          APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = mrange;
          *input = curr;
          return SVN_NO_ERROR;
        }
      else if (*curr == ',')
        {
          APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = mrange;
          curr++;
        }
      else
        {
          return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                                  _("Invalid character found in revision list"));
        }

    }
  if (*curr != '\n')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            _("Revision list parsing ended before hitting newline"));
  *input = curr;
  return SVN_NO_ERROR;
}

/* revisionline -> PATHNAME@REVISION COLON revisionlist */

static svn_error_t *
parse_revision_line(const char **input, const char *end, apr_hash_t *hash,
                    apr_pool_t *pool)
{
  svn_stringbuf_t *pathname;
  apr_array_header_t *revlist = apr_array_make(pool, 1,
                                               sizeof(svn_merge_range_t *));

  SVN_ERR(parse_pathname(input, end, &pathname, pool));

  if (*(*input) != ':')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            _("Pathname not terminated by ':'"));

  *input = *input + 1;

  SVN_ERR(parse_revlist(input, end, revlist, pool));

  if (*input != end && *(*input) != '\n')
    return svn_error_create(SVN_ERR_MERGE_INFO_PARSE_ERROR, NULL,
                            _("Could not find end of line in revision line"));

  if (*input != end)
    *input = *input + 1;

  apr_hash_set (hash, pathname->data, APR_HASH_KEY_STRING, revlist);

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
svn_parse_mergeinfo(const char *input, apr_hash_t **hash,
                    apr_pool_t *pool)
{
  *hash = apr_hash_make(pool);
  return parse_top(&input, input + strlen(input), *hash, pool);
}
