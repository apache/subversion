/*
 * opt.c :  parsing revision and date options.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <string.h>

#include <apr_general.h>

#include "svn_types.h"
#include "svn_opt.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "svn_time.h"
#include "svn_ctype.h"

#include "private/svn_opt_private.h"

#include "svn_private_config.h"


/** Parsing "X:Y"-style arguments. **/

/* If WORD matches one of the special revision descriptors,
 * case-insensitively, set *REVISION accordingly:
 *
 *   - For "head", set REVISION->kind to svn_opt_revision_head.
 *
 *   - For "prev", set REVISION->kind to svn_opt_revision_previous.
 *
 *   - For "base", set REVISION->kind to svn_opt_revision_base.
 *
 *   - For "committed", set REVISION->kind to svn_opt_revision_committed.
 *
 * If match, return 0, else return -1 and don't touch REVISION.
 */
static int
revision_from_word(svn_opt_revision_t *revision, const char *word)
{
  if (svn_cstring_casecmp(word, "head") == 0)
    {
      revision->kind = svn_opt_revision_head;
    }
  else if (svn_cstring_casecmp(word, "prev") == 0)
    {
      revision->kind = svn_opt_revision_previous;
    }
  else if (svn_cstring_casecmp(word, "base") == 0)
    {
      revision->kind = svn_opt_revision_base;
    }
  else if (svn_cstring_casecmp(word, "committed") == 0)
    {
      revision->kind = svn_opt_revision_committed;
    }
  else
    return -1;

  return 0;
}


/* Parse one revision specification.  Return pointer to character
   after revision, or NULL if the revision is invalid.  Modifies
   str, so make sure to pass a copy of anything precious.  Uses
   POOL for temporary allocation. */
static char *parse_one_rev(svn_opt_revision_t *revision, char *str,
                           apr_pool_t *pool)
{
  char *end, save;

  /* Allow any number of 'r's to prefix a revision number, because
     that way if a script pastes svn output into another svn command
     (like "svn log -r${REV_COPIED_FROM_OUTPUT}"), it'll Just Work,
     even when compounded.

     As it happens, none of our special revision words begins with
     "r".  If any ever do, then this code will have to get smarter.

     Incidentally, this allows "r{DATE}".  We could avoid that with
     some trivial code rearrangement, but it's not clear what would
     be gained by doing so. */
  while (*str == 'r')
    str++;

  if (*str == '{')
    {
      svn_boolean_t matched;
      apr_time_t tm;
      svn_error_t *err;

      /* Brackets denote a date. */
      str++;
      end = strchr(str, '}');
      if (!end)
        return NULL;
      *end = '\0';
      err = svn_parse_date(&matched, &tm, str, apr_time_now(), pool);
      if (err)
        {
          svn_error_clear(err);
          return NULL;
        }
      if (!matched)
        return NULL;
      revision->kind = svn_opt_revision_date;
      revision->value.date = tm;
      return end + 1;
    }
  else if (svn_ctype_isdigit(*str))
    {
      /* It's a number. */
      end = str + 1;
      while (svn_ctype_isdigit(*end))
        end++;
      save = *end;
      *end = '\0';
      revision->kind = svn_opt_revision_number;
      revision->value.number = SVN_STR_TO_REV(str);
      *end = save;
      return end;
    }
  else if (svn_ctype_isalpha(*str))
    {
      end = str + 1;
      while (svn_ctype_isalpha(*end))
        end++;
      save = *end;
      *end = '\0';
      if (revision_from_word(revision, str) != 0)
        return NULL;
      *end = save;
      return end;
    }
  else
    return NULL;
}

svn_error_t *
svn_opt_parse_one_revision(svn_opt_revision_t *revision,
                           const char *arg,
                           apr_pool_t *scratch_pool)
{
  /* copy because parse_one_rev() will mess up the string */
  char *str = apr_pstrdup(scratch_pool, arg);
  char *end = parse_one_rev(revision, str, scratch_pool);

  if (! end || *end != '\0')
    return svn_error_createf(SVN_ERR_OPT_REVISION_PARSE_ERROR, NULL,
                             "Error parsing revision argument '%s'", arg);

  return SVN_NO_ERROR;
}

int
svn_opt_parse_revision(svn_opt_revision_t *start_revision,
                       svn_opt_revision_t *end_revision,
                       const char *arg,
                       apr_pool_t *pool)
{
  char *left_rev, *right_rev, *end;

  /* Operate on a copy of the argument. */
  left_rev = apr_pstrdup(pool, arg);

  right_rev = parse_one_rev(start_revision, left_rev, pool);
  if (right_rev && *right_rev == ':')
    {
      right_rev++;
      end = parse_one_rev(end_revision, right_rev, pool);
      if (!end || *end != '\0')
        return -1;
    }
  else if (!right_rev || *right_rev != '\0')
    return -1;

  return 0;
}


int
svn_opt_parse_revision_to_range(apr_array_header_t *opt_ranges,
                                const char *arg,
                                apr_pool_t *pool)
{
  svn_opt_revision_range_t *range = apr_palloc(pool, sizeof(*range));

  range->start.kind = svn_opt_revision_unspecified;
  range->end.kind = svn_opt_revision_unspecified;

  if (svn_opt_parse_revision(&(range->start), &(range->end),
                             arg, pool) == -1)
    return -1;

  APR_ARRAY_PUSH(opt_ranges, svn_opt_revision_range_t *) = range;
  return 0;
}

int svn_opt_parse_change_to_range(apr_array_header_t *opt_ranges,
                                  const char *arg,
                                  apr_pool_t *result_pool)
{
  char *end;
  svn_revnum_t changeno, changeno_end;
  const char *s = arg;
  svn_boolean_t is_negative;

  /* Check for a leading minus to allow "-c -r42".
   * The is_negative flag is used to handle "-c -42" and "-c -r42".
   * The "-c r-42" case is handled by strtol() returning a
   * negative number. */
  is_negative = (*s == '-');
  if (is_negative)
    s++;

  /* Allow any number of 'r's to prefix a revision number. */
  while (*s == 'r')
    s++;
  changeno = changeno_end = strtol(s, &end, 10);
  if (end != s && *end == '-')
    {
      /* Negative number in range not supported with -c */
      if (changeno < 0 || is_negative)
        return -1;

      s = end + 1;
      while (*s == 'r')
        s++;
      changeno_end = strtol(s, &end, 10);

      /* Negative number in range not supported with -c */
      if (changeno_end < 0)
          return -1;
    }

  /* Non-numeric change argument given to -c? */
  if (end == arg || *end != '\0')
    return -1;

  /* There is no change 0 */
  if (changeno == 0 || changeno_end == 0)
    return -1;

  /* The revision number cannot contain a double minus */
  if (changeno < 0 && is_negative)
    return -1;

  if (is_negative)
    changeno = -changeno;

  /* Figure out the range:
        -c N  -> -r N-1:N
        -c -N -> -r N:N-1
        -c M-N -> -r M-1:N for M < N
        -c M-N -> -r M:N-1 for M > N
        -c -M-N -> error (too confusing/no valid use case)
  */
  if (changeno > 0)
    {
      if (changeno <= changeno_end)
        changeno--;
      else
        changeno_end--;
    }
  else
    {
      changeno = -changeno;
      changeno_end = changeno - 1;
    }

  APR_ARRAY_PUSH(opt_ranges,
                  svn_opt_revision_range_t *)
    = svn_opt__revision_range_from_revnums(changeno, changeno_end,
                                           result_pool);

  return 0;
}

svn_error_t *
svn_opt_parse_revnum(svn_revnum_t *rev, const char *str)
{
  /* Allow any number of 'r's to prefix a revision number. */
  while (*str == 'r')
    str++;

  return svn_error_trace(svn_revnum_parse(rev, str, NULL));
}

svn_error_t *
svn_opt_resolve_revisions(svn_opt_revision_t *peg_rev,
                          svn_opt_revision_t *op_rev,
                          svn_boolean_t is_url,
                          svn_boolean_t notice_local_mods,
                          apr_pool_t *pool)
{
  if (peg_rev->kind == svn_opt_revision_unspecified)
    {
      if (is_url)
        {
          peg_rev->kind = svn_opt_revision_head;
        }
      else
        {
          if (notice_local_mods)
            peg_rev->kind = svn_opt_revision_working;
          else
            peg_rev->kind = svn_opt_revision_base;
        }
    }

  if (op_rev->kind == svn_opt_revision_unspecified)
    *op_rev = *peg_rev;

  return SVN_NO_ERROR;
}

const char *
svn_opt__revision_to_string(const svn_opt_revision_t *revision,
                            apr_pool_t *result_pool)
{
  switch (revision->kind)
    {
      case svn_opt_revision_unspecified:
        return "unspecified";
      case svn_opt_revision_number:
        return apr_psprintf(result_pool, "%ld", revision->value.number);
      case svn_opt_revision_date:
        /* ### svn_time_to_human_cstring()? */
        return svn_time_to_cstring(revision->value.date, result_pool);
      case svn_opt_revision_committed:
        return "committed";
      case svn_opt_revision_previous:
        return "previous";
      case svn_opt_revision_base:
        return "base";
      case svn_opt_revision_working:
        return "working";
      case svn_opt_revision_head:
        return "head";
      default:
        return NULL;
    }
}

svn_opt_revision_range_t *
svn_opt__revision_range_create(const svn_opt_revision_t *start_revision,
                               const svn_opt_revision_t *end_revision,
                               apr_pool_t *result_pool)
{
  svn_opt_revision_range_t *range = apr_palloc(result_pool, sizeof(*range));

  range->start = *start_revision;
  range->end = *end_revision;
  return range;
}

svn_opt_revision_range_t *
svn_opt__revision_range_from_revnums(svn_revnum_t start_revnum,
                                     svn_revnum_t end_revnum,
                                     apr_pool_t *result_pool)
{
  svn_opt_revision_range_t *range = apr_palloc(result_pool, sizeof(*range));

  range->start.kind = svn_opt_revision_number;
  range->start.value.number = start_revnum;
  range->end.kind = svn_opt_revision_number;
  range->end.value.number = end_revnum;
  return range;
}
