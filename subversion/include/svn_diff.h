/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_diff.h
 * @brief Structures related to contextual diffing.
 *
 * This is an internalized library for performing contextual diffs
 * between sources of data.
 *
 * NOTE: this is different than Subversion's binary-diffing engine.
 * That API lives in @c svn_delta.h -- see the "text deltas" section.  A
 * "text delta" is way of representing precise binary diffs between
 * strings of data.  The Subversion client and server send text deltas
 * to one another during updates and commits.
 *
 * This API, however, is (or will be) used for peforming *contextual*
 * merges between files in the working copy.  During an update or
 * merge, 3-way file merging is needed.  And 'svn diff' needs to show
 * the differences between 2 files.
 *
 * The nice thing about this API is that it's very general.  It
 * operates on any source of data (a "datasource") and calculates
 * contextual differences on "tokens" within the data.  In our
 * particular usage, the datasources are files and the tokens are
 * lines.  But the possibilites are endless.
 */


#ifndef SVN_DIFF_H
#define SVN_DIFF_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Diffs. */

/** An opaque type that represents a difference between either two or
 * three datasources.
 *
 * An opaque type that represents a difference between either two or
 * three datasources.   This object is returned by @c svn_diff() and
 * @c svn_diff3() below, and consumed by a number of other routines.
 */
typedef struct svn_diff_t svn_diff_t;

/**
 * There are three types of datasources.  In GNU diff3 terminology,
 * these types correspond to the phrases "older", "mine", and "yours".
 */
typedef enum svn_diff_datasource_e
{
  /** The oldest form of the data. */
  svn_diff_datasource_original,

  /** The same data, but potentially changed by the user. */
  svn_diff_datasource_modified,

  /** The latest version of the data, possibly different than the
   * user's modified version.
   */
  svn_diff_datasource_latest

} svn_diff_datasource_e;


/** A vtable for reading data from the three datasources. */
typedef struct svn_diff_fns_t
{
  /** Open the datasource of type @a datasource. */
  svn_error_t *(*datasource_open) (void *diff_baton,
                                   svn_diff_datasource_e datasource);

  /** Close the datasource of type @a datasource. */
  svn_error_t *(*datasource_close) (void *diff_baton,
                                    svn_diff_datasource_e datasource);

  /** Get the next "token" from the datasource of type @a datasource. */
  svn_error_t *(*datasource_get_next_token) (void **token,
                                             void *diff_baton,
                                             svn_diff_datasource_e datasource);

  /** A function for ordering the tokens with the same interface as
   * 'strcmp'.
   *
   * A function for ordering the tokens with the same interface as
   * 'strcmp': If @a ltoken and @a rtoken are "equal", return 0.  If 
   * @a ltoken is "less than" @a rtoken, return a number < 0.  If @a ltoken 
   * is "greater than" @a rtoken, return a number > 0.  [The diff algorithm
   * uses this routine to assemble the tokens into a binary tree.]
   */
  int (*token_compare) (void *diff_baton,
                        void *ltoken,
                        void *rtoken);

  /** Free @a token from memory, the diff algorithm is done with it. */
  void (*token_discard) (void *diff_baton,
                         void *token);

  /** Free *all* tokens from memory, they're no longer needed. */
  void (*token_discard_all) (void *diff_baton);
} svn_diff_fns_t;


/* The Main Events */

/** Return a diff that represents the differences between an original and 
 * modified datasource.
 *
 * Given a vtable of @a diff_fns/@a diff_baton for reading datasources,
 * return a diff object in @a *diff that represents a difference between
 * an "original" and "modified" datasource.  Do all allocation in @a pool.
 */
svn_error_t *svn_diff (svn_diff_t **diff,
                       void *diff_baton,
                       const svn_diff_fns_t *diff_fns,
                       apr_pool_t *pool);

/** Return a diff that represents the difference between three datasources.
 *
 * Given a vtable of @a diff_fns/@a diff_baton for reading datasources,
 * return a diff object in @a *diff that represents a difference between
 * three datasources: "original", "modified", and "latest". Do all
 * allocation in @a pool.
 */
svn_error_t *svn_diff3 (svn_diff_t **diff,
                        void *diff_baton,
                        const svn_diff_fns_t *diff_fns,
                        apr_pool_t *pool);


/* Utility functions */

/** Determine if a diff object contains conflicts.
 *
 * Determine if a diff object contains conflicts.  If it does, return
 * @c TRUE, else return @c FALSE.
 */
svn_boolean_t
svn_diff_contains_conflicts (svn_diff_t *diff);


/** Determine if a diff object contains actual differences between the
 * datasources.
 *
 * Determine if a diff object contains actual differences between the
 * datasources.  If so, return @c TRUE, else return @c FALSE.
 */
svn_boolean_t
svn_diff_contains_diffs (svn_diff_t *diff);




/* Displaying Diffs */

/** A vtable for displaying (or consuming) differences between datasources.
 *
 * A vtable for displaying (or consuming) differences between datasources.
 * Differences, similarities, and conflicts are described by lining up
 * "ranges" of data.
 *  
 * Note: these callbacks describe data ranges in units of "tokens".
 * A "token" is whatever you've defined it to be in your datasource
 * @c svn_diff_fns_t vtable.
 */
typedef struct svn_diff_output_fns_t
{
  /* Two-way and three-way diffs both call the first two output functions: */

  /**
   * If doing a two-way diff, then an *identical* data range was found
   * between the "original" and "modified" datasources.  Specifically,
   * the match starts at @a original_start and goes for @a original_length
   * tokens in the original data, and at @a modified_start for 
   * @a modified_length tokens in the modified data.
   *
   * If doing a three-way diff, then all three datasources have
   * matching data ranges.  The range @a latest_start, @a latest_length in
   * the "latest" datasource is identical to the range @a original_start,
   * @a original_length in the original data, and is also identical to
   * the range @a modified_start, @a modified_length in the modified data.
   */
  svn_error_t *(*output_common) (void *output_baton,
                                apr_off_t original_start,
                                apr_off_t original_length,
                                apr_off_t modified_start,
                                apr_off_t modified_length,
                                apr_off_t latest_start,
                                apr_off_t latest_length);

  /**
   * If doing a two-way diff, then an *conflicting* data range was found
   * between the "original" and "modified" datasources.  Specifically,
   * the conflict starts at @a original_start and foes for @a original_length
   * tokens in the original data, and at @a modified_start for 
   * @a modified_lenght tokens in the modified data.
   *
   * If doing a three-way diff, then an identical data range was discovered
   * between the "original" and "latest" datasources, but this conflicts with
   * a range in the "modified" datasource.
   */
  svn_error_t *(*output_diff_modified) (void *output_baton,
                                        apr_off_t original_start,
                                        apr_off_t original_length,
                                        apr_off_t modified_start,
                                        apr_off_t modified_length,
                                        apr_off_t latest_start,
                                        apr_off_t latest_length);

  /* ------ The following callbacks are used by three-way diffs only --- */

  /** An identical data range was discovered between the "original" and
   * "modified" datasources, but this conflicts with a range in the
   * "latest" datasource.
   */
  svn_error_t *(*output_diff_latest) (void *output_baton,
                                      apr_off_t original_start,
                                      apr_off_t original_length,
                                      apr_off_t modified_start,
                                      apr_off_t modified_length,
                                      apr_off_t latest_start,
                                      apr_off_t latest_length);

  /** An identical data range was discovered between the "modified" and
   * "latest" datasources, but this conflicts with a range in the
   * "original" datasource.
   */
  svn_error_t *(*output_diff_common) (void *output_baton,
                                      apr_off_t original_start,
                                      apr_off_t original_length,
                                      apr_off_t modified_start,
                                      apr_off_t modified_length,
                                      apr_off_t latest_start,
                                      apr_off_t latest_length);

  /** All three datasources have conflicting data ranges.
   *
   * All three datasources have conflicting data ranges.  The range
   * @a latest_start, @a latest_length in the "latest" datasource conflicts 
   * with the range @a original_start, @a original_length in the "original" 
   * datasource, and also conflicts with the range @a modified_start, 
   * @a modified_length in the "modified" datasource.  */
  svn_error_t *(*output_conflict) (void *output_baton,
                                   apr_off_t original_start,
                                   apr_off_t original_length,
                                   apr_off_t modified_start,
                                   apr_off_t modified_length,
                                   apr_off_t latest_start,
                                   apr_off_t latest_length);
} svn_diff_output_fns_t;


/** Given a vtable of @a output_fns/@a output_baton for consuming
 * differences, output the differences in @a diff.
 */
svn_error_t *
svn_diff_output (svn_diff_t *diff,
                 void *output_baton,
                 const svn_diff_output_fns_t *output_fns);



/* Diffs on files */

/** A convenience function to produce a diff between two files.
 *
 * Return a diff object in @a *diff (allocated from @a pool) that represents
 * the difference between an @a original file and @a modified file.  
 * (The file arguments must be full paths to the files.)
 */
svn_error_t *
svn_diff_file(svn_diff_t **diff,
              const char *original,
              const char *modified,
              apr_pool_t *pool);


/** A convenience function to produce a diff between three files.
 *
 * Return a diff object in @a *diff (allocated from @a pool) that represents
 * the difference between an @a original file, @a modified file, and @a latest 
 * file. (The file arguments must be full paths to the files.)
 */
svn_error_t *
svn_diff3_file(svn_diff_t **diff,
               const char *original,
               const char *modified,
               const char *latest,
               apr_pool_t *pool);

/** A convenience function to produce unified diff output from the
 * diff generated by @c svn_diff_file.
 *
 * Output a @a diff between @a original_path and @a modified_path in unified
 * context diff format to @a output_file.  Optionally supply @a original_header
 * and/or @a modified_header to be displayed in the header of the output.
 * If @a original_header or @a modified_header is @c NULL, a default header 
 * will be displayed, consisting of path and last modified time.
 */
svn_error_t *
svn_diff_file_output_unified(apr_file_t *output_file,
                             svn_diff_t *diff,
                             const char *original_path,
                             const char *modified_path,
                             const char *original_header,
                             const char *modified_header,
                             apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIFF_H */
