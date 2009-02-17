/*
 * mergeinfo.c:  Mergeinfo parsing and handling
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
#include <assert.h>
#include <ctype.h>

#include "svn_path.h"
#include "svn_types.h"
#include "svn_ctype.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_string.h"
#include "svn_mergeinfo.h"
#include "private/svn_mergeinfo_private.h"
#include "svn_private_config.h"
#include "svn_hash.h"

/* Attempt to combine two adjacent or overlapping ranges, IN1 and IN2, and put
   the result in OUTPUT.  Return whether they could be combined.

   CONSIDER_INHERITANCE determines how to account for the inheritability
   of IN1 and IN2 when trying to combine ranges.  If ranges with different
   inheritability are combined (CONSIDER_INHERITANCE must be FALSE for this
   to happen) the result is inheritable.  If both ranges are inheritable the
   result is inheritable.  Only and if both ranges are non-inheritable is
   the result is non-inheritable.

   Range overlapping detection algorithm from
   http://c2.com/cgi-bin/wiki/fullSearch?TestIfDateRangesOverlap
*/
static svn_boolean_t
combine_ranges(svn_merge_range_t **output, svn_merge_range_t *in1,
               svn_merge_range_t *in2,
               svn_boolean_t consider_inheritance)
{
  if (in1->start <= in2->end && in2->start <= in1->end)
    {
      if (!consider_inheritance
          || (consider_inheritance
              && (in1->inheritable == in2->inheritable)))
        {
          (*output)->start = MIN(in1->start, in2->start);
          (*output)->end = MAX(in1->end, in2->end);
          (*output)->inheritable = (in1->inheritable || in2->inheritable);
          return TRUE;
        }
    }
  return FALSE;
}

/* pathname -> PATHNAME */
static svn_error_t *
parse_pathname(const char **input, const char *end,
               svn_stringbuf_t **pathname, apr_pool_t *pool)
{
  const char *curr = *input;
  const char *last_colon = NULL;

  /* A pathname may contain colons, so find the last colon before END
     or newline.  We'll consider this the divider between the pathname
     and the revisionlist. */
  while (curr < end && *curr != '\n')
    {
      if (*curr == ':')
        last_colon = curr;
      curr++;
    }

  if (!last_colon)
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("Pathname not terminated by ':'"));
  if (last_colon == *input)
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("No pathname preceding ':'"));

  *pathname = svn_stringbuf_ncreate(*input, last_colon - *input, pool);
  *input = last_colon;

  return SVN_NO_ERROR;
}

/* Helper for svn_rangelist_merge() and rangelist_intersect_or_remove().

   If *LASTRANGE is not NULL it should point to the last element in REVLIST.
   REVLIST must be sorted from lowest to highest revision and contain no
   overlapping revision ranges.  Any changes made to REVLIST will maintain
   this guarantee.

   If *LASTRANGE is NULL then push MRANGE to REVLIST.

   If *LASTRANGE and MRANGE don't intersect then push MRANGE to REVLIST.
   If they do intersect and have the same inheritability then combine the
   ranges, updating *LASTRANGE to reflect the new combined range.  If the
   ranges intersect but differ in inheritability, then merge the ranges - see
   the doc string for svn_mergeinfo_merge.  This may result in a change to
   *LASTRANGE's end field and the pushing of up to two new ranges on REVLIST.

     e.g.  *LASTRANGE: '4-10*' merged with MRANGE: '6'________
                  |                           |               |
             Update end field               Push       Account for trimmed
                  |                           |        range from *LASTRANGE.
                  |                           |        Push it last to
                  |                           |        maintain sort order.
                  |                           |               |
                  V                           V               V
           *LASTRANGE: '4-5*'              MRANGE: '6'   NEWRANGE: '6-10*'

   Upon return, if any new ranges were pushed onto REVLIST, then set
   *LASTRANGE to the last range pushed.

   CONSIDER_INHERITANCE determines how to account for the inheritability of
   MRANGE and *LASTRANGE when determining if they intersect.  If
   CONSIDER_INHERITANCE is TRUE, then only ranges with the same
   inheritability can intersect and therefore be combined.

   If DUP_MRANGE is TRUE then allocate a copy of MRANGE before pushing it
   onto REVLIST.
*/
static APR_INLINE void
combine_with_lastrange(svn_merge_range_t** lastrange,
                       svn_merge_range_t *mrange, svn_boolean_t dup_mrange,
                       apr_array_header_t *revlist,
                       svn_boolean_t consider_inheritance,
                       apr_pool_t *pool)
{
  svn_merge_range_t *pushed_mrange_1 = NULL;
  svn_merge_range_t *pushed_mrange_2 = NULL;
  svn_boolean_t ranges_intersect = FALSE;
  svn_boolean_t ranges_have_same_inheritance = FALSE;

  if (*lastrange)
    {
      if ((*lastrange)->start <= mrange->end
          && mrange->start <= (*lastrange)->end)
        ranges_intersect = TRUE;
      if ((*lastrange)->inheritable == mrange->inheritable)
        ranges_have_same_inheritance = TRUE;
    }

  if (!(*lastrange)
      || (!ranges_intersect || (!ranges_have_same_inheritance
                                && consider_inheritance)))

    {
      /* No *LASTRANGE
           or
         LASTRANGE and MRANGE don't intersect
           or
         LASTRANGE and MRANGE "intersect" but have different
         inheritability and we are considering inheritance so
         can't combined them...

         ...In all these cases just push MRANGE onto *LASTRANGE. */
      if (dup_mrange)
        pushed_mrange_1 = svn_merge_range_dup(mrange, pool);
      else
        pushed_mrange_1 = mrange;
    }
  else /* MRANGE and *LASTRANGE intersect */
    {
      if (ranges_have_same_inheritance)
        {
          /* Intersecting ranges have the same inheritability
             so just combine them. */
          (*lastrange)->start = MIN((*lastrange)->start, mrange->start);
          (*lastrange)->end = MAX((*lastrange)->end, mrange->end);
          (*lastrange)->inheritable =
            ((*lastrange)->inheritable || mrange->inheritable);
        }
      else /* Ranges intersect but have different
              inheritability so merge the ranges. */
        {
          svn_revnum_t tmp_revnum;

          /* Ranges have same starting revision. */
          if ((*lastrange)->start == mrange->start)
            {
              if ((*lastrange)->end == mrange->end)
                {
                  (*lastrange)->inheritable = TRUE;
                }
              else if ((*lastrange)->end > mrange->end)
                {
                  if (!(*lastrange)->inheritable)
                    {
                      tmp_revnum = (*lastrange)->end;
                      (*lastrange)->end = mrange->end;
                      (*lastrange)->inheritable = TRUE;
                      if (dup_mrange)
                        pushed_mrange_1 = svn_merge_range_dup(mrange, pool);
                      else
                        pushed_mrange_1 = mrange;
                      pushed_mrange_1->start = pushed_mrange_1->start;
                      pushed_mrange_1->end = tmp_revnum;
                      *lastrange = pushed_mrange_1;
                    }
                }
              else /* (*lastrange)->end < mrange->end) */
                {
                  if (mrange->inheritable)
                    {
                      (*lastrange)->inheritable = TRUE;
                      (*lastrange)->end = mrange->end;
                    }
                  else
                    {
                      if (dup_mrange)
                        pushed_mrange_1 = svn_merge_range_dup(mrange, pool);
                      else
                        pushed_mrange_1 = mrange;
                      pushed_mrange_1->start = (*lastrange)->end;
                    }
                }
            }
          /* Ranges have same ending revision. (Same starting
             and ending revisions already handled above.) */
          else if ((*lastrange)->end == mrange->end)
            {
              if ((*lastrange)->start < mrange->start)
                {
                  if (!(*lastrange)->inheritable)
                    {
                      (*lastrange)->end = mrange->start;
                      if (dup_mrange)
                        pushed_mrange_1 = svn_merge_range_dup(mrange, pool);
                      else
                        pushed_mrange_1 = mrange;
                      *lastrange = pushed_mrange_1;
                    }
                }
              else /* (*lastrange)->start > mrange->start */
                {
                  (*lastrange)->start = mrange->start;
                  (*lastrange)->end = mrange->end;
                  (*lastrange)->inheritable = mrange->inheritable;
                  if (dup_mrange)
                    pushed_mrange_1 = svn_merge_range_dup(mrange, pool);
                  else
                    pushed_mrange_1 = mrange;
                  pushed_mrange_1->start = (*lastrange)->end;
                  pushed_mrange_1->inheritable = TRUE;

                }
            }
          else /* Ranges have different starting and ending revisions. */
            {
              if ((*lastrange)->start < mrange->start)
                {
                  /* If MRANGE is a proper subset of *LASTRANGE and
                     *LASTRANGE is inheritable there is nothing more
                     to do. */
                  if (!((*lastrange)->end > mrange->end
                        && (*lastrange)->inheritable))
                    {
                      tmp_revnum = (*lastrange)->end;
                      if (!(*lastrange)->inheritable)
                        (*lastrange)->end = mrange->start;
                      else
                        mrange->start = (*lastrange)->end;
                      if (dup_mrange)
                        pushed_mrange_1 = svn_merge_range_dup(mrange, pool);
                      else
                        pushed_mrange_1 = mrange;

                      if (tmp_revnum > mrange->end)
                        {
                          pushed_mrange_2 =
                            apr_palloc(pool, sizeof(*pushed_mrange_2));
                          pushed_mrange_2->start = mrange->end;
                          pushed_mrange_2->end = tmp_revnum;
                          pushed_mrange_2->inheritable =
                            (*lastrange)->inheritable;
                        }
                      mrange->inheritable = TRUE;
                    }
                }
              else /* ((*lastrange)->start > mrange->start) */
                {
                  pushed_mrange_2 =
                    apr_palloc(pool, sizeof(*pushed_mrange_2));

                  if ((*lastrange)->end < mrange->end)
                    {
                      pushed_mrange_2->start = (*lastrange)->end;
                      pushed_mrange_2->end = mrange->end;
                      pushed_mrange_2->inheritable = mrange->inheritable;

                      tmp_revnum = (*lastrange)->start;
                      (*lastrange)->start = mrange->start;
                      (*lastrange)->end = tmp_revnum;
                      (*lastrange)->inheritable = mrange->inheritable;

                      mrange->start = tmp_revnum;
                      mrange->end = pushed_mrange_2->start;
                      mrange->inheritable = TRUE;
                    }
                  else /* (*lastrange)->end > mrange->end */
                    {
                      pushed_mrange_2->start = mrange->end;
                      pushed_mrange_2->end = (*lastrange)->end;
                      pushed_mrange_2->inheritable =
                        (*lastrange)->inheritable;

                      tmp_revnum = (*lastrange)->start;
                      (*lastrange)->start = mrange->start;
                      (*lastrange)->end = tmp_revnum;
                      (*lastrange)->inheritable = mrange->inheritable;

                      mrange->start = tmp_revnum;
                      mrange->end = pushed_mrange_2->start;
                      mrange->inheritable = TRUE;
                    }
                }
            }
        }
    }
  if (pushed_mrange_1)
    {
      APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = pushed_mrange_1;
      *lastrange = pushed_mrange_1;
    }
  if (pushed_mrange_2)
    {
      APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = pushed_mrange_2;
      *lastrange = pushed_mrange_2;
    }
}

/* Convert a single svn_merge_range_t * back into an svn_string_t *.  */
static svn_error_t *
range_to_string(svn_string_t **result, svn_merge_range_t *range,
                apr_pool_t *pool)
{
  if (range->start == range->end - 1)
    *result = svn_string_createf(pool, "%ld%s", range->end,
                                 range->inheritable
                                 ? "" : SVN_MERGEINFO_NONINHERITABLE_STR);
  else if (range->start - 1 == range->end)
    *result = svn_string_createf(pool, "-%ld%s", range->start,
                                 range->inheritable
                                 ? "" : SVN_MERGEINFO_NONINHERITABLE_STR);
  else if (range->start < range->end)
    *result = svn_string_createf(pool, "%ld-%ld%s", range->start + 1,
                                 range->end, range->inheritable
                                 ? "" : SVN_MERGEINFO_NONINHERITABLE_STR);
  else
    *result = svn_string_createf(pool, "%ld-%ld%s", range->start,
                                 range->end + 1, range->inheritable
                                 ? "" : SVN_MERGEINFO_NONINHERITABLE_STR);
  return SVN_NO_ERROR;
}

/* Helper for svn_mergeinfo_parse()

   revisionlist -> (revisionelement)(COMMA revisionelement)*
   revisionrange -> REVISION "-" REVISION("*")
   revisionelement -> revisionrange | REVISION("*")

   PATHNAME is the path this revisionlist is mapped to.  It is
   used only for producing a more descriptive error message.
*/
static svn_error_t *
parse_revlist(const char **input, const char *end,
              apr_array_header_t *revlist, const char *pathname,
              apr_pool_t *pool)
{
  const char *curr = *input;

  /* Eat any leading horizontal white-space before the rangelist. */
  while (curr < end && *curr != '\n' && isspace(*curr))
    curr++;

  if (*curr == '\n' || curr == end)
    {
      /* Empty range list. */
      *input = curr;
      return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                               _("Mergeinfo for '%s' maps to an "
                                 "empty revision range"), pathname);
    }

  while (curr < end && *curr != '\n')
    {
      /* Parse individual revisions or revision ranges. */
      svn_merge_range_t *mrange = apr_pcalloc(pool, sizeof(*mrange));
      svn_revnum_t firstrev;

      SVN_ERR(svn_revnum_parse(&firstrev, curr, &curr));
      if (*curr != '-' && *curr != '\n' && *curr != ',' && *curr != '*'
          && curr != end)
        return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                 _("Invalid character '%c' found in revision "
                                   "list"), *curr);
      mrange->start = firstrev - 1;
      mrange->end = firstrev;
      mrange->inheritable = TRUE;

      if (*curr == '-')
        {
          svn_revnum_t secondrev;

          curr++;
          SVN_ERR(svn_revnum_parse(&secondrev, curr, &curr));
          if (firstrev > secondrev)
            return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                     _("Unable to parse reversed revision "
                                       "range '%ld-%ld'"),
                                       firstrev, secondrev);
          else if (firstrev == secondrev)
            return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                     _("Unable to parse revision range "
                                       "'%ld-%ld' with same start and end "
                                       "revisions"), firstrev, secondrev);
          mrange->end = secondrev;
        }

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
      else if (*curr == '*')
        {
          mrange->inheritable = FALSE;
          curr++;
          if (*curr == ',' || *curr == '\n' || curr == end)
            {
              APR_ARRAY_PUSH(revlist, svn_merge_range_t *) = mrange;
              if (*curr == ',')
                {
                  curr++;
                }
              else
                {
                  *input = curr;
                  return SVN_NO_ERROR;
                }
            }
          else
            {
              return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                       _("Invalid character '%c' found in "
                                         "range list"), *curr);
            }
        }
      else
        {
          return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                   _("Invalid character '%c' found in "
                                     "range list"), *curr);
        }

    }
  if (*curr != '\n')
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("Range list parsing ended before hitting "
                              "newline"));
  *input = curr;
  return SVN_NO_ERROR;
}

/* revisionline -> PATHNAME COLON revisionlist */
static svn_error_t *
parse_revision_line(const char **input, const char *end, svn_mergeinfo_t hash,
                    apr_pool_t *pool)
{
  svn_stringbuf_t *pathname;
  apr_array_header_t *revlist = apr_array_make(pool, 1,
                                               sizeof(svn_merge_range_t *));

  SVN_ERR(parse_pathname(input, end, &pathname, pool));

  if (*(*input) != ':')
    return svn_error_create(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                            _("Pathname not terminated by ':'"));

  *input = *input + 1;

  SVN_ERR(parse_revlist(input, end, revlist, pathname->data, pool));

  if (*input != end && *(*input) != '\n')
    return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                             _("Could not find end of line in range list line "
                               "in '%s'"), *input);

  if (*input != end)
    *input = *input + 1;

  /* Sort the rangelist, combine adjacent ranges into single ranges,
     and make sure there are no overlapping ranges. */
  if (revlist->nelts > 1)
    {
      int i;
      svn_merge_range_t *range, *lastrange;

      qsort(revlist->elts, revlist->nelts, revlist->elt_size,
        svn_sort_compare_ranges);
      lastrange = APR_ARRAY_IDX(revlist, 0, svn_merge_range_t *);

      for (i = 1; i < revlist->nelts; i++)
        {
          range = APR_ARRAY_IDX(revlist, i, svn_merge_range_t *);
          if (lastrange->start <= range->end
              && range->start <= lastrange->end)
            {
              /* The ranges are adjacent or intersect. */

              /* svn_mergeinfo_parse promises to combine overlapping
                 ranges as long as their inheritability is the same. */
              if (range->start < lastrange->end
                  && range->inheritable != lastrange->inheritable)
                {
                  svn_string_t *r1, *r2;

                  SVN_ERR(range_to_string(&r1, lastrange, pool));
                  SVN_ERR(range_to_string(&r2, range, pool));
                  return svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, NULL,
                                           _("Unable to parse overlapping "
                                             "revision ranges '%s' and '%s' "
                                             "with different inheritance "
                                             "types"), r1->data, r2->data);
                }

              /* Combine overlapping or adjacent ranges with the
                 same inheritability. */
              if (lastrange->inheritable == range->inheritable)
                {
                  lastrange->end = MAX(range->end, lastrange->end);
                  if (i + 1 < revlist->nelts)
                    memmove(revlist->elts + (revlist->elt_size * i),
                            revlist->elts + (revlist->elt_size * (i + 1)),
                            revlist->elt_size * (revlist->nelts - i));
                  revlist->nelts--;
                  i--;
                }
            }
          lastrange = APR_ARRAY_IDX(revlist, i, svn_merge_range_t *);
        }
    }
  apr_hash_set(hash, pathname->data, APR_HASH_KEY_STRING, revlist);

  return SVN_NO_ERROR;
}

/* top -> revisionline (NEWLINE revisionline)*  */
static svn_error_t *
parse_top(const char **input, const char *end, svn_mergeinfo_t hash,
          apr_pool_t *pool)
{
  while (*input < end)
    SVN_ERR(parse_revision_line(input, end, hash, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_parse(svn_mergeinfo_t *mergeinfo,
                    const char *input,
                    apr_pool_t *pool)
{
  svn_error_t *err;

  *mergeinfo = apr_hash_make(pool);
  err = parse_top(&input, input + strlen(input), *mergeinfo, pool);

  /* Always return SVN_ERR_MERGEINFO_PARSE_ERROR as the topmost error. */
  if (err && err->apr_err != SVN_ERR_MERGEINFO_PARSE_ERROR)
    err = svn_error_createf(SVN_ERR_MERGEINFO_PARSE_ERROR, err,
                            _("Could not parse mergeinfo string '%s'"),
                            input);
  return err;
}


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
          /* Only when merging two non-inheritable ranges is the result also
             non-inheritable.  In all other cases ensure an inheritiable
             result. */
          if (elt1->inheritable || elt2->inheritable)
            elt1->inheritable = TRUE;
          combine_with_lastrange(&lastrange, elt1, TRUE, output,
                                 FALSE, pool);
          i++;
          j++;
        }
      else if (res < 0)
        {
          combine_with_lastrange(&lastrange, elt1, TRUE, output,
                                 FALSE, pool);
          i++;
        }
      else
        {
          combine_with_lastrange(&lastrange, elt2, TRUE, output,
                                 FALSE, pool);
          j++;
        }
    }
  /* Copy back any remaining elements.
     Only one of these loops should end up running, if anything. */

  SVN_ERR_ASSERT(!(i < (*rangelist)->nelts && j < changes->nelts));

  for (; i < (*rangelist)->nelts; i++)
    {
      svn_merge_range_t *elt = APR_ARRAY_IDX(*rangelist, i,
                                             svn_merge_range_t *);
      combine_with_lastrange(&lastrange, elt, TRUE, output,
                             FALSE, pool);
    }


  for (; j < changes->nelts; j++)
    {
      svn_merge_range_t *elt = APR_ARRAY_IDX(changes, j, svn_merge_range_t *);
      combine_with_lastrange(&lastrange, elt, TRUE, output,
                             FALSE, pool);
    }

  *rangelist = output;
  return SVN_NO_ERROR;
}

static svn_boolean_t
range_intersect(svn_merge_range_t *first, svn_merge_range_t *second,
                svn_boolean_t consider_inheritance)
{
  return (first->start + 1 <= second->end)
    && (second->start + 1 <= first->end)
    && (!consider_inheritance
        || (!(first->inheritable) == !(second->inheritable)));
}

static svn_boolean_t
range_contains(svn_merge_range_t *first, svn_merge_range_t *second,
               svn_boolean_t consider_inheritance)
{
  return (first->start <= second->start) && (second->end <= first->end)
    && (!consider_inheritance
        || (!(first->inheritable) == !(second->inheritable)));
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
                              svn_boolean_t consider_inheritance,
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
          lasti = i;
        }

      elt1 = &wboardelt;

      /* If the whiteboard range is contained completely in the
         eraser, we increment the whiteboard.
         If the ranges intersect, and match exactly, we increment both
         eraser and whiteboard.
         Otherwise, we have to generate a range for the left part of
         the removal of eraser from whiteboard, and possibly change
         the whiteboard to the remaining portion of the right part of
         the removal, to test against. */
      if (range_contains(elt2, elt1, consider_inheritance))
        {
          if (!do_remove)
              combine_with_lastrange(&lastrange, elt1, TRUE, *output,
                                     consider_inheritance, pool);

          i++;

          if (elt1->start == elt2->start && elt1->end == elt2->end)
            j++;
        }
      else if (range_intersect(elt2, elt1, consider_inheritance))
        {
          if (elt1->start < elt2->start)
            {
              /* The whiteboard range starts before the eraser range. */
              svn_merge_range_t tmp_range;
              tmp_range.inheritable = elt1->inheritable;
              if (do_remove)
                {
                  /* Retain the range that falls before the eraser start. */
                  tmp_range.start = elt1->start;
                  tmp_range.end = elt2->start;
                }
              else
                {
                  /* Retain the range that falls between the eraser
                     start and whiteboard end. */
                  tmp_range.start = elt2->start;
                  tmp_range.end = MIN(elt1->end, elt2->end);
                }

              combine_with_lastrange(&lastrange, &tmp_range, TRUE,
                                     *output, consider_inheritance, pool);
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
                  tmp_range.start = MAX(elt1->start, elt2->start);
                  tmp_range.end = elt2->end;
                  tmp_range.inheritable = elt1->inheritable;
                  combine_with_lastrange(&lastrange, &tmp_range, TRUE,
                                         *output, consider_inheritance, pool);
                }

              wboardelt.start = elt2->end;
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
              if (do_remove && !(lastrange &&
                                 combine_ranges(&lastrange, lastrange, elt1,
                                                consider_inheritance)))
                {
                  lastrange = svn_merge_range_dup(elt1, pool);
                  APR_ARRAY_PUSH(*output, svn_merge_range_t *) = lastrange;
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
          combine_with_lastrange(&lastrange, &wboardelt, TRUE, *output,
                                 consider_inheritance, pool);
          i++;
        }

      /* Copy any other remaining untouched whiteboard elements.  */
      for (; i < whiteboard->nelts; i++)
        {
          svn_merge_range_t *elt = APR_ARRAY_IDX(whiteboard, i,
                                                 svn_merge_range_t *);

          combine_with_lastrange(&lastrange, elt, TRUE, *output,
                                 consider_inheritance, pool);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_rangelist_intersect(apr_array_header_t **output,
                        apr_array_header_t *rangelist1,
                        apr_array_header_t *rangelist2,
                        svn_boolean_t consider_inheritance,
                        apr_pool_t *pool)
{
  return rangelist_intersect_or_remove(output, rangelist1, rangelist2, FALSE,
                                       consider_inheritance, pool);
}

svn_error_t *
svn_rangelist_remove(apr_array_header_t **output,
                     apr_array_header_t *eraser,
                     apr_array_header_t *whiteboard,
                     svn_boolean_t consider_inheritance,
                     apr_pool_t *pool)
{
  return rangelist_intersect_or_remove(output, eraser, whiteboard, TRUE,
                                       consider_inheritance, pool);
}

svn_error_t *
svn_rangelist_diff(apr_array_header_t **deleted, apr_array_header_t **added,
                   apr_array_header_t *from, apr_array_header_t *to,
                   svn_boolean_t consider_inheritance,
                   apr_pool_t *pool)
{
  /* The following diagrams illustrate some common range delta scenarios:

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

  /* The items that are present in from, but not in to, must have been
     deleted. */
  SVN_ERR(svn_rangelist_remove(deleted, to, from, consider_inheritance,
                               pool));
  /* The items that are present in to, but not in from, must have been
     added.  */
  return svn_rangelist_remove(added, from, to, consider_inheritance, pool);
}

struct mergeinfo_diff_baton
{
  svn_mergeinfo_t from;
  svn_mergeinfo_t to;
  svn_mergeinfo_t deleted;
  svn_mergeinfo_t added;
  svn_boolean_t consider_inheritance;
  apr_pool_t *pool;
};

/* This implements the 'svn_hash_diff_func_t' interface.
   BATON is of type 'struct mergeinfo_diff_baton *'.
*/
static svn_error_t *
mergeinfo_hash_diff_cb(const void *key, apr_ssize_t klen,
                       enum svn_hash_diff_key_status status,
                       void *baton)
{
  /* hash_a is FROM mergeinfo,
     hash_b is TO mergeinfo. */
  struct mergeinfo_diff_baton *cb = baton;
  apr_array_header_t *from_rangelist, *to_rangelist;
  const char *path = key;
  if (status == svn_hash_diff_key_both)
    {
      /* Record any deltas (additions or deletions). */
      apr_array_header_t *deleted_rangelist, *added_rangelist;
      from_rangelist = apr_hash_get(cb->from, path, APR_HASH_KEY_STRING);
      to_rangelist = apr_hash_get(cb->to, path, APR_HASH_KEY_STRING);
      svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                         from_rangelist, to_rangelist,
                         cb->consider_inheritance, cb->pool);
      if (cb->deleted && deleted_rangelist->nelts > 0)
        apr_hash_set(cb->deleted, apr_pstrdup(cb->pool, path),
                     APR_HASH_KEY_STRING, deleted_rangelist);
      if (cb->added && added_rangelist->nelts > 0)
        apr_hash_set(cb->added, apr_pstrdup(cb->pool, path),
                     APR_HASH_KEY_STRING, added_rangelist);
    }
  else if ((status == svn_hash_diff_key_a) && cb->deleted)
    {
      from_rangelist = apr_hash_get(cb->from, path, APR_HASH_KEY_STRING);
      apr_hash_set(cb->deleted, apr_pstrdup(cb->pool, path),
                   APR_HASH_KEY_STRING,
                   svn_rangelist_dup(from_rangelist, cb->pool));
    }
  else if ((status == svn_hash_diff_key_b) && cb->added)
    {
      to_rangelist = apr_hash_get(cb->to, path, APR_HASH_KEY_STRING);
      apr_hash_set(cb->added, apr_pstrdup(cb->pool, path), APR_HASH_KEY_STRING,
                   svn_rangelist_dup(to_rangelist, cb->pool));
    }
  return SVN_NO_ERROR;
}

/* Record deletions and additions of entire range lists (by path
   presence), and delegate to svn_rangelist_diff() for delta
   calculations on a specific path. */
static svn_error_t *
walk_mergeinfo_hash_for_diff(svn_mergeinfo_t from, svn_mergeinfo_t to,
                             svn_mergeinfo_t deleted, svn_mergeinfo_t added,
                             svn_boolean_t consider_inheritance,
                             apr_pool_t *pool)
{
  struct mergeinfo_diff_baton mdb;
  mdb.from = from;
  mdb.to = to;
  mdb.deleted = deleted;
  mdb.added = added;
  mdb.consider_inheritance = consider_inheritance;
  mdb.pool = pool;

  return svn_hash_diff(from, to, mergeinfo_hash_diff_cb, &mdb, pool);
}

svn_error_t *
svn_mergeinfo_diff(svn_mergeinfo_t *deleted, svn_mergeinfo_t *added,
                   svn_mergeinfo_t from, svn_mergeinfo_t to,
                   svn_boolean_t consider_inheritance,
                   apr_pool_t *pool)
{
  if (from && to == NULL)
    {
      *deleted = svn_mergeinfo_dup(from, pool);
      *added = apr_hash_make(pool);
    }
  else if (from == NULL && to)
    {
      *deleted = apr_hash_make(pool);
      *added = svn_mergeinfo_dup(to, pool);
    }
  else
    {
      *deleted = apr_hash_make(pool);
      *added = apr_hash_make(pool);

      if (from && to)
        {
          SVN_ERR(walk_mergeinfo_hash_for_diff(from, to, *deleted, *added,
                                               consider_inheritance, pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__equals(svn_boolean_t *is_equal,
                      svn_mergeinfo_t info1,
                      svn_mergeinfo_t info2,
                      svn_boolean_t consider_inheritance,
                      apr_pool_t *pool)
{
  if (apr_hash_count(info1) == apr_hash_count(info2))
    {
      svn_mergeinfo_t deleted, added;
      SVN_ERR(svn_mergeinfo_diff(&deleted, &added, info1, info2,
                                 consider_inheritance, pool));
      *is_equal = apr_hash_count(deleted) == 0 && apr_hash_count(added) == 0;
    }
  else
    {
      *is_equal = FALSE;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_merge(svn_mergeinfo_t mergeinfo, svn_mergeinfo_t changes,
                    apr_pool_t *pool)
{
  apr_array_header_t *sorted1, *sorted2;
  int i, j;

  sorted1 = svn_sort__hash(mergeinfo, svn_sort_compare_items_as_paths, pool);
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

          SVN_ERR(svn_rangelist_merge(&rl1, rl2,
                                      pool));
          apr_hash_set(mergeinfo, elt1.key, elt1.klen, rl1);
          i++;
          j++;
        }
      else if (res < 0)
        {
          i++;
        }
      else
        {
          apr_hash_set(mergeinfo, elt2.key, elt2.klen, elt2.value);
          j++;
        }
    }

  /* Copy back any remaining elements from the second hash. */
  for (; j < sorted2->nelts; j++)
    {
      svn_sort__item_t elt = APR_ARRAY_IDX(sorted2, j, svn_sort__item_t);
      apr_hash_set(mergeinfo, elt.key, elt.klen, elt.value);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_intersect(svn_mergeinfo_t *mergeinfo,
                        svn_mergeinfo_t mergeinfo1,
                        svn_mergeinfo_t mergeinfo2,
                        apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *mergeinfo = apr_hash_make(pool);

  /* ### TODO(reint): Do we care about the case when a path in one
     ### mergeinfo hash has inheritable mergeinfo, and in the other
     ### has non-inhertiable mergeinfo?  It seems like that path
     ### itself should really be an intersection, while child paths
     ### should not be... */
  for (hi = apr_hash_first(apr_hash_pool_get(mergeinfo1), mergeinfo1);
       hi; hi = apr_hash_next(hi))
    {
      apr_array_header_t *rangelist;
      const void *path;
      void *val;
      apr_hash_this(hi, &path, NULL, &val);

      rangelist = apr_hash_get(mergeinfo2, path, APR_HASH_KEY_STRING);
      if (rangelist)
        {
          SVN_ERR(svn_rangelist_intersect(&rangelist,
                                          (apr_array_header_t *) val,
                                          rangelist, TRUE, pool));
          if (rangelist->nelts > 0)
            apr_hash_set(*mergeinfo,
                         apr_pstrdup(pool, path),
                         APR_HASH_KEY_STRING, rangelist);
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_remove(svn_mergeinfo_t *mergeinfo, svn_mergeinfo_t eraser,
                     svn_mergeinfo_t whiteboard, apr_pool_t *pool)
{
  *mergeinfo = apr_hash_make(pool);
  return walk_mergeinfo_hash_for_diff(whiteboard, eraser, *mergeinfo, NULL,
                                      TRUE, pool);
}

svn_error_t *
svn_rangelist_to_string(svn_string_t **output,
                        const apr_array_header_t *rangelist,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *buf = svn_stringbuf_create("", pool);

  if (rangelist->nelts > 0)
    {
      int i;
      svn_merge_range_t *range;
      svn_string_t *toappend;

      /* Handle the elements that need commas at the end.  */
      for (i = 0; i < rangelist->nelts - 1; i++)
        {
          range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
          SVN_ERR(range_to_string(&toappend, range, pool));
          svn_stringbuf_appendcstr(buf, toappend->data);
          svn_stringbuf_appendcstr(buf, ",");
        }

      /* Now handle the last element, which needs no comma.  */
      range = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      SVN_ERR(range_to_string(&toappend, range, pool));
      svn_stringbuf_appendcstr(buf, toappend->data);
    }

  *output = svn_string_create_from_buf(buf, pool);

  return SVN_NO_ERROR;
}

/* Converts a mergeinfo INPUT to an unparsed mergeinfo in OUTPUT.  If PREFIX
   is not NULL then prepend PREFIX to each line in OUTPUT.  If INPUT contains
   no elements, return the empty string.
 */
static svn_error_t *
mergeinfo_to_stringbuf(svn_stringbuf_t **output,
                       svn_mergeinfo_t input,
                       const char *prefix,
                       apr_pool_t *pool)
{
  *output = svn_stringbuf_create("", pool);

  if (apr_hash_count(input) > 0)
    {
      apr_array_header_t *sorted =
        svn_sort__hash(input, svn_sort_compare_items_as_paths, pool);
      int i;

      for (i = 0; i < sorted->nelts; i++)
        {
          svn_sort__item_t elt = APR_ARRAY_IDX(sorted, i, svn_sort__item_t);
          svn_string_t *revlist;

          SVN_ERR(svn_rangelist_to_string(&revlist, elt.value, pool));
          svn_stringbuf_appendcstr(*output,
                                   apr_psprintf(pool, "%s%s:%s",
                                                prefix ? prefix : "",
                                                (const char *) elt.key,
                                                revlist->data));
          if (i < sorted->nelts - 1)
            svn_stringbuf_appendcstr(*output, "\n");
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_to_string(svn_string_t **output, svn_mergeinfo_t input,
                        apr_pool_t *pool)
{
  if (apr_hash_count(input) > 0)
    {
      svn_stringbuf_t *mergeinfo_buf;
      SVN_ERR(mergeinfo_to_stringbuf(&mergeinfo_buf, input, NULL, pool));
      *output = svn_string_create_from_buf(mergeinfo_buf, pool);
    }
  else
    {
      *output = svn_string_create("", pool);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo_sort(svn_mergeinfo_t input, apr_pool_t *pool)
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

svn_mergeinfo_catalog_t
svn_mergeinfo_catalog_dup(svn_mergeinfo_catalog_t mergeinfo_catalog,
                          apr_pool_t *pool)
{
  svn_mergeinfo_t new_mergeinfo_catalog = apr_hash_make(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, mergeinfo_catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      apr_hash_this(hi, &key, NULL, &val);
      apr_hash_set(new_mergeinfo_catalog,
                   apr_pstrdup(pool, key),
                   APR_HASH_KEY_STRING,
                   svn_mergeinfo_dup(val, pool));
    }

  return new_mergeinfo_catalog;
}

svn_mergeinfo_t
svn_mergeinfo_dup(svn_mergeinfo_t mergeinfo, apr_pool_t *pool)
{
  svn_mergeinfo_t new_mergeinfo = apr_hash_make(pool);
  apr_hash_index_t *hi;
  const void *path;
  apr_ssize_t pathlen;
  void *rangelist;

  for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      apr_hash_this(hi, &path, &pathlen, &rangelist);
      apr_hash_set(new_mergeinfo, apr_pstrmemdup(pool, path, pathlen), pathlen,
                   svn_rangelist_dup((apr_array_header_t *) rangelist, pool));
    }

  return new_mergeinfo;
}

svn_error_t *
svn_mergeinfo_inheritable(svn_mergeinfo_t *output,
                          svn_mergeinfo_t mergeinfo,
                          const char *path,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *key;
  apr_ssize_t keylen;
  void *rangelist;

  svn_mergeinfo_t inheritable_mergeinfo = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
    {
      apr_array_header_t *inheritable_rangelist;
      apr_hash_this(hi, &key, &keylen, &rangelist);
      if (!path || svn_path_compare_paths(path, (const char *)key) == 0)
        SVN_ERR(svn_rangelist_inheritable(&inheritable_rangelist,
                                          (apr_array_header_t *) rangelist,
                                          start, end, pool));
      else
        inheritable_rangelist =
          svn_rangelist_dup((apr_array_header_t *)rangelist, pool);

      /* Only add this rangelist if some ranges remain.  A rangelist with
         a path mapped to an empty rangelist is not syntactically valid */
      if (inheritable_rangelist->nelts)
        apr_hash_set(inheritable_mergeinfo,
                     apr_pstrmemdup(pool, key, keylen), keylen,
                     inheritable_rangelist);
    }
  *output = inheritable_mergeinfo;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_rangelist_inheritable(apr_array_header_t **inheritable_rangelist,
                          apr_array_header_t *rangelist,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          apr_pool_t *pool)
{
  *inheritable_rangelist = apr_array_make(pool, 1,
                                          sizeof(svn_merge_range_t *));
  if (rangelist->nelts)
    {
      if (!SVN_IS_VALID_REVNUM(start)
          || !SVN_IS_VALID_REVNUM(end)
          || end < start)
        {
          int i;
          /* We want all non-inheritable ranges removed. */
          for (i = 0; i < rangelist->nelts; i++)
            {
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                       svn_merge_range_t *);
              if (range->inheritable)
                {
                  svn_merge_range_t *inheritable_range =
                    apr_palloc(pool, sizeof(*inheritable_range));
                  inheritable_range->start = range->start;
                  inheritable_range->end = range->end;
                  inheritable_range->inheritable = TRUE;
                  APR_ARRAY_PUSH(*inheritable_rangelist,
                                 svn_merge_range_t *) = range;
                }
            }
        }
      else
        {
          /* We want only the non-inheritable ranges bound by START
             and END removed. */
          apr_array_header_t *ranges_inheritable =
            apr_array_make(pool, 0, sizeof(svn_merge_range_t *));
          svn_merge_range_t *range = apr_palloc(pool, sizeof(*range));

          range->start = start;
          range->end = end;
          range->inheritable = FALSE;
          APR_ARRAY_PUSH(ranges_inheritable, svn_merge_range_t *) = range;

          if (rangelist->nelts)
            SVN_ERR(svn_rangelist_remove(inheritable_rangelist,
                                         ranges_inheritable,
                                         rangelist,
                                         TRUE,
                                         pool));
        }
    }
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_mergeinfo__remove_empty_rangelists(svn_mergeinfo_t mergeinfo,
                                       apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_boolean_t removed_some_ranges = FALSE;

  if (mergeinfo)
    {
      for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *value;
          const char *path;
          apr_array_header_t *rangelist;

          apr_hash_this(hi, &key, NULL, &value);
          path = key;
          rangelist = value;

          if (rangelist->nelts == 0)
            {
              apr_hash_set(mergeinfo, path, APR_HASH_KEY_STRING, NULL);
              removed_some_ranges = TRUE;
            }
        }
    }
  return removed_some_ranges;
}

svn_error_t *
svn_mergeinfo__remove_prefix_from_catalog(svn_mergeinfo_catalog_t *out_catalog,
                                          svn_mergeinfo_catalog_t in_catalog,
                                          const char *prefix,
                                          apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  int prefix_len = strlen(prefix);

  *out_catalog = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, in_catalog); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *original_path;
      void *value;
      apr_ssize_t klen;

      apr_hash_this(hi, &key, &klen, &value);
      original_path = key;
      SVN_ERR_ASSERT(klen >= prefix_len);
      SVN_ERR_ASSERT(strncmp(key, prefix, prefix_len) == 0);

      apr_hash_set(*out_catalog, original_path + prefix_len,
                   klen-prefix_len, value);
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
      APR_ARRAY_PUSH(new_rl, svn_merge_range_t *) =
        svn_merge_range_dup(APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *),
                            pool);
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

svn_boolean_t
svn_merge_range_contains_rev(svn_merge_range_t *range, svn_revnum_t rev)
{
  assert(SVN_IS_VALID_REVNUM(range->start));
  assert(SVN_IS_VALID_REVNUM(range->end));
  assert(range->start != range->end);

  if (range->start < range->end)
    return rev > range->start && rev <= range->end;
  else
    return rev > range->end && rev <= range->start;
}

svn_error_t *
svn_mergeinfo__catalog_to_formatted_string(svn_string_t **output,
                                           svn_mergeinfo_catalog_t catalog,
                                           const char *key_prefix,
                                           const char *val_prefix,
                                           apr_pool_t *pool)
{
  svn_stringbuf_t *output_buf = NULL;

  if (catalog && apr_hash_count(catalog))
    {
      int i;
      apr_array_header_t *sorted_catalog =
        svn_sort__hash(catalog, svn_sort_compare_items_as_paths, pool);

      output_buf = svn_stringbuf_create("", pool);
      for (i = 0; i < sorted_catalog->nelts; i++)
        {
          svn_sort__item_t elt =
            APR_ARRAY_IDX(sorted_catalog, i, svn_sort__item_t);
          const char *path1;
          svn_mergeinfo_t mergeinfo;
          svn_stringbuf_t *mergeinfo_output_buf;

          path1 = elt.key;
          mergeinfo = elt.value;
          if (key_prefix)
            svn_stringbuf_appendcstr(output_buf, key_prefix);
          svn_stringbuf_appendcstr(output_buf, path1);
          svn_stringbuf_appendcstr(output_buf, "\n");
          SVN_ERR(mergeinfo_to_stringbuf(&mergeinfo_output_buf, mergeinfo,
                                         val_prefix ? val_prefix : "", pool));
          svn_stringbuf_appendstr(output_buf, mergeinfo_output_buf);
          svn_stringbuf_appendcstr(output_buf, "\n");
        }
    }
#if SVN_DEBUG
  else if (!catalog)
    {
      output_buf = svn_stringbuf_create(key_prefix ? key_prefix : "", pool);
      svn_stringbuf_appendcstr(output_buf, _("NULL mergeinfo catalog\n"));
    }
  else if (apr_hash_count(catalog) == 0)
    {
      output_buf = svn_stringbuf_create(key_prefix ? key_prefix : "", pool);
      svn_stringbuf_appendcstr(output_buf, _("empty mergeinfo catalog\n"));
    }
#endif

  /* If we have an output_buf, convert it to an svn_string_t;
     otherwise, return a new string containing only a newline
     character.  */
  if (output_buf)
    *output = svn_string_create_from_buf(output_buf, pool);
  else
    *output = svn_string_create("\n", pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__to_formatted_string(svn_string_t **output,
                                   svn_mergeinfo_t mergeinfo,
                                   const char *prefix,
                                   apr_pool_t *pool)
{
  svn_stringbuf_t *output_buf = NULL;

  if (mergeinfo && apr_hash_count(mergeinfo))
    {
      SVN_ERR(mergeinfo_to_stringbuf(&output_buf, mergeinfo,
                                     prefix ? prefix : "", pool));
      svn_stringbuf_appendcstr(output_buf, "\n");
    }
#if SVN_DEBUG
  else if (!mergeinfo)
    {
      output_buf = svn_stringbuf_create(prefix ? prefix : "", pool);
      svn_stringbuf_appendcstr(output_buf, _("NULL mergeinfo\n"));
    }
  else if (apr_hash_count(mergeinfo) == 0)
    {
      output_buf = svn_stringbuf_create(prefix ? prefix : "", pool);
      svn_stringbuf_appendcstr(output_buf, _("empty mergeinfo\n"));
    }
#endif

  *output = output_buf ? svn_string_create_from_buf(output_buf, pool)
                       : svn_string_create("", pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__get_range_endpoints(svn_revnum_t *youngest_rev,
                                   svn_revnum_t *oldest_rev,
                                   svn_mergeinfo_t mergeinfo,
                                   apr_pool_t *pool)
{
  *youngest_rev = *oldest_rev = SVN_INVALID_REVNUM;
  if (mergeinfo)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *value;
          const char *path;
          apr_array_header_t *rangelist;

          apr_hash_this(hi, &key, NULL, &value);
          path = key;
          rangelist = value;

          if (rangelist->nelts)
            {
              svn_merge_range_t *range = APR_ARRAY_IDX(rangelist,
                                                       rangelist->nelts - 1,
                                                       svn_merge_range_t *);
              if (!SVN_IS_VALID_REVNUM(*youngest_rev)
                  || (range->end > *youngest_rev))
                *youngest_rev = range->end;

              range = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);
              if (!SVN_IS_VALID_REVNUM(*oldest_rev)
                  || (range->start < *oldest_rev))
                *oldest_rev = range->start;
            }
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__filter_catalog_by_ranges(svn_mergeinfo_catalog_t *filtered_cat,
                                        svn_mergeinfo_catalog_t catalog,
                                        svn_revnum_t youngest_rev,
                                        svn_revnum_t oldest_rev,
                                        apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *filtered_cat = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const char *path;

      svn_mergeinfo_t mergeinfo, filtered_mergeinfo;
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      mergeinfo = val;
      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(&filtered_mergeinfo,
                                                        mergeinfo,
                                                        youngest_rev,
                                                        oldest_rev,
                                                        pool));
      if (apr_hash_count(filtered_mergeinfo))
        apr_hash_set(*filtered_cat,
                     apr_pstrdup(pool, path),
                     APR_HASH_KEY_STRING,
                     filtered_mergeinfo);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mergeinfo__filter_mergeinfo_by_ranges(svn_mergeinfo_t *filtered_mergeinfo,
                                          svn_mergeinfo_t mergeinfo,
                                          svn_revnum_t youngest_rev,
                                          svn_revnum_t oldest_rev,
                                          apr_pool_t *pool)
{
  *filtered_mergeinfo = apr_hash_make(pool);

  if (mergeinfo)
    {
      apr_hash_index_t *hi;
      svn_merge_range_t *range = apr_palloc(pool, sizeof(*range));
      apr_array_header_t *filter_rangelist =
        apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

      range->start = oldest_rev;
      range->end = youngest_rev;
      range->inheritable = TRUE;
      APR_ARRAY_PUSH(filter_rangelist, svn_merge_range_t *) = range;

      for (hi = apr_hash_first(pool, mergeinfo); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *value;
          const char *path;
          apr_array_header_t *rangelist;

          apr_hash_this(hi, &key, NULL, &value);
          path = key;
          rangelist = value;

          if (rangelist->nelts)
            {
              apr_array_header_t *new_rangelist;

              svn_rangelist_intersect(&new_rangelist, rangelist,
                                      filter_rangelist, FALSE, pool);
              if (new_rangelist->nelts)
                apr_hash_set(*filtered_mergeinfo,
                             apr_pstrdup(pool, path),
                             APR_HASH_KEY_STRING,
                             new_rangelist);
            }
        }
    }
  return SVN_NO_ERROR;
}
