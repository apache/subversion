/*
 * xml_escape.c:  XML escaping routines
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



#include "svn_pools.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_ctype.h"

#include "private/svn_utf_private.h"
#include "private/svn_subr_private.h"


/*** XML character validation ***/

svn_boolean_t
svn_xml_is_xml_safe(const char *data, apr_size_t len)
{
  const char *end = data + len;
  const char *p;

  if (! svn_utf__is_valid(data, len))
    return FALSE;

  for (p = data; p < end; p++)
    {
      unsigned char c = *p;

      if (svn_ctype_iscntrl(c))
        {
          if ((c != SVN_CTYPE_ASCII_TAB)
              && (c != SVN_CTYPE_ASCII_LINEFEED)
              && (c != SVN_CTYPE_ASCII_CARRIAGERETURN)
              && (c != SVN_CTYPE_ASCII_DELETE))
            return FALSE;
        }
    }
  return TRUE;
}





/*** XML escaping. ***/

/* ### ...?
 *
 * If *OUTSTR is @c NULL, set *OUTSTR to a new stringbuf allocated
 * in POOL, else append to the existing stringbuf there.
 */
static void
xml_escape_cdata(svn_stringbuf_t **outstr,
                 const char *data,
                 apr_size_t len,
                 apr_pool_t *pool)
{
  const char *end = data + len;
  const char *p = data, *q;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create_empty(pool);

  while (1)
    {
      /* Find a character which needs to be quoted and append bytes up
         to that point.  Strictly speaking, '>' only needs to be
         quoted if it follows "]]", but it's easier to quote it all
         the time.

         So, why are we escaping '\r' here?  Well, according to the
         XML spec, '\r\n' gets converted to '\n' during XML parsing.
         Also, any '\r' not followed by '\n' is converted to '\n'.  By
         golly, if we say we want to escape a '\r', we want to make
         sure it remains a '\r'!  */
      q = p;
      while (q < end && *q != '&' && *q != '<' && *q != '>' && *q != '\r')
        q++;
      svn_stringbuf_appendbytes(*outstr, p, q - p);

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        svn_stringbuf_appendcstr(*outstr, "&amp;");
      else if (*q == '<')
        svn_stringbuf_appendcstr(*outstr, "&lt;");
      else if (*q == '>')
        svn_stringbuf_appendcstr(*outstr, "&gt;");
      else if (*q == '\r')
        svn_stringbuf_appendcstr(*outstr, "&#13;");

      p = q + 1;
    }
}

/* Essentially the same as xml_escape_cdata, with the addition of
   whitespace and quote characters. */
static void
xml_escape_attr(svn_stringbuf_t **outstr,
                const char *data,
                apr_size_t len,
                apr_pool_t *pool)
{
  const char *end = data + len;
  const char *p = data, *q;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create_ensure(len, pool);

  while (1)
    {
      /* Find a character which needs to be quoted and append bytes up
         to that point. */
      q = p;
      while (q < end && *q != '&' && *q != '<' && *q != '>'
             && *q != '"' && *q != '\'' && *q != '\r'
             && *q != '\n' && *q != '\t')
        q++;
      svn_stringbuf_appendbytes(*outstr, p, q - p);

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        svn_stringbuf_appendcstr(*outstr, "&amp;");
      else if (*q == '<')
        svn_stringbuf_appendcstr(*outstr, "&lt;");
      else if (*q == '>')
        svn_stringbuf_appendcstr(*outstr, "&gt;");
      else if (*q == '"')
        svn_stringbuf_appendcstr(*outstr, "&quot;");
      else if (*q == '\'')
        svn_stringbuf_appendcstr(*outstr, "&apos;");
      else if (*q == '\r')
        svn_stringbuf_appendcstr(*outstr, "&#13;");
      else if (*q == '\n')
        svn_stringbuf_appendcstr(*outstr, "&#10;");
      else if (*q == '\t')
        svn_stringbuf_appendcstr(*outstr, "&#9;");

      p = q + 1;
    }
}


void
svn_xml_escape_cdata_stringbuf(svn_stringbuf_t **outstr,
                               const svn_stringbuf_t *string,
                               apr_pool_t *pool)
{
  xml_escape_cdata(outstr, string->data, string->len, pool);
}


void
svn_xml_escape_cdata_string(svn_stringbuf_t **outstr,
                            const svn_string_t *string,
                            apr_pool_t *pool)
{
  xml_escape_cdata(outstr, string->data, string->len, pool);
}


void
svn_xml_escape_cdata_cstring(svn_stringbuf_t **outstr,
                             const char *string,
                             apr_pool_t *pool)
{
  xml_escape_cdata(outstr, string, (apr_size_t) strlen(string), pool);
}


void
svn_xml_escape_attr_stringbuf(svn_stringbuf_t **outstr,
                              const svn_stringbuf_t *string,
                              apr_pool_t *pool)
{
  xml_escape_attr(outstr, string->data, string->len, pool);
}


void
svn_xml_escape_attr_string(svn_stringbuf_t **outstr,
                           const svn_string_t *string,
                           apr_pool_t *pool)
{
  xml_escape_attr(outstr, string->data, string->len, pool);
}


void
svn_xml_escape_attr_cstring(svn_stringbuf_t **outstr,
                            const char *string,
                            apr_pool_t *pool)
{
  xml_escape_attr(outstr, string, (apr_size_t) strlen(string), pool);
}


const char *
svn_xml_fuzzy_escape(const char *string, apr_pool_t *pool)
{
  const char *end = string + strlen(string);
  const char *p = string, *q;
  svn_stringbuf_t *outstr;
  char escaped_char[6];   /* ? \ u u u \0 */

  for (q = p; q < end; q++)
    {
      if (svn_ctype_iscntrl(*q)
          && ! ((*q == '\n') || (*q == '\r') || (*q == '\t')))
        break;
    }

  /* Return original string if no unsafe characters found. */
  if (q == end)
    return string;

  outstr = svn_stringbuf_create_empty(pool);
  while (1)
    {
      q = p;

      /* Traverse till either unsafe character or eos. */
      while ((q < end)
             && ((! svn_ctype_iscntrl(*q))
                 || (*q == '\n') || (*q == '\r') || (*q == '\t')))
        q++;

      /* copy chunk before marker */
      svn_stringbuf_appendbytes(outstr, p, q - p);

      if (q == end)
        break;

      /* Append an escaped version of the unsafe character.

         ### This format was chosen for consistency with
         ### svn_utf__cstring_from_utf8_fuzzy().  The two functions
         ### should probably share code, even though they escape
         ### different characters.
      */
      apr_snprintf(escaped_char, sizeof(escaped_char), "?\\%03u",
                   (unsigned char) *q);
      svn_stringbuf_appendcstr(outstr, escaped_char);

      p = q + 1;
    }

  return outstr->data;
}
