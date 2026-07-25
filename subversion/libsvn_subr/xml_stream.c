/*
 * xml_stream.c:  implements a writable XML parse stream
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

#include "private/svn_xml_private.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_xml.h"


typedef struct xml_stream_baton_t
{
  /* Handle to an XML parser. NULL means that the parser has been already
     disposed or we've closed the stream. */
  svn_xml_parser_t *parser;
} xml_stream_baton_t;


/* This implements svn_write_fn_t. */
static svn_error_t *
xml_stream_write(void *baton, const char *data, apr_size_t *len)
{
  xml_stream_baton_t *b = baton;
  svn_error_t *err;

  /*
   * Check if the XML parser has already been freed.
   * This can happen if an error occurs during XML parsing.
   */
  if (b->parser == NULL)
    return NULL;

  err = svn_xml_parse(b->parser, data, *len, FALSE);

  if (err)
    {
      /* Dispose the parser due to an error. */
      svn_xml_free_parser(b->parser);
      b->parser = NULL;
    }

  return svn_error_trace(err);
}

/* This implements svn_close_fn_t. */
static svn_error_t *
xml_stream_close(void *baton)
{
  svn_error_t *err;
  xml_stream_baton_t *b = baton;

  if (b->parser)
    {
      /* Dispose the parser with a final push because we are closing
         the stream. */
      err = svn_xml_parse(b->parser, NULL, 0, TRUE);
      svn_xml_free_parser(b->parser);
      b->parser = NULL;
      return svn_error_trace(err);
    }
  else
    {
      /* We have nothing to do with a disposed stream; Probably it failed
         with an error before or we are now closing it second time. */
    }

  return SVN_NO_ERROR;
}



/* Public Interface */

svn_stream_t *
svn_xml__make_parse_stream(svn_xml_parser_t *parser,
                          apr_pool_t *result_pool)
{
  svn_stream_t *result;
  xml_stream_baton_t *baton;

  baton = apr_pcalloc(result_pool, sizeof(*baton));
  baton->parser = parser;

  result = svn_stream_create(baton, result_pool);

  svn_stream_set_write(result, xml_stream_write);
  svn_stream_set_close(result, xml_stream_close);

  return result;
}
