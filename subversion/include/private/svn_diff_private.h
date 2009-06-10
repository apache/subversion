/**
 * svn_diff_private.h: libsvn_diff related functions
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#ifndef SVN_DIFF_PRIVATE_H
#define SVN_DIFF_PRIVATE_H

#include "svn_io.h"
#include "svn_string.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* --- Diff parsing --- */

/* A single hunk inside a patch */
typedef struct svn_hunk_t {
  /* The hunk's text as it appeared in the patch file,
   * without range information. */
  const svn_string_t *diff_text;

  /* The original and modified texts in the hunk range.
   * Derived from the diff text. */
  const svn_string_t *original_text;
  const svn_string_t *modified_text;

  /* Hunk ranges as they appeared in the patch file.
   * All numbers are lines, not bytes. */
  svn_linenum_t original_start;
  svn_linenum_t original_length;
  svn_linenum_t modified_start;
  svn_linenum_t modified_length;
} svn_hunk_t;

/* Data type to manage parsing of patches. */
typedef struct svn_patch_t {
  /* The patch file itself. */
  apr_file_t *patch_file;

  /* The old and new file names as retrieved from the patch file.
   * These paths are UTF-8 encoded and canonicalized, but otherwise
   * left unchanged from how they appeared in the patch file. */
  const char *old_filename;
  const char *new_filename;

  /* EOL string used in patch file. */
  const char *eol_str;
} svn_patch_t;

/* Return the next *PATCH in PATCH_FILE. The patch file is assumed to
 * have consistent EOL-markers as specified in EOL_STR.
 * If no patch can be found, set *PATCH to NULL.
 * Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for all other allocations. */
svn_error_t *
svn_diff__parse_next_patch(svn_patch_t **patch,
                           apr_file_t *patch_file,
                           const char *eol_str,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Return the next *HUNK from a PATCH.
 * If no hunk can be found, set *HUNK to NULL.
 * Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for all other allocations. */
svn_error_t *
svn_diff__parse_next_hunk(svn_hunk_t **hunk,
                          svn_patch_t *patch,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIFF_PRIVATE_H */
