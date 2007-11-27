/*
 * mergeinfo.c :  merge history functions for the libsvn_client library
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
#include <apr_strings.h>

#include "svn_pools.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_sorts.h"
#include "svn_client.h"

#include "private/svn_mergeinfo_private.h"
#include "private/svn_wc_private.h"
#include "client.h"
#include "mergeinfo.h"
#include "svn_private_config.h"



svn_error_t *
svn_client__parse_mergeinfo(apr_hash_t **mergeinfo,
                            const svn_wc_entry_t *entry,
                            const char *wcpath,
                            svn_boolean_t pristine,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  apr_hash_t *props = apr_hash_make(pool);
  const svn_string_t *propval;

  /* ### Use svn_wc_prop_get() would actually be sufficient for now.
     ### DannyB thinks that later we'll need behavior more like
     ### svn_client__get_prop_from_wc(). */
  SVN_ERR(svn_client__get_prop_from_wc(props, SVN_PROP_MERGE_INFO,
                                       wcpath, pristine, entry, adm_access,
                                       svn_depth_empty, ctx, pool));
  propval = apr_hash_get(props, wcpath, APR_HASH_KEY_STRING);
  if (propval)
    SVN_ERR(svn_mergeinfo_parse(mergeinfo, propval->data, pool));
  else
    *mergeinfo = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__record_wc_mergeinfo(const char *wcpath,
                                apr_hash_t *mergeinfo,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  svn_string_t *mergeinfo_str;

  /* Convert the mergeinfo (if any) into text for storage as a
     property value. */
  if (mergeinfo)
    {
      /* The WC will contain mergeinfo. */
      SVN_ERR(svn_mergeinfo__to_string(&mergeinfo_str, mergeinfo, pool));
    }
  else
    {
      mergeinfo_str = NULL;
    }

  /* Record the new mergeinfo in the WC. */
  /* ### Later, we'll want behavior more analogous to
     ### svn_client__get_prop_from_wc(). */
  return svn_wc_prop_set2(SVN_PROP_MERGE_INFO, mergeinfo_str, wcpath,
                          adm_access, TRUE /* skip checks */, pool);
}

/*-----------------------------------------------------------------------*/

/*** Retrieving mergeinfo. ***/

/* Adjust merge sources in MERGEINFO (which is assumed to be non-NULL). */
static APR_INLINE void
adjust_mergeinfo_source_paths(apr_hash_t *mergeinfo, const char *walk_path,
                              apr_hash_t *wc_mergeinfo, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const void *merge_source;
  void *rangelist;
  const char *path;

  for (hi = apr_hash_first(NULL, wc_mergeinfo); hi; hi = apr_hash_next(hi))
    {
      /* Copy inherited mergeinfo into our output hash, adjusting the
         merge source as appropriate. */
      apr_hash_this(hi, &merge_source, NULL, &rangelist);
      path = svn_path_join((const char *) merge_source, walk_path,
                           apr_hash_pool_get(mergeinfo));
      /* ### If pool has a different lifetime than mergeinfo->pool,
         ### this use of "rangelist" will be a problem... */
      apr_hash_set(mergeinfo, path, APR_HASH_KEY_STRING, rangelist);
    }
}


svn_error_t *
svn_client__get_wc_mergeinfo(apr_hash_t **mergeinfo,
                             svn_boolean_t *inherited,
                             svn_boolean_t pristine,
                             svn_mergeinfo_inheritance_t inherit,
                             const svn_wc_entry_t *entry,
                             const char *wcpath,
                             const char *limit_path,
                             const char **walked_path,
                             svn_wc_adm_access_t *adm_access,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  const char *walk_path = "";
  apr_hash_t *wc_mergeinfo;
  svn_boolean_t switched;

  if (limit_path)
    SVN_ERR(svn_path_get_absolute(&limit_path, limit_path, pool));

  while (TRUE)
    {
      /* Don't look for explicit mergeinfo on WCPATH if we are only
         interested in inherited mergeinfo. */
      if (inherit == svn_mergeinfo_nearest_ancestor)
        {
          wc_mergeinfo = NULL;
          inherit = svn_mergeinfo_inherited;
        }
      else
        {
          /* Look for mergeinfo on WCPATH.  If there isn't any and we want
             inherited mergeinfo, walk towards the root of the WC until we
             encounter either (a) an unversioned directory, or (b) mergeinfo.
             If we encounter (b), use that inherited mergeinfo as our
             baseline. */
          SVN_ERR(svn_client__parse_mergeinfo(&wc_mergeinfo, entry, wcpath,
                                              pristine, adm_access, ctx,
                                              pool));

          /* If WCPATH is switched, don't look any higher for inherited
             mergeinfo. */
          SVN_ERR(svn_wc__path_switched(wcpath, &switched, entry, pool));
          if (switched)
            break;
        }

      /* Subsequent svn_wc_adm_access_t need to be opened with
         an absolute path so we can walk up and out of the WC
         if necessary.  If we are using LIMIT_PATH it needs to
         be absolute too. */
#if defined(WIN32) || defined(__CYGWIN__)
      /* On Windows a path is also absolute when it starts with
         'H:/' where 'H' is any upper or lower case letter. */
      if (strlen(wcpath) == 0
          || ((strlen(wcpath) > 0 && wcpath[0] != '/')
               && !(strlen(wcpath) > 2
                    && wcpath[1] == ':'
                    && wcpath[2] == '/'
                    && ((wcpath[0] >= 'A' && wcpath[0] <= 'Z')
                        || (wcpath[0] >= 'a' && wcpath[0] <= 'z')))))
#else
      if (!(strlen(wcpath) > 0 && wcpath[0] == '/'))
#endif /* WIN32 or Cygwin */
        {
          SVN_ERR(svn_path_get_absolute(&wcpath, wcpath, pool));
        }

      if (wc_mergeinfo == NULL &&
          inherit != svn_mergeinfo_explicit &&
          !svn_dirent_is_root(wcpath, strlen(wcpath)))
        {
          svn_error_t *err;

          /* Don't look any higher than the limit path. */
          if (limit_path && strcmp(limit_path, wcpath) == 0)
            break;

          /* No explicit mergeinfo on this path.  Look higher up the
             directory tree while keeping track of what we've walked. */
          walk_path = svn_path_join(svn_path_basename(wcpath, pool),
                                    walk_path, pool);
          wcpath = svn_path_dirname(wcpath, pool);

          err = svn_wc_adm_open3(&adm_access, NULL, wcpath,
                                 FALSE, 0, NULL, NULL, pool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_WC_NOT_DIRECTORY)
                {
                  svn_error_clear(err);
                  err = SVN_NO_ERROR;
                  *inherited = FALSE;
                  *mergeinfo = wc_mergeinfo;
                }
              return err;
            }

          SVN_ERR(svn_wc_entry(&entry, wcpath, adm_access, FALSE, pool));

          if (entry)
            /* We haven't yet risen above the root of the WC. */
            continue;
      }
      break;
    }

  if (svn_path_is_empty(walk_path))
    {
      /* Merge info is explicit. */
      *inherited = FALSE;
      *mergeinfo = wc_mergeinfo;
    }
  else
    {
      /* Merge info may be inherited. */
      if (wc_mergeinfo)
        {
          *inherited = (wc_mergeinfo && apr_hash_count(wc_mergeinfo) > 0);
          *mergeinfo = apr_hash_make(pool);
          adjust_mergeinfo_source_paths(*mergeinfo, walk_path, wc_mergeinfo,
                                        pool);
        }
      else
        {
          *inherited = FALSE;
          *mergeinfo = NULL;
        }
    }

  if (walked_path)
    *walked_path = walk_path;

  /* Remove non-inheritable mergeinfo if WCPATH's mergeinfo is
     not explicit. */
  if (*inherited)
    SVN_ERR(svn_mergeinfo_inheritable(mergeinfo, *mergeinfo, NULL,
            SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_repos_mergeinfo(svn_ra_session_t *ra_session,
                                apr_hash_t **target_mergeinfo,
                                const char *rel_path,
                                svn_revnum_t rev,
                                svn_mergeinfo_inheritance_t inherit,
                                apr_pool_t *pool)
{
  apr_hash_t *repos_mergeinfo;
  apr_array_header_t *rel_paths = apr_array_make(pool, 1, sizeof(rel_path));
  APR_ARRAY_PUSH(rel_paths, const char *) = rel_path;
  SVN_ERR(svn_ra_get_mergeinfo(ra_session, &repos_mergeinfo, rel_paths, rev,
                               inherit, pool));

  /* Grab only the mergeinfo provided for REL_PATH. */
  if (repos_mergeinfo)
    *target_mergeinfo = apr_hash_get(repos_mergeinfo, rel_path,
                                     APR_HASH_KEY_STRING);
  else
    *target_mergeinfo = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_wc_or_repos_mergeinfo(apr_hash_t **target_mergeinfo,
                                      const svn_wc_entry_t *entry,
                                      svn_boolean_t *indirect,
                                      svn_boolean_t repos_only,
                                      svn_mergeinfo_inheritance_t inherit,
                                      svn_ra_session_t *ra_session,
                                      const char *target_wcpath,
                                      svn_wc_adm_access_t *adm_access,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool)
{
  const char *url;
  const char *repos_rel_path;
  svn_revnum_t target_rev;

  /* We may get an entry with abrieviated information from TARGET_WCPATH's
     parent if TARGET_WCPATH is missing.  These limited entries do not have
     a URL and without that we cannot get accurate mergeinfo for
     TARGET_WCPATH. */
  SVN_ERR(svn_client__entry_location(&url, &target_rev, target_wcpath,
                                     svn_opt_revision_working, entry, pool));

  repos_rel_path = url + strlen(entry->repos);

  if (repos_only)
    *target_mergeinfo = NULL;
  else
    SVN_ERR(svn_client__get_wc_mergeinfo(target_mergeinfo, indirect, FALSE,
                                         inherit, entry, target_wcpath,
                                         NULL, NULL, adm_access, ctx, pool));

  /* If there in no WC mergeinfo check the repository. */
  if (*target_mergeinfo == NULL)
    {
      apr_hash_t *repos_mergeinfo;

      /* No need to check the repos is this is a local addition. */
      if (entry->schedule != svn_wc_schedule_add)
        {
          apr_hash_t *props = apr_hash_make(pool);

          /* Get the pristine SVN_PROP_MERGE_INFO.
             If it exists, then it should have been deleted by the local
             merges. So don't get the mergeinfo from the repository. Just
             assume the mergeinfo to be NULL.
          */
          SVN_ERR(svn_client__get_prop_from_wc(props, SVN_PROP_MERGE_INFO,
                                               target_wcpath, TRUE, entry,
                                               adm_access, svn_depth_empty,
                                               ctx, pool));
          if (apr_hash_get(props, target_wcpath, APR_HASH_KEY_STRING) == NULL)
            {
              if (ra_session == NULL)
                SVN_ERR(svn_client__open_ra_session_internal(&ra_session, url,
                                                             NULL, NULL, NULL,
                                                             FALSE, TRUE, ctx,
                                                             pool));

              SVN_ERR(svn_client__get_repos_mergeinfo(ra_session,
                                                      &repos_mergeinfo,
                                                      repos_rel_path,
                                                      target_rev,
                                                      inherit,
                                                      pool));
              if (repos_mergeinfo)
                {
                  *target_mergeinfo = repos_mergeinfo;
                  *indirect = TRUE;
                }
            }
        }
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_implicit_mergeinfo(apr_hash_t **mergeinfo_p,
                                   const char *path_or_url,
                                   const svn_opt_revision_t *peg_revision,
                                   svn_revnum_t range_youngest,
                                   svn_revnum_t range_oldest,
                                   svn_ra_session_t *ra_session,
                                   svn_wc_adm_access_t *adm_access,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *pool)
{
  apr_array_header_t *segments;
  svn_revnum_t peg_revnum = SVN_INVALID_REVNUM;
  const char *url;
  apr_hash_t *mergeinfo = apr_hash_make(pool);
  apr_pool_t *sesspool = NULL;  /* only used for an RA session we open */
  svn_ra_session_t *session = ra_session;
  int i;

  /* If PATH_OR_URL is a local path (not a URL), we need to transform
     it into a URL, open an RA session for it, and resolve the peg
     revision.  Note that if the local item is scheduled for addition
     as a copy of something else, we'll use its copyfrom data to query
     its history.  */
  SVN_ERR(svn_client__derive_location(&url, &peg_revnum, path_or_url,
                                      peg_revision, session, adm_access,
                                      ctx, pool));

  if (session == NULL)
    {
      sesspool = svn_pool_create(pool);
      SVN_ERR(svn_client__open_ra_session_internal(&session, url, NULL, NULL,
                                                   NULL, FALSE, TRUE, ctx,
                                                   sesspool));
    }

  /* Fetch the location segments for our URL@PEG_REVNUM. */
  if (! SVN_IS_VALID_REVNUM(range_youngest))
    range_youngest = peg_revnum;
  if (! SVN_IS_VALID_REVNUM(range_oldest))
    range_oldest = 0;
  SVN_ERR(svn_client__repos_location_segments(&segments, session, "", 
                                              peg_revnum, range_youngest, 
                                              range_oldest, ctx, pool));

  /* Translate location segments into merge sources and ranges. */
  for (i = 0; i < segments->nelts; i++)
    {
      svn_location_segment_t *segment = 
        APR_ARRAY_IDX(segments, i, svn_location_segment_t *);
      apr_array_header_t *path_ranges;
      svn_merge_range_t *range;
      const char *source_path;

      /* No path segment?  Skip it. */
      if (! segment->path)
        continue;

      /* Prepend a leading slash to our path. */
      source_path = apr_pstrcat(pool, "/", segment->path, NULL);

      /* See if we already stored ranges for this path.  If not, make
         a new list.  */
      path_ranges = apr_hash_get(mergeinfo, source_path, APR_HASH_KEY_STRING);
      if (! path_ranges)
        path_ranges = apr_array_make(pool, 1, sizeof(range));

      /* Build a merge range, push it onto the list of ranges, and for
         good measure, (re)store it in the hash. */
      range = apr_pcalloc(pool, sizeof(*range));
      range->start = MAX(segment->range_start - 1, 0);
      range->end = segment->range_end;
      range->inheritable = TRUE;
      APR_ARRAY_PUSH(path_ranges, svn_merge_range_t *) = range;
      apr_hash_set(mergeinfo, source_path, APR_HASH_KEY_STRING, path_ranges);
    }

  /* If we opened an RA session, ensure its closure. */
  if (sesspool)
    svn_pool_destroy(sesspool);

  *mergeinfo_p = mergeinfo;
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Eliding mergeinfo. ***/

/* Helper for elide_mergeinfo().

   Find all paths in CHILD_MERGEINFO which map to empty revision ranges
   and copy these from CHILD_MERGEINFO to *EMPTY_RANGE_MERGEINFO iff
   PARENT_MERGEINFO is NULL or does not have mergeinfo for the path in
   question.

   All mergeinfo in CHILD_MERGEINFO not copied to *EMPTY_RANGE_MERGEINFO
   is copied to *NONEMPTY_RANGE_MERGEINFO.

   *EMPTY_RANGE_MERGEINFO and *NONEMPTY_RANGE_MERGEINFO are set to empty
   hashes if nothing is copied into them.

   All copied hashes are deep copies allocated in POOL. */
static svn_error_t *
get_empty_rangelists_unique_to_child(apr_hash_t **empty_range_mergeinfo,
                                     apr_hash_t **nonempty_range_mergeinfo,
                                     apr_hash_t *child_mergeinfo,
                                     apr_hash_t *parent_mergeinfo,
                                     apr_pool_t *pool)
{
  *empty_range_mergeinfo = apr_hash_make(pool);
  *nonempty_range_mergeinfo = apr_hash_make(pool);

  if (child_mergeinfo)
    {
      apr_hash_index_t *hi;
      void *child_val;
      const void *child_key;
      apr_array_header_t *child_range;
      const char *child_path;

      /* Iterate through CHILD_MERGEINFO looking for mergeinfo with empty
         revision ranges. */
      for (hi = apr_hash_first(pool, child_mergeinfo); hi;
           hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &child_key, NULL, &child_val);
          child_path = child_key;
          child_range = child_val;

          /* Copy paths with empty revision ranges which don't exist in
             PARENT_MERGEINFO from CHILD_MERGEINFO to *EMPTY_RANGE_MERGEINFO.
             Copy everything else to *NONEMPTY_RANGE_MERGEINFO. */
          if (child_range->nelts == 0
              && (parent_mergeinfo == NULL
                  || apr_hash_get(parent_mergeinfo, child_path,
                                  APR_HASH_KEY_STRING) == NULL))
            {
              apr_hash_set(*empty_range_mergeinfo,
                           apr_pstrdup(pool, child_path),
                           APR_HASH_KEY_STRING,
                           svn_rangelist_dup(child_range, pool));
            }
          else
            {
              apr_hash_set(*nonempty_range_mergeinfo,
                           apr_pstrdup(pool, child_path),
                           APR_HASH_KEY_STRING,
                           svn_rangelist_dup(child_range, pool));
            }
        }
    }
  return SVN_NO_ERROR;
}

/* Helper for svn_client__elide_mergeinfo() and svn_client__elide_children().

   Given a working copy PATH, its mergeinfo hash CHILD_MERGEINFO, and the
   mergeinfo of PATH's nearest ancestor PARENT_MERGEINFO, compare
   CHILD_MERGEINFO to PARENT_MERGEINFO to see if the former elides to
   the latter, following the elision rules described in
   svn_client__elide_mergeinfo()'s docstring.  If elision (full or partial)
   does occur, then update PATH's mergeinfo appropriately.  If CHILD_MERGEINFO
   is NULL, do nothing.

   If PATH_SUFFIX and PARENT_MERGEINFO are not NULL append PATH_SUFFIX to each
   path in PARENT_MERGEINFO before performing the comparison. */
static svn_error_t *
elide_mergeinfo(apr_hash_t *parent_mergeinfo,
                apr_hash_t *child_mergeinfo,
                const char *path,
                const char *path_suffix,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  apr_pool_t *subpool;
  apr_hash_t *mergeinfo, *child_empty_mergeinfo, *child_nonempty_mergeinfo;
  svn_boolean_t equal_mergeinfo;

  /* A tri-state value describing the various types of elision possible for
     svn:mergeinfo set on a WC path. */
  enum wc_elision_type
  {
    elision_type_none,    /* No elision occurs. */
    elision_type_partial, /* Paths that exist only in CHILD_MERGEINFO and
                             map to empty revision ranges elide. */
    elision_type_full     /* All mergeinfo in CHILD_MERGEINFO elides. */
  } elision_type = elision_type_none;

  /* Easy out: No child mergeinfo to elide. */
  if (child_mergeinfo == NULL)
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);
  if (path_suffix && parent_mergeinfo)
    {
      apr_hash_index_t *hi;
      void *val;
      const void *key;
      const char *new_path;
      apr_array_header_t *rangelist;

      mergeinfo = apr_hash_make(subpool);

      for (hi = apr_hash_first(subpool, parent_mergeinfo); hi;
           hi = apr_hash_next(hi))
        {
          apr_hash_this(hi, &key, NULL, &val);
          new_path = svn_path_join((const char *) key, path_suffix, subpool);
          rangelist = val;
          apr_hash_set(mergeinfo, new_path, APR_HASH_KEY_STRING, rangelist);
        }
    }
  else
    {
      mergeinfo = parent_mergeinfo;
    }

 /* Separate any mergeinfo with empty rev ranges for paths that exist only
    in CHILD_MERGEINFO and store these in CHILD_EMPTY_MERGEINFO. */
  SVN_ERR(get_empty_rangelists_unique_to_child(&child_empty_mergeinfo,
                                               &child_nonempty_mergeinfo,
                                               child_mergeinfo, mergeinfo,
                                               subpool));

  /* If *all* paths in CHILD_MERGEINFO map to empty revision ranges and none
     of these paths exist in PARENT_MERGEINFO full elision occurs; if only
     *some* of the paths in CHILD_MERGEINFO meet this criteria we know, at a
     minimum, partial elision will occur. */
  if (apr_hash_count(child_empty_mergeinfo) > 0)
    elision_type = apr_hash_count(child_nonempty_mergeinfo) == 0
                   ? elision_type_full : elision_type_partial;

  if (elision_type == elision_type_none && mergeinfo)
    {
      apr_hash_t *parent_empty_mergeinfo, *parent_nonempty_mergeinfo;

      /* Full elision also occurs if MERGEINFO and TARGET_MERGEINFO are
         equal except for paths unique to MERGEINFO that map to empty
         revision ranges.

         Separate any mergeinfo with empty rev ranges for paths that exist
         only in MERGEINFO and store these in PARENT_EMPTY_MERGEINFO and
         compare that with CHILD_MERGEINFO. */
      SVN_ERR(get_empty_rangelists_unique_to_child(&parent_empty_mergeinfo,
                                                   &parent_nonempty_mergeinfo,
                                                   mergeinfo, child_mergeinfo,
                                                   subpool));
      SVN_ERR(svn_mergeinfo__equals(&equal_mergeinfo,
                                    parent_nonempty_mergeinfo,
                                    child_mergeinfo,
                                    TRUE,
                                    subpool));
      if (equal_mergeinfo)
        elision_type = elision_type_full;
    }

  if (elision_type != elision_type_full && mergeinfo)
    {
      /* If no determination of elision status has been made yet or we know
        only that partial elision occurs, compare CHILD_NONEMPTY_MERGEINFO
        with the PATH_SUFFIX tweaked version of PARENT_MERGEINFO for equality.

        If we determined that at least partial elision occurs, full elision
        may still yet occur if CHILD_NONEMPTY_MERGEINFO, which no longer
        contains any paths unique to it that map to empty revision ranges,
        is equivalent to PARENT_MERGEINFO. */
      SVN_ERR(svn_mergeinfo__equals(&equal_mergeinfo,
                                    child_nonempty_mergeinfo,
                                    mergeinfo, TRUE,
                                    subpool));
      if (equal_mergeinfo)
        elision_type = elision_type_full;
    }

    switch (elision_type)
      {
      case elision_type_full:
        SVN_ERR(svn_wc_prop_set2(SVN_PROP_MERGE_INFO, NULL, path, adm_access,
                                 TRUE, subpool));
        break;
      case elision_type_partial:
        SVN_ERR(svn_client__record_wc_mergeinfo(path,
                                                child_nonempty_mergeinfo,
                                                adm_access, subpool));
        break;
      default:
        break; /* Leave mergeinfo on PATH as-is. */
      }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__elide_children(apr_array_header_t *children_with_mergeinfo,
                           const char *target_wcpath,
                           const svn_wc_entry_t *entry,
                           svn_wc_adm_access_t *adm_access,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  if (children_with_mergeinfo && children_with_mergeinfo->nelts)
    {
      int i;
      const char *last_immediate_child;
      apr_hash_t *target_mergeinfo;
      apr_pool_t *iterpool = svn_pool_create(pool);

      /* Get mergeinfo for the target of the merge. */
      SVN_ERR(svn_client__parse_mergeinfo(&target_mergeinfo, entry,
                                          target_wcpath, FALSE,
                                          adm_access, ctx, pool));

      /* For each immediate child of the merge target check if
         its merginfo elides to the target. */
      for (i = 0; i < children_with_mergeinfo->nelts; i++)
        {
          apr_hash_t *child_mergeinfo;
          svn_boolean_t switched;
          const svn_wc_entry_t *child_entry;
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, i,
                          svn_client__merge_path_t *);
          svn_pool_clear(iterpool);

          if (!child)
            continue;

          if (child->absent)
            continue;

          if (i == 0)
            {
              /* children_with_mergeinfo is sorted depth
                 first so first path might be the target of
                 the merge if the target had mergeinfo prior
                 to the start of the merge. */
              if (strcmp(target_wcpath, child->path) == 0)
                {
                  last_immediate_child = NULL;
                  continue;
                }
              last_immediate_child = child->path;
            }
          else if (last_immediate_child
                   && svn_path_is_ancestor(last_immediate_child, child->path))
            {
              /* Not an immediate child. */
              continue;
            }
          else
            {
              /* Found the first (last_immediate_child == NULL)
                 or another immediate child. */
              last_immediate_child = child->path;
            }

          /* Don't try to elide switched children. */
          SVN_ERR(svn_wc__entry_versioned(&child_entry, child->path,
                                          adm_access, FALSE, iterpool));
          SVN_ERR(svn_wc__path_switched(child->path, &switched, child_entry,
                                        iterpool));
          if (!switched)
            {
              const char *path_prefix = svn_path_dirname(child->path,
                                                         iterpool);
              const char *path_suffix = svn_path_basename(child->path,
                                                          iterpool);

              SVN_ERR(svn_client__parse_mergeinfo(&child_mergeinfo, entry,
                                                  child->path, FALSE,
                                                  adm_access, ctx, iterpool));

              while (strcmp(path_prefix, target_wcpath) != 0)
                {
                  path_suffix = svn_path_join(svn_path_basename(path_prefix,
                                                                iterpool),
                                              path_suffix, iterpool);
                  path_prefix = svn_path_dirname(path_prefix, iterpool);
                }

              SVN_ERR(elide_mergeinfo(target_mergeinfo, child_mergeinfo,
                                      child->path, path_suffix, adm_access,
                                      iterpool));
            }
        }
    svn_pool_destroy(iterpool);
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__elide_mergeinfo(const char *target_wcpath,
                            const char *wc_elision_limit_path,
                            const svn_wc_entry_t *entry,
                            svn_wc_adm_access_t *adm_access,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  /* Check for first easy out: We are already at the limit path. */
  if (!wc_elision_limit_path
      || strcmp(target_wcpath, wc_elision_limit_path) != 0)
    {
      apr_hash_t *target_mergeinfo;
      apr_hash_t *mergeinfo = NULL;
      svn_boolean_t inherited, switched;
      const char *walk_path;

      /* Check for second easy out: TARGET_WCPATH is switched. */
      SVN_ERR(svn_wc__path_switched(target_wcpath, &switched, entry, pool));
      if (!switched)
        {
          /* Get the TARGET_WCPATH's explicit mergeinfo. */
          SVN_ERR(svn_client__get_wc_mergeinfo(&target_mergeinfo, &inherited,
                                               FALSE, svn_mergeinfo_inherited,
                                               entry, target_wcpath,
                                               wc_elision_limit_path
                                                 ? wc_elision_limit_path
                                                 : NULL,
                                               &walk_path, adm_access,
                                               ctx, pool));

         /* If TARGET_WCPATH has no explicit mergeinfo, there's nothing to
             elide, we're done. */
          if (inherited || target_mergeinfo == NULL)
            return SVN_NO_ERROR;

          /* Get TARGET_WCPATH's inherited mergeinfo from the WC. */
          SVN_ERR(svn_client__get_wc_mergeinfo(&mergeinfo, &inherited, FALSE,
                                               svn_mergeinfo_nearest_ancestor,
                                               entry, target_wcpath,
                                               wc_elision_limit_path
                                                 ? wc_elision_limit_path
                                                 : NULL,
                                               &walk_path, adm_access,
                                               ctx, pool));

          /* If TARGET_WCPATH inherited no mergeinfo from the WC and we are
             not limiting our search to the working copy then check if it
             inherits any from the repos. */
          if (!mergeinfo && !wc_elision_limit_path)
            {
              SVN_ERR(svn_client__get_wc_or_repos_mergeinfo
                      (&mergeinfo, entry, &inherited, TRUE,
                       svn_mergeinfo_nearest_ancestor,
                       NULL, target_wcpath, adm_access, ctx, pool));
            }

          /* If there is nowhere to elide TARGET_WCPATH's mergeinfo to and
             the elision is limited, then we are done.*/
          if (!mergeinfo && wc_elision_limit_path)
            return SVN_NO_ERROR;

          SVN_ERR(elide_mergeinfo(mergeinfo, target_mergeinfo, target_wcpath,
                                  NULL, adm_access, pool));
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__elide_mergeinfo_for_tree(apr_hash_t *children_with_mergeinfo,
                                     svn_wc_adm_access_t *adm_access,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *sorted_children =
    svn_sort__hash(children_with_mergeinfo, svn_sort_compare_items_as_paths,
                   pool);

  /* sorted_children is in depth first order.  To minimize
     svn_client__elide_mergeinfo()'s crawls up the working copy from
     each child, run through the array backwards, effectively doing a
     right-left post-order traversal. */
  for (i = sorted_children->nelts -1; i >= 0; i--)
    {
      const svn_wc_entry_t *child_entry;
      const char *child_wcpath;
      svn_sort__item_t *item = &APR_ARRAY_IDX(sorted_children, i,
                                              svn_sort__item_t);
      svn_pool_clear(iterpool);
      child_wcpath = item->key;
      SVN_ERR(svn_wc__entry_versioned(&child_entry, child_wcpath, adm_access,
                                      FALSE, iterpool));
      SVN_ERR(svn_client__elide_mergeinfo(child_wcpath, NULL, child_entry,
                                          adm_access, ctx, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}



/*** Public APIs ***/

svn_error_t *
svn_client_mergeinfo_get_merged(apr_hash_t **mergeinfo,
                                const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  const char *repos_root;
  apr_hash_t *full_path_mergeinfo;

  if (svn_path_is_url(path_or_url))
    {
      svn_ra_session_t *ra_session;
      const char *repos_rel_path;
      svn_revnum_t rev;

      SVN_ERR(svn_client__open_ra_session_internal(&ra_session, path_or_url,
                                                   NULL, NULL, NULL, FALSE,
                                                   TRUE, ctx, pool));
      SVN_ERR(svn_client__get_revision_number(&rev, NULL, ra_session,
                                              peg_revision, "", pool));
      SVN_ERR(svn_ra_get_repos_root(ra_session, &repos_root, pool));
      SVN_ERR(svn_client__path_relative_to_root(&repos_rel_path, path_or_url,
                                                repos_root, TRUE, NULL, 
                                                NULL, pool));
      SVN_ERR(svn_client__get_repos_mergeinfo(ra_session, mergeinfo,
                                              repos_rel_path, rev,
                                              svn_mergeinfo_inherited, pool));
    }
  else /* ! svn_path_is_url() */
    {
      svn_wc_adm_access_t *adm_access;
      const svn_wc_entry_t *entry;
      svn_boolean_t indirect;

      SVN_ERR(svn_wc_adm_probe_open3(&adm_access, NULL, path_or_url, FALSE,
                                     0, ctx->cancel_func, ctx->cancel_baton,
                                     pool));
      SVN_ERR(svn_wc__entry_versioned(&entry, path_or_url, adm_access, FALSE,
                                      pool));
      SVN_ERR(svn_client__get_repos_root(&repos_root, path_or_url,
                                         peg_revision, adm_access, ctx, pool));
      SVN_ERR(svn_client__get_wc_or_repos_mergeinfo(mergeinfo, entry,
                                                    &indirect, FALSE,
                                                    svn_mergeinfo_inherited,
                                                    NULL, path_or_url,
                                                    adm_access, ctx, pool));
      SVN_ERR(svn_wc_adm_close(adm_access));
    }

  if (*mergeinfo)
    {
      apr_hash_index_t *hi;

      /* Copy the MERGEINFO hash items into another hash, but change
         the relative paths into full URLs. */

      full_path_mergeinfo = apr_hash_make(pool);
      for (hi = apr_hash_first(pool, *mergeinfo); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *rel_url;

          apr_hash_this(hi, &key, NULL, &val);
          rel_url = svn_path_uri_encode(key, pool);
          apr_hash_set(full_path_mergeinfo,
                       apr_pstrcat(pool, repos_root, rel_url, NULL),
                       APR_HASH_KEY_STRING,
                       val);
        }
      *mergeinfo = full_path_mergeinfo;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_mergeinfo_get_available(apr_array_header_t **rangelist,
                                   const char *path_or_url,
                                   const svn_opt_revision_t *peg_revision,
                                   const char *merge_source_url,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *pool)
{
  apr_hash_t *mergeinfo;
  apr_array_header_t *already_merged_ranges = NULL, *full_range_list;
  svn_opt_revision_t head_revision;
  svn_merge_range_t *full_range = apr_pcalloc(pool, sizeof(*full_range));
  svn_ra_session_t *ra_session;

  /* Step 1: See what merges we *do* have for MERGE_SOURCE.  If none,
     that's okay. */
  SVN_ERR(svn_client_mergeinfo_get_merged(&mergeinfo, path_or_url,
                                          peg_revision, ctx, pool));
  if (mergeinfo)
    already_merged_ranges = apr_hash_get(mergeinfo, merge_source_url,
                                         APR_HASH_KEY_STRING);

  /* Step 2: See what revision ranges exist for MERGE_SOURCE.  This
     means a history crawl. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, merge_source_url,
                                               NULL, NULL, NULL, FALSE,
                                               TRUE, ctx, pool));
  head_revision.kind = svn_opt_revision_head;
  SVN_ERR(svn_client__get_revision_number(&(full_range->end), NULL, ra_session,
                                          &head_revision, "", pool));

  /* ### FIXME: What if MERGE_SOURCE_URL no longer exists in HEAD?
     ### What if it exists, but is a different resource than was
     ### previously merged?  What about chunks of MERGE_SOURCE_URL's
     ### history that are shared with PATH_OR_URL?  These are all
     ### unpleasantries.
  */
  SVN_ERR(svn_client__oldest_rev_at_path(&(full_range->start), ra_session,
                                         "", full_range->end, pool));
  full_range->inheritable = TRUE;
  full_range_list = apr_array_make(pool, 1, sizeof(svn_merge_range_t *));
  APR_ARRAY_PUSH(full_range_list, svn_merge_range_t *) = full_range;

  /* Step 3: Calculate the difference (by removing the already merged
     ranges from the full set).  If there are no already-merged
     ranges, then the full range is deemed eligible.

     ### TODO: At no point did we say check to see if MERGE_SOURCE_URL
     ### was a sane merge source to ask about.  If there were previous
     ### merges from there, fine, but if not ...
  */
  if (! already_merged_ranges)
    *rangelist = full_range_list;
  else
    SVN_ERR(svn_rangelist_remove(rangelist, already_merged_ranges,
                                 full_range_list,
                                 FALSE, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_suggest_merge_sources(apr_array_header_t **suggestions,
                                 const char *path_or_url,
                                 const svn_opt_revision_t *peg_revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool)
{
  const char *repos_root;
  const char *copyfrom_path;
  apr_array_header_t *list;
  svn_revnum_t copyfrom_rev;
  apr_hash_t *mergeinfo;
  apr_hash_index_t *hi;

  list = apr_array_make(pool, 1, sizeof(const char *));

  /* In our ideal algorithm, the list of recommendations should be
     ordered by:

        1. The most recent existing merge source.
        2. The copyfrom source (which will also be listed as a merge
           source if the copy was made with a 1.5+ client and server).
        3. All other merge sources, most recent to least recent.

     However, determining the order of application of merge sources
     requires a new RA API.  Until such an API is available, our
     algorithm will be:

        1. The copyfrom source.
        2. All remaining merge sources (unordered).
  */

  /* ### TODO: Share ra_session batons to improve efficiency? */
  SVN_ERR(svn_client__get_repos_root(&repos_root, path_or_url, peg_revision,
                                     NULL, ctx, pool));
  SVN_ERR(svn_client__get_copy_source(path_or_url, peg_revision,
                                      &copyfrom_path, &copyfrom_rev,
                                      ctx, pool));
  if (copyfrom_path)
    {
      copyfrom_path = svn_path_join(repos_root,
                                    svn_path_uri_encode(copyfrom_path + 1,
                                                        pool),
                                    pool);
      APR_ARRAY_PUSH(list, const char *) = copyfrom_path;
    }

  SVN_ERR(svn_client_mergeinfo_get_merged(&mergeinfo, path_or_url,
                                          peg_revision, ctx, pool));
  if (mergeinfo)
    {
      for (hi = apr_hash_first(NULL, mergeinfo); hi; hi = apr_hash_next(hi))
        {
          const char *merge_path;
          apr_hash_this(hi, (void *)(&merge_path), NULL, NULL);
          if (copyfrom_path == NULL || strcmp(merge_path, copyfrom_path) != 0)
            APR_ARRAY_PUSH(list, const char *) = merge_path;
        }
    }

  *suggestions = list;
  return SVN_NO_ERROR;
}
