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
#include <assert.h>

#include "svn_types.h"
#include "svn_ctype.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_string.h"
#include "svn_mergeinfo.h"
#include "svn_private_config.h"

/* Define a MAX macro if we don't already have one */
#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

/* Attempt to combine two ranges, IN1 and IN2, and put the result in
   OUTPUT.
   If they could be combined, return TRUE. If they could not, return FALSE  */
static svn_boolean_t
svn_combine_ranges(svn_merge_range_t **output, svn_merge_range_t *in1,
                   svn_merge_range_t *in2)
{
  if (in1->start == in2->start)
    {
      (*output)->start = in1->start;
      (*output)->end = MAX(in1->end, in2->end);
      return TRUE;
    }
  /* [1,4] U [5,9] = [1,9] in subversion revisions */
  else if (in2->start == in1->end
           || in2->start == in1->end + 1)
    {
      (*output)->start = in1->start;
      (*output)->end = in2->end;
      return TRUE;
    }
  else if (in1->start == in2->end
           || in1->start == in2->end + 1)
    {
      (*output)->start = in2->start;
      (*output)->end = in1->end;
      return TRUE;
    }
  else if (in1->start <= in2->start
           && in1->end >= in2->start)
    {
      (*output)->start = in1->start;
      (*output)->end = MAX(in1->end, in2->end);
      return TRUE;
    }
  else if (in2->start <= in1->start
           && in2->end >= in1->start)
    {
      (*output)->start = in2->start;
      (*output)->end = MAX(in1->end, in2->end);
      return TRUE;
    }
  else if (in1->start >= in2->start
           && in1->end <= in2->end)
    {
      (*output)->start = in2->start;
      (*output)->end = in2->end;
      return TRUE;
    }
  else if (in2->start >= in1->start
           && in2->end <= in1->end)
    {
      (*output)->start = in1->start;
      (*output)->end = in1->end;
      return TRUE;
    }

  return FALSE;
}

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
  svn_merge_range_t *lastrange = NULL;

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
          if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, mrange))
            {
              APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = mrange;
              lastrange = mrange;
            }
          *input = curr;
          return SVN_NO_ERROR;
        }
      else if (*curr == ',')
        {
          if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, mrange))
            {
              APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = mrange;
              lastrange = mrange;
            }
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

/* revisionline -> PATHNAME COLON revisionlist */
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

  qsort(revlist->elts, revlist->nelts, revlist->elt_size,
        svn_sort_compare_ranges);
  apr_hash_set(hash, pathname->data, APR_HASH_KEY_STRING, revlist);

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
svn_mergeinfo_parse(const char *input, apr_hash_t **hash,
                    apr_pool_t *pool)
{
  *hash = apr_hash_make(pool);
  return parse_top(&input, input + strlen(input), *hash, pool);
}


/* Merge two revision lists IN1 and IN2 and place the result in
   OUTPUT.  We do some trivial attempts to combine ranges as we go.  */
svn_error_t *
svn_rangelist_merge(apr_array_header_t **output, apr_array_header_t *in1,
                    apr_array_header_t *in2, apr_pool_t *pool)
{
  int i, j;
  svn_merge_range_t *lastrange = NULL;
  svn_merge_range_t *newrange;

  *output = apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

  qsort(in1->elts, in1->nelts, in1->elt_size, svn_sort_compare_ranges);
  qsort(in2->elts, in2->nelts, in2->elt_size, svn_sort_compare_ranges);

  i = 0;
  j = 0;
  while (i < in1->nelts && j < in2->nelts)
    {
      svn_merge_range_t *elt1, *elt2;
      int res;

      elt1 = APR_ARRAY_IDX(in1, i, svn_merge_range_t *);
      elt2 = APR_ARRAY_IDX(in2, j, svn_merge_range_t *);

      res = svn_sort_compare_ranges(&elt1, &elt2);
      if (res == 0)
        {
          if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, elt1))
            {
              newrange = apr_pcalloc(pool, sizeof(*newrange));
              newrange->start = elt1->start;
              newrange->end = elt1->end;

              APR_ARRAY_PUSH(*output, svn_merge_range_t *) = newrange;
              lastrange = newrange;
            }

          i++;
          j++;
        }
      else if (res < 0)
        {
          if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, elt1))
            {

              newrange = apr_pcalloc(pool, sizeof(*newrange));
              newrange->start = elt1->start;
              newrange->end = elt1->end;

              APR_ARRAY_PUSH(*output, svn_merge_range_t *) = newrange;
              lastrange = newrange;
            }

          i++;
        }
      else
        {
          if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, elt2))
            {
              newrange = apr_pcalloc(pool, sizeof(*newrange));
              newrange->start = elt2->start;
              newrange->end = elt2->end;

              APR_ARRAY_PUSH(*output, svn_merge_range_t *) = newrange;
              lastrange = newrange;
            }

          j++;
        }
    }
  /* Copy back any remaining elements.
     Only one of these loops should end up running, if anything. */

  assert (!(i < in1->nelts && j < in2->nelts));

  for (; i < in1->nelts; i++)
    {
      svn_merge_range_t *elt = APR_ARRAY_IDX(in1, i, svn_merge_range_t *);

      if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, elt))
        {
          newrange = apr_pcalloc(pool, sizeof(*newrange));
          newrange->start = elt->start;
          newrange->end = elt->end;

          APR_ARRAY_PUSH(*output, svn_merge_range_t *) = newrange;
          lastrange = newrange;
        }
    }


  for (; j < in2->nelts; j++)
    {
      svn_merge_range_t *elt = APR_ARRAY_IDX(in2, j, svn_merge_range_t *);

      if (!lastrange || !svn_combine_ranges(&lastrange, lastrange, elt))
        {
          newrange = apr_pcalloc(pool, sizeof(*newrange));
          newrange->start = elt->start;
          newrange->end = elt->end;

          APR_ARRAY_PUSH(*output, svn_merge_range_t *) = newrange;
          lastrange = newrange;
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_rangelist_remove(apr_array_header_t **output, apr_array_header_t *eraser,
                     apr_array_header_t *whiteboard, apr_pool_t *pool)
{
  /* ### TODO: Implement me!  dberlin suggests: Walk the sorted list
     ### like svn_rangelist_merge() does, doing subtraction instead of
     ### a union.  For the equals case, do nothing and increment i +
     ### j.  For the other two cases, subtract the range.  If it has
     ### become disjoint, add the two ranges to the list. */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_diff(apr_hash_t **deleted, apr_hash_t **added,
                   apr_hash_t *from, apr_hash_t *to, apr_pool_t *pool)
{
  *deleted = apr_hash_make(pool);
  *added = apr_hash_make(pool);

  /* ### TODO: Implement me! */

  return SVN_NO_ERROR;
}

/* Merge two sets of merge info IN1 and IN2 and place the result in
   OUTPUT */
svn_error_t *
svn_mergeinfo_merge(apr_hash_t **output, apr_hash_t *in1, apr_hash_t *in2,
                    apr_pool_t *pool)
{
  apr_array_header_t *sorted1, *sorted2;
  int i, j;

  *output = apr_hash_make (pool);
  sorted1 = svn_sort__hash(in1, svn_sort_compare_items_as_paths, pool);
  sorted2 = svn_sort__hash(in2, svn_sort_compare_items_as_paths, pool);

  i = 0;
  j = 0;
  while (i < sorted1->nelts && j < sorted2->nelts)
    {
      svn_sort__item_t elt1, elt2;
      int res;

      elt1 = APR_ARRAY_IDX(sorted1, i, svn_sort__item_t);
      elt2 = APR_ARRAY_IDX(sorted2, j, svn_sort__item_t);
      res = svn_sort_compare_items_as_paths(&elt1, &elt2);

      if (res == 0)
        {
          apr_array_header_t *merged;

          SVN_ERR(svn_rangelist_merge(&merged, elt1.value, elt2.value, pool));
          apr_hash_set(*output, elt1.key, elt1.klen, merged);
          i++;
          j++;
        }
      else if (res < 0)
        {
          apr_hash_set(*output, elt1.key, elt1.klen, elt1.value);
          i++;
        }
      else
        {
          apr_hash_set(*output, elt2.key, elt2.klen, elt2.value);
          j++;
        }
    }

  /* Copy back any remaining elements.
     Only one of these loops should end up running, if anything. */

  assert (!(i < sorted1->nelts && j < sorted2->nelts));

  for (; i < sorted1->nelts; i++)
    {
      svn_sort__item_t elt = APR_ARRAY_IDX(sorted1, i, svn_sort__item_t);
      apr_hash_set(*output, elt.key, elt.klen, elt.value);
    }

  for (; j < sorted2->nelts; j++)
    {
      svn_sort__item_t elt = APR_ARRAY_IDX(sorted2, j, svn_sort__item_t);
      apr_hash_set(*output, elt.key, elt.klen, elt.value);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_remove(apr_hash_t **output, apr_hash_t *eraser,
                     apr_hash_t *whiteboard, apr_pool_t *pool)
{
  /* ### TODO: Implement me! */

  return SVN_NO_ERROR;
}

/* Convert a single svn_merge_range_t * back into a svn_stringbuf_t.  */
static svn_error_t *
svn_range_to_string(svn_stringbuf_t **result, svn_merge_range_t *range,
                    apr_pool_t *pool)
{
  if (range->start == range->end)
    *result = svn_stringbuf_createf(pool, "%" SVN_REVNUM_T_FMT,
                                    range->start);
  else
    *result = svn_stringbuf_createf(pool, "%" SVN_REVNUM_T_FMT "-%" SVN_REVNUM_T_FMT,
                                    range->start, range->end);
  return SVN_NO_ERROR;
}

/* Take an array of svn_merge_range_t *'s in INPUT, and convert it
   back to a text format rangelist in OUTPUT.  */
svn_error_t *
svn_rangelist_to_string(svn_stringbuf_t **output, apr_array_header_t *input,
                         apr_pool_t *pool)
{
  int i;
  svn_merge_range_t *range;
  svn_stringbuf_t *toappend;

  *output = svn_stringbuf_create("", pool);

  /* Handle the elements that need commas at the end.  */
  for (i = 0; i < input->nelts - 1; i++)
    {
      range = APR_ARRAY_IDX(input, i, svn_merge_range_t *);
      SVN_ERR(svn_range_to_string(&toappend, range, pool));
      svn_stringbuf_appendstr(*output, toappend);
      svn_stringbuf_appendcstr(*output,",");
    }

  /* Now handle the last element, which needs no comma.  */
  range = APR_ARRAY_IDX(input, i, svn_merge_range_t *);
  SVN_ERR(svn_range_to_string(&toappend, range, pool));
  svn_stringbuf_appendstr(*output, toappend);

  return SVN_NO_ERROR;
}

/* Take a mergeinfo hash and turn it back into a string.  */
svn_error_t *
svn_mergeinfo_to_string(svn_stringbuf_t **output, apr_hash_t *input,
                        apr_pool_t *pool)
{
  apr_array_header_t *sorted1;
  svn_sort__item_t elt;
  svn_stringbuf_t *revlist, *combined;
  int i;

  *output = svn_stringbuf_create("", pool);
  sorted1 = svn_sort__hash(input, svn_sort_compare_items_as_paths, pool);

  /* Handle the elements that need newlines at the end.  */
  for (i = 0; i < sorted1->nelts -1; i++)
    {
      elt = APR_ARRAY_IDX(sorted1, i, svn_sort__item_t);

      SVN_ERR(svn_rangelist_to_string(&revlist, elt.value, pool));
      combined = svn_stringbuf_createf(pool, "%s:%s\n", (char *) elt.key,
                                       revlist->data);
      svn_stringbuf_appendstr(*output, combined);
    }

  /* Now handle the last element, which is not newline terminated.  */
  elt = APR_ARRAY_IDX(sorted1, i, svn_sort__item_t);

  SVN_ERR(svn_rangelist_to_string(&revlist, elt.value, pool));
  combined = svn_stringbuf_createf(pool, "%s:%s", (char *) elt.key,
                                   revlist->data);
  svn_stringbuf_appendstr(*output, combined);
  return SVN_NO_ERROR;
}
