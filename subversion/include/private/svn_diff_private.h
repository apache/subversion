/**
 * svn_diff_private.h: libsvn_diff related functions
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
  /* The hunk's unidiff text as it appeared in the patch file,
   * without range information. */
  svn_stream_t *diff_text;

  /* The original and modified texts in the hunk range.
   * Derived from the diff text. The lines are read verbatim
   * from the patch file, so the first character of each line
   * read from these streams is a space or a '-' in case of
   * the original text, or a space and a '+' in case of the
   * modified text. */
  svn_stream_t *original_text;
  svn_stream_t *modified_text;

  /* Hunk ranges as they appeared in the patch file.
   * All numbers are lines, not bytes. */
  svn_linenum_t original_start;
  svn_linenum_t original_length;
  svn_linenum_t modified_start;
  svn_linenum_t modified_length;
} svn_hunk_t;

/* Data type to manage parsing of patches. */
typedef struct svn_patch_t {
  /* Path to the patch file. */
  const char *path;

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

/* 
 * This function should be called before clearing or destroying the pool
 * HUNK was allocated in (i.e. the result pool passed to
 * svn_diff__parse_next_hunk()).
 * It ensures that all streams which were opened for the hunk are closed.
 **/
svn_error_t *
svn_diff__destroy_hunk(svn_hunk_t *hunk);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIFF_PRIVATE_H */
