/*
 * xml_writer.c:  svn_xml_writer_t implementation
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



/*** Includes. ***/

#include <assert.h>
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_io.h"
#include "svn_string.h"
#include "svn_xml.h"

#include "svn_private_config.h"

#define BUFFER_LENGTH 512


/*** svn_xml_writer_t constructor and destructor ***/

struct svn_xml_writer_t
{
  /* buffer and its offset */
  char buffer[BUFFER_LENGTH];
  apr_size_t offset;

  /* an output stream, to write the XML to. */
  svn_stream_t *ostream;

  /* where this object is allocated, so we can free it easily */
  apr_pool_t *pool;
};

svn_error_t *
svn_xml_writer_create(svn_xml_writer_t **writer,
                      svn_stream_t *ostream,
                      apr_pool_t *result_pool)
{
  svn_xml_writer_t *result = apr_palloc(result_pool, sizeof(*result));

  result->offset = 0;
  result->ostream = ostream;
  result->pool = result_pool;

  *writer = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_xml_writer_close(svn_xml_writer_t *writer)
{
  if (writer)
    {
      SVN_ERR(svn_xml_writer_flush(writer));
      SVN_ERR(svn_stream_close(writer->ostream));
      writer->ostream = NULL;
    }

  return SVN_NO_ERROR;
}


/*** Buffering and writing routines ***/

static svn_error_t *
xml_stream_write(svn_xml_writer_t *writer, const char *data, apr_size_t len)
{
  apr_size_t write_len = len;

  /* We're gonna bail on an incomplete write here only because we know
     that this stream is really stdout, which should never be blocking
     on us. */
  SVN_ERR(svn_stream_write(writer->ostream, data, &write_len));
  if (write_len != len)
    return svn_error_create(SVN_ERR_STREAM_UNEXPECTED_EOF, NULL,
                            _("Error writing to stream"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_xml_writer_flush(svn_xml_writer_t *writer)
{
  if (writer->offset > 0)
    {
      SVN_ERR(xml_stream_write(writer, writer->buffer, writer->offset));
      writer->offset = 0;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
xml_ensure_bytes(svn_xml_writer_t *writer, apr_size_t bytes)
{
  if (writer->offset + bytes > BUFFER_LENGTH)
    SVN_ERR(svn_xml_writer_flush(writer));

  return SVN_NO_ERROR;
}

static svn_error_t *
xml_write_byte(svn_xml_writer_t *writer, char b)
{
  SVN_ERR(xml_ensure_bytes(writer, 1));
  writer->buffer[writer->offset++] = b;
  return SVN_NO_ERROR;
}

static svn_error_t *
xml_write_bytes(svn_xml_writer_t *writer, const char *data, apr_size_t len)
{
  if (len < BUFFER_LENGTH)
    {
      SVN_ERR(xml_ensure_bytes(writer, len));
      memcpy(writer->buffer + writer->offset, data, len);
      writer->offset += len;
    }
  else
    {
      SVN_ERR(svn_xml_writer_flush(writer));
      SVN_ERR(xml_stream_write(writer, data, len));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
xml_write_cstring(svn_xml_writer_t *writer, const char *str)
{
  return svn_error_trace(xml_write_bytes(writer, str, strlen(str)));
}

svn_error_t *
svn_xml_write_raw(svn_xml_writer_t *writer,
                  const char *data, apr_size_t len)
{
  return svn_error_trace(xml_write_bytes(writer, data, len));
}

/* XML Escaping */

static svn_error_t *
xml_write_escaped_attr(svn_xml_writer_t *xml_writer,
                       const char *data, apr_size_t len)
{
  const char *end = data + len;
  const char *p = data, *q;

  while (1)
    {
      /* Find a character which needs to be quoted and append bytes up
         to that point. */
      q = p;
      while (q < end && *q != '&' && *q != '<' && *q != '>' && *q != '"' &&
             *q != '\'' && *q != '\r' && *q != '\n' && *q != '\t')
        q++;
      SVN_ERR(xml_write_bytes(xml_writer, p, q - p));

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        SVN_ERR(xml_write_cstring(xml_writer, "&amp;"));
      else if (*q == '<')
        SVN_ERR(xml_write_cstring(xml_writer, "&lt;"));
      else if (*q == '>')
        SVN_ERR(xml_write_cstring(xml_writer, "&gt;"));
      else if (*q == '"')
        SVN_ERR(xml_write_cstring(xml_writer, "&quot;"));
      else if (*q == '\'')
        SVN_ERR(xml_write_cstring(xml_writer, "&apos;"));
      else if (*q == '\r')
        SVN_ERR(xml_write_cstring(xml_writer, "&#13;"));
      else if (*q == '\n')
        SVN_ERR(xml_write_cstring(xml_writer, "&#10;"));
      else if (*q == '\t')
        SVN_ERR(xml_write_cstring(xml_writer, "&#9;"));

      p = q + 1;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_xml_write_cdata(svn_xml_writer_t *xml_writer,
                    const char *data, apr_size_t len)
{
  const char *end = data + len;
  const char *p = data, *q;

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
      SVN_ERR(xml_write_bytes(xml_writer, p, q - p));

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        SVN_ERR(xml_write_cstring(xml_writer, "&amp;"));
      else if (*q == '<')
        SVN_ERR(xml_write_cstring(xml_writer, "&lt;"));
      else if (*q == '>')
        SVN_ERR(xml_write_cstring(xml_writer, "&gt;"));
      else if (*q == '\r')
        SVN_ERR(xml_write_cstring(xml_writer, "&#13;"));

      p = q + 1;
    }

  return SVN_NO_ERROR;
}



/*** Writing an open tag ***/

static svn_error_t *
xml_write_attribute(svn_xml_writer_t *xml_writer,
                    const char *key, const char *val)
{
  SVN_ERR(xml_write_cstring(xml_writer, "\n   "));
  SVN_ERR(xml_write_cstring(xml_writer, key));
  SVN_ERR(xml_write_cstring(xml_writer, "=\""));
  SVN_ERR(xml_write_escaped_attr(xml_writer, val, strlen(val)));
  SVN_ERR(xml_write_byte(xml_writer, '"'));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_xml_write_open_tag_hash(svn_xml_writer_t *xml_writer,
                            apr_pool_t *scratch_pool,
                            enum svn_xml_open_tag_style style,
                            const char *tagname, apr_hash_t *attributes)
{
  apr_hash_index_t *hi;

  SVN_ERR(xml_write_byte(xml_writer, '<'));
  SVN_ERR(xml_write_cstring(xml_writer, tagname));

  for (hi = apr_hash_first(scratch_pool, attributes);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;

      apr_hash_this(hi, &key, NULL, &val);
      assert(val != NULL);

      SVN_ERR(xml_write_attribute(xml_writer, key, val));
    }

  if (style == svn_xml_self_closing)
    SVN_ERR(xml_write_byte(xml_writer, '/'));
  SVN_ERR(xml_write_byte(xml_writer, '>'));
  if (style != svn_xml_protect_pcdata)
    SVN_ERR(xml_write_byte(xml_writer, '\n'));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_xml_write_open_tag_v(svn_xml_writer_t *xml_writer,
                         apr_pool_t *scratch_pool,
                         enum svn_xml_open_tag_style style,
                         const char *tagname,
                         va_list ap)
{
  apr_hash_t *ht = svn_xml_ap_to_hash(ap, scratch_pool);
  return svn_error_trace(svn_xml_write_open_tag_hash(xml_writer, scratch_pool,
                                                     style, tagname, ht));
}

svn_error_t *
svn_xml_write_open_tag(svn_xml_writer_t *xml_writer,
                       apr_pool_t *scratch_pool,
                       enum svn_xml_open_tag_style style,
                       const char *tagname,
                       ...)
{
  va_list ap;
  svn_error_t *err;

  va_start(ap, tagname);
  err = svn_xml_write_open_tag_v(xml_writer, scratch_pool, style, tagname, ap);
  va_end(ap);

  return svn_error_trace(err);
}



svn_error_t *
svn_xml_write_cdata_cstring(svn_xml_writer_t *xml_writer, const char *str)
{
  return svn_error_trace(svn_xml_write_cdata(xml_writer, str, strlen(str)));
}

/* close tag */

svn_error_t *
svn_xml_write_close_tag(svn_xml_writer_t *xml_writer,
                        apr_pool_t *scratch_pool,
                        const char *tagname)
{
  SVN_ERR(xml_write_cstring(xml_writer, "</"));
  SVN_ERR(xml_write_cstring(xml_writer, tagname));
  SVN_ERR(xml_write_cstring(xml_writer, ">\n"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_xml_write_header(svn_xml_writer_t *xml_writer,
                     const char *encoding,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(xml_write_cstring(xml_writer, "<?xml version=\"1.0\""));
  if (encoding)
    {
      SVN_ERR(xml_write_cstring(xml_writer, " encoding=\""));
      SVN_ERR(xml_write_cstring(xml_writer, encoding));
      SVN_ERR(xml_write_cstring(xml_writer, "\""));
    }
  SVN_ERR(xml_write_cstring(xml_writer, "?>\n"));

  return SVN_NO_ERROR;
}
