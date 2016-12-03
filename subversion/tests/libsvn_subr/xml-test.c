/* xml-test.c --- tests for the XML parser
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

#include <apr.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_xml.h"

#include "../svn_test.h"

typedef struct xml_callbacks_baton_t
{
  svn_stringbuf_t *buf;
  svn_xml_parser_t *parser;
} xml_callbacks_baton_t;

/* Implements svn_xml_start_elem. Logs all invocations to svn_stringbuf_t
 * provided via BATTON. */
static void
strbuf_start_elem(void *baton, const char *name, const char **atts)
{
  xml_callbacks_baton_t *b = baton;
  svn_stringbuf_appendcstr(b->buf, "<");
  svn_stringbuf_appendcstr(b->buf, name);
  while (*atts)
  {
    svn_stringbuf_appendcstr(b->buf, " ");
    svn_stringbuf_appendcstr(b->buf, atts[0]);
    svn_stringbuf_appendcstr(b->buf, "=");
    svn_stringbuf_appendcstr(b->buf, atts[1]);
    atts += 2;
  }
  svn_stringbuf_appendcstr(b->buf, ">");
}

/* Implements svn_xml_end_elem. Logs all invocations to svn_stringbuf_t
 * provided via BATTON. */
static void
strbuf_end_elem(void *baton, const char *name)
{
  xml_callbacks_baton_t *b = baton;
  svn_stringbuf_appendcstr(b->buf, "</");
  svn_stringbuf_appendcstr(b->buf, name);
  svn_stringbuf_appendcstr(b->buf, ">");
}

/* Implements svn_xml_char_data. Logs all invocations to svn_stringbuf_t
 * provided via BATTON. */
static void
strbuf_cdata(void *baton, const char *data, apr_size_t len)
{
  xml_callbacks_baton_t *b = baton;
  svn_stringbuf_appendbytes(b->buf, data, len);
}

/* Implements svn_xml_char_data. Calls strbuf_end_elem() but also
 * signals XML parser bailout. */
static void
err_end_elem(void *baton, const char *name)
{
  xml_callbacks_baton_t *b = baton;

  /* Log invocation first. */
  strbuf_end_elem(baton, name);

  svn_xml_signal_bailout(svn_error_create(APR_EGENERAL, NULL, NULL),
                         b->parser);
}

static svn_error_t *
test_simple(apr_pool_t *pool)
{
  const char *xml = "<root><tag1>value</tag1><tag2 a='v' /></root>";
  const char *p;
  xml_callbacks_baton_t b;

  /* Test parsing XML in one chunk.*/
  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, strbuf_end_elem,
                                 strbuf_cdata, pool);

  SVN_ERR(svn_xml_parse(b.parser, xml, strlen(xml), TRUE));

  SVN_TEST_STRING_ASSERT(b.buf->data,
                         "<root><tag1>value</tag1><tag2 a=v></tag2></root>");
  svn_xml_free_parser(b.parser);

  /* Test parsing XML byte by byte.*/
  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, strbuf_end_elem,
                                 strbuf_cdata, pool);

  for (p = xml; *p; p++)
    {
      SVN_ERR(svn_xml_parse(b.parser, p, 1, FALSE));
    }
  SVN_ERR(svn_xml_parse(b.parser, NULL, 0, TRUE));
  svn_xml_free_parser(b.parser);

  SVN_TEST_STRING_ASSERT(b.buf->data,
                         "<root><tag1>value</tag1><tag2 a=v></tag2></root>");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_invalid_xml(apr_pool_t *pool)
{
  /* Invalid XML (missing </root>) */
  const char *xml = "<root><tag1>value</tag1>";
  xml_callbacks_baton_t b;
  svn_error_t *err;

  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, strbuf_end_elem,
                                 strbuf_cdata, pool);

  err = svn_xml_parse(b.parser, xml, strlen(xml), TRUE);

  SVN_TEST_ASSERT_ERROR(err, SVN_ERR_XML_MALFORMED);

  return SVN_NO_ERROR;
}

static svn_error_t *
test_signal_bailout(apr_pool_t *pool)
{
  /* Invalid XML (missing </root>) */
  const char *xml = "<root><tag1></tag1></root>";
  xml_callbacks_baton_t b;
  svn_error_t *err;

  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, err_end_elem,
                                 strbuf_cdata, pool);
  err = svn_xml_parse(b.parser, xml, strlen(xml), TRUE);
  SVN_TEST_ASSERT_ERROR(err, APR_EGENERAL);
  SVN_TEST_STRING_ASSERT(b.buf->data,
                         "<root><tag1></tag1>");

  return SVN_NO_ERROR;
}

static svn_error_t *
test_invalid_xml_signal_bailout(apr_pool_t *pool)
{
  /* Invalid XML (missing </root>) */
  const char *xml = "<root><tag1></tag1>";
  xml_callbacks_baton_t b;
  svn_error_t *err;
  apr_status_t status;

  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, NULL, err_end_elem, NULL, pool);
  err = svn_xml_parse(b.parser, xml, strlen(xml), TRUE);

  /* We may get SVN_ERR_XML_MALFORMED or error from err_end_elem() callback.
   * This behavior depends how XML parser works: it may pre-parse data before
   * callback invocation. */
  status = err->apr_err;
  SVN_TEST_ASSERT_ANY_ERROR(err); /* This clears err! */

  if (status != SVN_ERR_XML_MALFORMED && status != APR_EGENERAL)
    {
      return svn_error_createf(SVN_ERR_TEST_FAILED, NULL,
                               "Got unexpected error '%s'",
                               svn_error_symbolic_name(status));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_parser_free(apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool;

  /* Test explicit svn_xml_free_parser() calls. */
  iterpool = svn_pool_create(pool);
  for (i = 0; i < 100; i++)
  {
      svn_xml_parser_t *parser;

      svn_pool_clear(iterpool);

      parser = svn_xml_make_parser(&parser, NULL, NULL, NULL, iterpool);
      svn_xml_free_parser(parser);
  }
  svn_pool_destroy(iterpool);

  /* Test parser free using pool cleanup. */
  iterpool = svn_pool_create(pool);
  for (i = 0; i < 100; i++)
  {
      svn_xml_parser_t *parser;

      svn_pool_clear(iterpool);

      parser = svn_xml_make_parser(&parser, NULL, NULL, NULL, iterpool);
      /* We didn't call svn_xml_free_parser(): the parser will be freed on
         pool cleanup. */
  }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Test that builtin XML entities are expanded as expected. */
static svn_error_t *
test_xml_builtin_entity_expansion(apr_pool_t *pool)
{
  const char *xml =
    "<?xml version='1.0'?>\n"
    "<root a='&amp;'>&amp;&#9;</root>";

  xml_callbacks_baton_t b;

  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, strbuf_end_elem,
                                 strbuf_cdata, pool);

  SVN_ERR(svn_xml_parse(b.parser, xml, strlen(xml), TRUE));

  SVN_TEST_STRING_ASSERT(b.buf->data,
                         "<root a=&>&\t</root>");

  return SVN_NO_ERROR;
}

/* Test that custom XML entities are not allowed. */
static svn_error_t *
test_xml_custom_entity_expansion(apr_pool_t *pool)
{
  const char *xml =
    "<?xml version='1.0'?>\n"
    "<!DOCTYPE test ["
    "<!ELEMENT root (#PCDATA)>"
    "<!ENTITY xmlentity 'val'>"
    "]>"
    "<root>&xmlentity;</root>";

  xml_callbacks_baton_t b;
  svn_error_t *err;

  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, strbuf_end_elem,
                                 strbuf_cdata, pool);

  err = svn_xml_parse(b.parser, xml, strlen(xml), TRUE);

  /* XML entity declarations will be either silently ignored or error
     will be returned depending on Expat version. */
  if (err)
    {
      SVN_TEST_ASSERT_ERROR(err, SVN_ERR_XML_MALFORMED);
      SVN_TEST_STRING_ASSERT(b.buf->data,
                             "");
    }
  else
    {
      SVN_TEST_STRING_ASSERT(b.buf->data,
                             "<root></root>");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
test_xml_doctype_declaration(apr_pool_t *pool)
{
  const char *xml =
    "<?xml version='1.0'?>\n"
    "<?xml-stylesheet type='text/xsl' href='/svnindex.xsl'?>"
    "<!DOCTYPE svn ["
    "  <!ELEMENT svn   (index)>"
    "  <!ATTLIST svn   version CDATA #REQUIRED"
    "                  href    CDATA #REQUIRED>"
    "  <!ELEMENT index (updir?, (file | dir)*)>"
    "  <!ATTLIST index name    CDATA #IMPLIED"
    "                  path    CDATA #IMPLIED"
    "                  rev     CDATA #IMPLIED"
    "                  base    CDATA #IMPLIED>"
    "  <!ELEMENT updir EMPTY>"
    "  <!ATTLIST updir href    CDATA #REQUIRED>"
    "  <!ELEMENT file  EMPTY>"
    "  <!ATTLIST file  name    CDATA #REQUIRED"
    "                  href    CDATA #REQUIRED>"
    "  <!ELEMENT dir   EMPTY>"
    "  <!ATTLIST dir   name    CDATA #REQUIRED"
    "                  href    CDATA #REQUIRED>"
    "]>"
    "<svn version='1.9.4'>"
    "  <index rev='0' path='Collection of Repositories'>"
    "  </index>"
    "</svn>";

  xml_callbacks_baton_t b;

  b.buf = svn_stringbuf_create_empty(pool);
  b.parser = svn_xml_make_parser(&b, strbuf_start_elem, strbuf_end_elem,
                                 strbuf_cdata, pool);

  SVN_ERR(svn_xml_parse(b.parser, xml, strlen(xml), TRUE));

  SVN_TEST_STRING_ASSERT(b.buf->data,
                         "<svn version=1.9.4>"
                         "  <index rev=0 path=Collection of Repositories>"
                         "  </index>"
                         "</svn>");

  return SVN_NO_ERROR;
}

/* The test table.  */
static int max_threads = 1;

static struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_PASS2(test_simple,
                   "simple XML parser test"),
    SVN_TEST_PASS2(test_invalid_xml,
                   "invalid XML test"),
    SVN_TEST_PASS2(test_signal_bailout,
                   "test svn_xml_signal_bailout()"),
    SVN_TEST_PASS2(test_invalid_xml_signal_bailout,
                   "test svn_xml_signal_bailout() for invalid XML"),
    SVN_TEST_PASS2(test_parser_free,
                   "test svn_xml_parser_free()"),
    SVN_TEST_PASS2(test_xml_builtin_entity_expansion,
                   "test XML builtin entity expansion"),
    SVN_TEST_PASS2(test_xml_custom_entity_expansion,
                   "test XML custom entity expansion"),
    SVN_TEST_PASS2(test_xml_doctype_declaration,
                   "test XML doctype declaration"),
    SVN_TEST_NULL
  };

SVN_TEST_MAIN
