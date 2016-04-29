/*
 * util.c: Utility functions for 'svnmover'
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

#include "svnmover.h"

#ifdef HAVE_LINENOISE
#include "linenoise/linenoise.c"
#else
#include "svn_cmdline.h"
#endif


svn_error_t *
svnmover_prompt_user(const char **result,
                     const char *prompt_str,
                     apr_pool_t *pool)
{
#ifdef HAVE_LINENOISE
  char *input;

  input = linenoise(prompt_str);
  if (! input)
    {
      return svn_error_create(SVN_ERR_CANCELLED, NULL, NULL);
    }
  /* add the line to the recallable history (if non-empty) */
  if (input && *input)
    {
      linenoiseHistoryAdd(input);
    }
  *result = apr_pstrdup(pool, input);
  free(input);
#else
  SVN_ERR(svn_cmdline_prompt_user2(result, prompt_str, NULL, pool));
#endif
  return SVN_NO_ERROR;
}


