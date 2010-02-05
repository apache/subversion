/**
 * svn_diff_private.h: libsvn_diff related functions
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
   * Derived from the diff text.
   *
   * For example, consider a hunk such as:
   *   @@ -1,5 +1,5 @@
   *    #include <stdio.h>
   *    int main(int argc, char *argv[])
   *    {
   *   -        printf("Hello World!\n");
   *   +        printf("I like Subversion!\n");
   *    }
   *
   * Then, the original text described by the hunk is:
   *   #include <stdio.h>
   *   int main(int argc, char *argv[])
   *   {
   *           printf("Hello World!\n");
   *   }
   *
   * And the modified text described by the hunk is:
   *   #include <stdio.h>
   *   int main(int argc, char *argv[])
   *   {
   *           printf("I like Subversion!\n");
   *   }
   *
   * Because these streams make use of line filtering and transformation,
   * they should only be read line-by-line with svn_stream_readline().
   * Reading them with svn_stream_read() will not yield the expected result,
   * because it will return the unidiff text from the patch file unmodified.
   * The streams support resetting.
   */
  svn_stream_t *original_text;
  svn_stream_t *modified_text;

  /* Hunk ranges as they appeared in the patch file.
   * All numbers are lines, not bytes. */
  svn_linenum_t original_start;
  svn_linenum_t original_length;
  svn_linenum_t modified_start;
  svn_linenum_t modified_length;

  /* Number of lines starting with ' ' before first '+' or '-'. */
  svn_linenum_t leading_context;

  /* Number of lines starting with ' ' after last '+' or '-'. */
  svn_linenum_t trailing_context;
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

  /* An array containing an svn_hunk_t object for each hunk parsed
   * from the patch. */
  apr_array_header_t *hunks;
} svn_patch_t;

/* Return the next *PATCH in PATCH_FILE.
 * If no patch can be found, set *PATCH to NULL.
 * If reverse is TRUE, invert the patch while parsing it.
 * Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for all other allocations. */
svn_error_t *
svn_diff__parse_next_patch(svn_patch_t **patch,
                           apr_file_t *patch_file,
                           svn_boolean_t reverse,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Dispose of PATCH, closing any streams used by it. */
svn_error_t *
svn_diff__close_patch(const svn_patch_t *patch);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIFF_PRIVATE_H */
