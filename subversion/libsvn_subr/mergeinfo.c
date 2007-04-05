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

/* Define a MIN macro if we don't already have one */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* Attempt to combine two ranges, IN1 and IN2, and put the result in
   OUTPUT.  Return whether they could be combined. 
   Range overlapping detection algorithm from 
   http://c2.com/cgi-bin/wiki/fullSearch?TestIfDateRangesOverlap
*/
static svn_boolean_t
combine_ranges(svn_merge_range_t **output, svn_merge_range_t *in1,
               svn_merge_range_t *in2)
{
  if (in1->start <= in2->end + 1 && in2->start <= in1->end + 1)
    {
      (*output)->start = MIN(in1->start, in2->start);
      (*output)->end = MAX(in1->end, in2->end);
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

/*push to revlist and set lastrange, if could not combine mrange
  with *lastrange or *lastrange is NULL.
*/
static APR_INLINE void
combine_with_lastrange(svn_merge_range_t** lastrange, 
                       svn_merge_range_t *mrange, svn_boolean_t dup_mrange, 
                       apr_array_header_t *revlist, apr_pool_t *pool)
{
  svn_merge_range_t *pushed_mrange = mrange;
  if (!(*lastrange) || !combine_ranges(lastrange, *lastrange, mrange))
    {
      if (dup_mrange)
        pushed_mrange = svn_merge_range_dup(mrange, pool);
      APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = pushed_mrange;
      *lastrange = pushed_mrange;
    }
}

/* revisionlist -> (revisionelement)(COMMA revisionelement)*
   revisionrange -> REVISION "-" REVISION
   revisionelement -> revisionrange | REVISION
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
          combine_with_lastrange(&lastrange, mrange, FALSE, revlist, pool);
          *input = curr;
          return SVN_NO_ERROR;
        }
      else if (*curr == ',')
        {
          combine_with_lastrange(&lastrange, mrange, FALSE, revlist, pool);
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
svn_mergeinfo_parse(apr_hash_t **mergehash, 
                    const char *input, 
                    apr_pool_t *pool)
{
  *mergehash = apr_hash_make(pool);
  return parse_top(&input, input + strlen(input), *mergehash, pool);
}


/* Merge revision list RANGELIST into *MERGEINFO, doing some trivial
   attempts to combine ranges as we go. */
svn_error_t *
svn_rangelist_merge(apr_array_header_t **rangelist,
                    apr_array_header_t *changes,
                    apr_pool_t *pool)
{
  int i, j;
  svn_merge_range_t *lastrange = NULL;
  apr_array_header_t *output = apr_array_make(pool, 1,
                                              sizeof(svn_merge_range_t *));
  i = 0;
  j = 0;
  while (i < (*rangelist)->nelts && j < changes->nelts)
    {
      svn_merge_range_t *elt1, *elt2;
      int res;

      elt1 = APR_ARRAY_IDX(*rangelist, i, svn_merge_range_t *);
      elt2 = APR_ARRAY_IDX(changes, j, svn_merge_range_t *);

      res = svn_sort_compare_ranges(&elt1, &elt2);
      if (res == 0)
        {
          combine_with_lastrange(&lastrange, elt1, TRUE, output, pool);
          i++;
          j++;
        }
      else if (res < 0)
        {
          combine_with_lastrange(&lastrange, elt1, TRUE, output, pool);
          i++;
        }
      else
        {
          combine_with_lastrange(&lastrange, elt2, TRUE, output, pool);
          j++;
        }
    }
  /* Copy back any remaining elements.
     Only one of these loops should end up running, if anything. */

  assert (!(i < (*rangelist)->nelts && j < changes->nelts));

  for (; i < (*rangelist)->nelts; i++)
    {
      svn_merge_range_t *elt = APR_ARRAY_IDX(*rangelist, i,
                                             svn_merge_range_t *);
      combine_with_lastrange(&lastrange, elt, TRUE, output, pool);
    }


  for (; j < changes->nelts; j++)
    {
      svn_merge_range_t *elt = APR_ARRAY_IDX(changes, j, svn_merge_range_t *);
      combine_with_lastrange(&lastrange, elt, TRUE, output, pool);
    }

  *rangelist = output;
  return SVN_NO_ERROR;
}

static svn_boolean_t
range_intersect(svn_merge_range_t *first, svn_merge_range_t *second)
{
  return (first->start <= second->end) && (second->start <= first->end);
}

static svn_boolean_t
range_contains(svn_merge_range_t *first, svn_merge_range_t *second)
{
  return (first->start <= second->start) && (second->end <= first->end);
}

/* Swap start and end fields of RANGE. */
static void
range_swap_endpoints(svn_merge_range_t *range)
{
  svn_revnum_t swap = range->start;
  range->start = range->end;
  range->end = swap;
}

svn_error_t *
svn_rangelist_reverse(apr_array_header_t *rangelist, apr_pool_t *pool)
{
  int i, swap_index;
  svn_merge_range_t range;
  for (i = 0; i < rangelist->nelts / 2; i++)
    {
      swap_index = rangelist->nelts - i - 1;
      range = *APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      *APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *) =
        *APR_ARRAY_IDX(rangelist, swap_index, svn_merge_range_t *);
      *APR_ARRAY_IDX(rangelist, swap_index, svn_merge_range_t *) = range;
      range_swap_endpoints(APR_ARRAY_IDX(rangelist, swap_index,
                                         svn_merge_range_t *));
      range_swap_endpoints(APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *));
    }

  /* If there's an odd number of elements, we still need to swap the
     end points of the remaining range. */
  if (rangelist->nelts % 2 == 1)
    range_swap_endpoints(APR_ARRAY_IDX(rangelist, rangelist->nelts / 2,
                                       svn_merge_range_t *));

  return SVN_NO_ERROR;
}

/* Either remove any overlapping ranges described by ERASER from
   WHITEBOARD (when DO_REMOVE is TRUE), or capture the overlap, and
   place the remaining or overlapping ranges in OUTPUT. */
/*  ### FIXME: Some variables names and inline comments for this method
    ### are legacy from when it was solely the remove() impl. */
static svn_error_t *
rangelist_intersect_or_remove(apr_array_header_t **output,
                              apr_array_header_t *eraser,
                              apr_array_header_t *whiteboard,
                              svn_boolean_t do_remove,
                              apr_pool_t *pool)
{
  int i, j, lasti;
  svn_merge_range_t *lastrange = NULL;
  svn_merge_range_t wboardelt;

  *output = apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

  i = 0;
  j = 0;
  lasti = -1;  /* Initialized to a value that "i" will never be. */

  while (i < whiteboard->nelts && j < eraser->nelts)
    {
      svn_merge_range_t *elt1, *elt2;

      elt2 = APR_ARRAY_IDX(eraser, j, svn_merge_range_t *);

      /* Instead of making a copy of the entire array of whiteboard
         elements, we just keep a copy of the current whiteboard element
         that needs to be used, and modify our copy if necessary. */
      if (i != lasti)
        {
          wboardelt = *(APR_ARRAY_IDX(whiteboard, i, svn_merge_range_t *));
          elt1 = &wboardelt;
          lasti = i;
        }

      /* If the whiteboard range is contained completely in the
         eraser, we increment the whiteboard.
         If the ranges intersect, and match exactly, we increment both
         eraser and whiteboard.
         Otherwise, we have to generate a range for the left part of
         the removal of eraser from whiteboard, and possibly change
         the whiteboard to the remaining portion of the right part of
         the removal, to test against. */
      if (range_contains(elt2, elt1))
        {
          if (!do_remove)
              combine_with_lastrange(&lastrange, elt1, TRUE, *output, pool);
          
          i++;

          if (elt1->start == elt2->start && elt1->end == elt2->end)
            j++;
        }
      else if (range_intersect(elt2, elt1))
        {
          if (elt1->start < elt2->start)
            {
              /* The whiteboard range starts before the eraser range. */
              svn_merge_range_t tmp_range;
              if (do_remove)
                {
                  /* Retain the range that falls before the eraser start. */
                  tmp_range.start = elt1->start;
                  tmp_range.end = elt2->start - 1;
                }
              else
                {
                  /* Retain the range that falls between the eraser
                     start and whiteboard end. */
                  tmp_range.start = elt2->start;
                  tmp_range.end = elt1->end;
                }

              combine_with_lastrange(&lastrange, &tmp_range, TRUE, *output, pool);
            }

          /* Set up the rest of the whiteboard range for further
             processing.  */
          if (elt1->end > elt2->end)
            {
              /* The whiteboard range ends after the eraser range. */
              if (!do_remove)
                {
                  /* Partial overlap. */
                  svn_merge_range_t tmp_range;                  
                  tmp_range.start = elt1->start;
                  tmp_range.end = elt2->end;
                  
                  combine_with_lastrange(&lastrange, &tmp_range, TRUE, *output, pool);
                }

              wboardelt.start = elt2->end + 1;
              wboardelt.end = elt1->end;
            }
          else
            i++;
        }
      else  /* ranges don't intersect */
        {
          /* See which side of the whiteboard the eraser is on.  If it
             is on the left side, we need to move the eraser.

             If it is on past the whiteboard on the right side, we
             need to output the whiteboard and increment the
             whiteboard.  */
          if (svn_sort_compare_ranges(&elt2, &elt1) < 0)
            j++;
          else
            {
              if (!lastrange || !combine_ranges(&lastrange, lastrange, elt1))
                {
                  if (do_remove)
                    {
                      lastrange = svn_merge_range_dup(elt1, pool);
                      APR_ARRAY_PUSH(*output, svn_merge_range_t *) = lastrange;
                    }
                }
              i++;
            }
        }
    }

  if (do_remove)
    {
      /* Copy the current whiteboard element if we didn't hit the end
         of the whiteboard, and we still had it around.  This element
         may have been touched, so we can't just walk the whiteboard
         array, we have to use our copy.  This case only happens when
         we ran out of eraser before whiteboard, *and* we had changed
         the whiteboard element. */
      if (i == lasti && i < whiteboard->nelts)
        {
          combine_with_lastrange(&lastrange, &wboardelt, TRUE, *output, pool);
          i++;
        }

      /* Copy any other remaining untouched whiteboard elements.  */
      for (; i < whiteboard->nelts; i++)
        {
          svn_merge_range_t *elt = APR_ARRAY_IDX(whiteboard, i,
                                                 svn_merge_range_t *);

          combine_with_lastrange(&lastrange, elt, TRUE, *output, pool);
        }
    }

  return SVN_NO_ERROR;
}


/* Expected to handle all the range overlap cases: non, partial, full */
svn_error_t *
svn_rangelist_intersect(apr_array_header_t **output,
                        apr_array_header_t *rangelist1,
                        apr_array_header_t *rangelist2,
                        apr_pool_t *pool)
{
  return rangelist_intersect_or_remove(output, rangelist1, rangelist2, FALSE,
                                       pool);
}

svn_error_t *
svn_rangelist_remove(apr_array_header_t **output, apr_array_header_t *eraser,
                     apr_array_header_t *whiteboard, apr_pool_t *pool)
{
  return rangelist_intersect_or_remove(output, eraser, whiteboard, TRUE,
                                       pool);
}

/* Output deltas via *DELETED and *ADDED, which will never be @c NULL.

   The following diagrams illustrate some common range delta scenarios:

    (from)           deleted
    r0 <===========(=========)============[=========]===========> rHEAD
    [to]                                    added

    (from)           deleted                deleted
    r0 <===========(=========[============]=========)===========> rHEAD
    [to]

    (from)           deleted
    r0 <===========(=========[============)=========]===========> rHEAD
    [to]                                    added

    (from)                                  deleted
    r0 <===========[=========(============]=========)===========> rHEAD
    [to]             added

    (from)
    r0 <===========[=========(============)=========]===========> rHEAD
    [to]             added                  added

    (from)  d                                  d             d
    r0 <===(=[=)=]=[==]=[=(=)=]=[=]=[=(===|===(=)==|=|==[=(=]=)=> rHEAD
    [to]        a   a    a   a   a   a                   a
*/
svn_error_t *
svn_rangelist_diff(apr_array_header_t **deleted, apr_array_header_t **added,
                   apr_array_header_t *from, apr_array_header_t *to,
                   apr_pool_t *pool)
{
  /* The items that are present in from, but not in to, must have been
     deleted. */
  SVN_ERR(svn_rangelist_remove(deleted, to, from, pool));
  /* The items that are present in to, but not in from, must have been
     added.  */
  SVN_ERR(svn_rangelist_remove(added, from, to, pool));
  return SVN_NO_ERROR;
}

/* Record deletions and additions of entire range lists (by path
   presence), and delegate to svn_rangelist_diff() for delta
   calculations on a specific path. */
/* ### TODO: Merge implementation with
   ### libsvn_subr/sorts.c:svn_prop_diffs().  Factor out a generic
   ### hash diffing function for addition to APR's apr_hash.h API. */
static svn_error_t *
walk_mergeinfo_hash_for_diff(apr_hash_t *from, apr_hash_t *to, 
                             apr_hash_t *deleted, apr_hash_t *added,
                             apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *key;
  void *val;
  const char *path;
  apr_array_header_t *from_rangelist, *to_rangelist;

  /* Handle path deletions and differences. */
  for (hi = apr_hash_first(pool, from); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      from_rangelist = val;

      /* If the path is not present at all in the "to" hash, the
         entire "from" rangelist is a deletion.  Paths which are
         present in the "to" hash require closer scrutiny. */
      to_rangelist = apr_hash_get(to, path, APR_HASH_KEY_STRING);
      if (to_rangelist)
        {
          /* Record any deltas (additions or deletions). */
          apr_array_header_t *deleted_rangelist, *added_rangelist;
          svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                             from_rangelist, to_rangelist, pool);
          if (deleted && deleted_rangelist->nelts > 0)
            apr_hash_set(deleted, apr_pstrdup(pool, path),
                         APR_HASH_KEY_STRING, deleted_rangelist);
          if (added && added_rangelist->nelts > 0)
            apr_hash_set(added, apr_pstrdup(pool, path),
                         APR_HASH_KEY_STRING, added_rangelist);
        }
      else if (deleted)
        apr_hash_set(deleted, apr_pstrdup(pool, path), APR_HASH_KEY_STRING,
                     svn_rangelist_dup(from_rangelist, pool));
    }

  /* Handle path additions. */
  if (!added)
    return SVN_NO_ERROR;

  for (hi = apr_hash_first(pool, to); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      to_rangelist = val;

      /* If the path is not present in the "from" hash, the entire
         "to" rangelist is an addition. */
      if (apr_hash_get(from, path, APR_HASH_KEY_STRING) == NULL)
        apr_hash_set(added, apr_pstrdup(pool, path), APR_HASH_KEY_STRING,
                     svn_rangelist_dup(to_rangelist, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_diff(apr_hash_t **deleted, apr_hash_t **added,
                   apr_hash_t *from, apr_hash_t *to, apr_pool_t *pool)
{
  *deleted = apr_hash_make(pool);
  *added = apr_hash_make(pool);
  SVN_ERR(walk_mergeinfo_hash_for_diff(from, to, *deleted, *added, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_merge(apr_hash_t **mergeinfo, apr_hash_t *changes,
                    apr_pool_t *pool)
{
  apr_array_header_t *sorted1, *sorted2;
  int i, j;

  sorted1 = svn_sort__hash(*mergeinfo, svn_sort_compare_items_as_paths, pool);
  sorted2 = svn_sort__hash(changes, svn_sort_compare_items_as_paths, pool);

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
          apr_array_header_t *rl1, *rl2;

          rl1 = elt1.value;
          rl2 = elt2.value;

          SVN_ERR(svn_rangelist_merge(&rl1, rl2, pool));
          apr_hash_set(*mergeinfo, elt1.key, elt1.klen, rl1);
          i++;
          j++;
        }
      else if (res < 0)
        {
          i++;
        }
      else
        {
          apr_hash_set(*mergeinfo, elt2.key, elt2.klen, elt2.value);
          j++;
        }
    }

  /* Copy back any remaining elements from the second hash. */
  for (; j < sorted2->nelts; j++)
    {
      svn_sort__item_t elt = APR_ARRAY_IDX(sorted2, j, svn_sort__item_t);
      apr_hash_set(*mergeinfo, elt.key, elt.klen, elt.value);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_remove(apr_hash_t **output, apr_hash_t *eraser,
                     apr_hash_t *whiteboard, apr_pool_t *pool)
{
  *output = apr_hash_make(pool);
  SVN_ERR(walk_mergeinfo_hash_for_diff(whiteboard, eraser, *output, NULL, 
                                       pool));
  return SVN_NO_ERROR;
}

/* Convert a single svn_merge_range_t * back into an svn_stringbuf_t *.  */
static svn_error_t *
svn_range_to_stringbuf(svn_stringbuf_t **result, svn_merge_range_t *range,
                       apr_pool_t *pool)
{
  if (range->start == range->end)
    *result = svn_stringbuf_createf(pool, "%ld", range->start);
  else
    *result = svn_stringbuf_createf(pool, "%ld-%ld",range->start,
                                    range->end);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_rangelist_to_stringbuf(svn_stringbuf_t **output, apr_array_header_t *input,
                           apr_pool_t *pool)
{
  *output = svn_stringbuf_create("", pool);

  if (input->nelts > 0)
    {
      int i;
      svn_merge_range_t *range;
      svn_stringbuf_t *toappend;

      /* Handle the elements that need commas at the end.  */
      for (i = 0; i < input->nelts - 1; i++)
        {
          range = APR_ARRAY_IDX(input, i, svn_merge_range_t *);
          SVN_ERR(svn_range_to_stringbuf(&toappend, range, pool));
          svn_stringbuf_appendstr(*output, toappend);
          svn_stringbuf_appendcstr(*output, ",");
        }

      /* Now handle the last element, which needs no comma.  */
      range = APR_ARRAY_IDX(input, i, svn_merge_range_t *);
      SVN_ERR(svn_range_to_stringbuf(&toappend, range, pool));
      svn_stringbuf_appendstr(*output, toappend);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_to_stringbuf(svn_stringbuf_t **output, apr_hash_t *input,
                           apr_pool_t *pool)
{
  *output = svn_stringbuf_create("", pool);

  if (apr_hash_count(input) > 0)
    {
      apr_array_header_t *sorted =
        svn_sort__hash(input, svn_sort_compare_items_as_paths, pool);
      svn_sort__item_t elt;
      svn_stringbuf_t *revlist, *combined;
      int i;

      /* Handle the elements that need newlines at the end.  */
      for (i = 0; i < sorted->nelts - 1; i++)
        {
          elt = APR_ARRAY_IDX(sorted, i, svn_sort__item_t);

          SVN_ERR(svn_rangelist_to_stringbuf(&revlist, elt.value, pool));
          combined = svn_stringbuf_createf(pool, "%s:%s\n", (char *) elt.key,
                                           revlist->data);
          svn_stringbuf_appendstr(*output, combined);
        }

      /* Now handle the last element, which is not newline terminated.  */
      elt = APR_ARRAY_IDX(sorted, i, svn_sort__item_t);

      SVN_ERR(svn_rangelist_to_stringbuf(&revlist, elt.value, pool));
      combined = svn_stringbuf_createf(pool, "%s:%s", (char *) elt.key,
                                       revlist->data);
      svn_stringbuf_appendstr(*output, combined);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__to_string(svn_string_t **output, apr_hash_t *input,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *mergeinfo_buf;
  SVN_ERR(svn_mergeinfo_to_stringbuf(&mergeinfo_buf, input, pool));
  *output = svn_string_create_from_buf(mergeinfo_buf, pool);
  return SVN_NO_ERROR;
}

/* Perform an in-place sort of the rangelists in a mergeinfo hash.  */
svn_error_t*
svn_mergeinfo_sort(apr_hash_t *input, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  void *val;

  for (hi = apr_hash_first(pool, input); hi; hi = apr_hash_next(hi))
    {
      apr_array_header_t *rl;
      apr_hash_this(hi, NULL, NULL, &val);

      rl = val;
      qsort(rl->elts, rl->nelts, rl->elt_size, svn_sort_compare_ranges);
    }
  return SVN_NO_ERROR;
}

apr_array_header_t *
svn_rangelist_dup(apr_array_header_t *rangelist, apr_pool_t *pool)
{
  apr_array_header_t *new_rl = apr_array_make(pool, rangelist->nelts,
                                              sizeof(svn_merge_range_t *));
  int i;

  for (i = 0; i < rangelist->nelts; i++)
    {
      svn_merge_range_t *range = apr_palloc(pool, sizeof(*range));
      *range = *APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      APR_ARRAY_PUSH(new_rl, svn_merge_range_t *) = range;
    }

  return new_rl;
}

svn_merge_range_t *
svn_merge_range_dup(svn_merge_range_t *range, apr_pool_t *pool)
{
  svn_merge_range_t *new_range = apr_palloc(pool, sizeof(*new_range));
  memcpy(new_range, range, sizeof(*new_range));
  return new_range;
}
