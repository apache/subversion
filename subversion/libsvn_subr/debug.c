/*
 * debug.c :  small functions to help SVN developers
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

/* These functions are only available to SVN developers and should never
   be used in release code. One of the reasons to avoid this code in release
   builds is that this code is not thread-safe. */
#include <stdarg.h>

#include "svn_types.h"
#include "svn_string.h"

#include "private/svn_debug.h"


#define DBG_FLAG "DBG: "

/* This will be tweaked by the preamble code.  */
static FILE * volatile debug_output = NULL;


static svn_boolean_t
quiet_mode(void)
{
  return getenv("SVN_DBG_QUIET") != NULL;
}


void
svn_dbg__preamble(const char *file, long line, FILE *output)
{
  debug_output = output;

  if (output != NULL && !quiet_mode())
    {
      /* Quick and dirty basename() code.  */
      const char *slash = strrchr(file, '/');

      if (slash == NULL)
        slash = strrchr(file, '\\');
      if (slash == NULL)
        slash = file;
      else
        ++slash;

      fprintf(output, DBG_FLAG "%s:%4ld: ", slash, line);
    }
}


static void
print_line(const char *fmt, va_list ap)
{
  FILE *output = debug_output;

  if (output == NULL || quiet_mode())
    return;

  (void) vfprintf(output, fmt, ap);
}


void
svn_dbg__printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  print_line(fmt, ap);
  va_end(ap);
}


void
svn_dbg__print_props(apr_hash_t *props,
                     const char *header_fmt,
                     ...)
{
/* We only build this code if SVN_DEBUG is defined. */
#ifdef SVN_DEBUG

  apr_hash_index_t *hi;
  va_list ap;

  va_start(ap, header_fmt);
  print_line(header_fmt, ap);
  va_end(ap);

  if (props == NULL)
    {
      SVN_DBG(("    (null)\n"));
      return;
    }

  for (hi = apr_hash_first(apr_hash_pool_get(props), props); hi;
        hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      svn_string_t *val = svn__apr_hash_index_val(hi);

      SVN_DBG(("    '%s' -> '%s'\n", name, val->data));
    }
#endif /* SVN_DEBUG */
}

