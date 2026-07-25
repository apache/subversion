/*
 * xml.c:  xml helper code shared among the Subversion libraries.
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



#include <string.h>
#include <assert.h>

#include "svn_private_config.h"         /* for SVN_HAVE_OLD_EXPAT */
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_xml.h"
#include "svn_error.h"

#include "private/svn_subr_private.h"

#ifdef SVN_HAVE_OLD_EXPAT
#include <xmlparse.h>
#else
#include <expat.h>
#endif

#ifndef XML_VERSION_AT_LEAST
#define XML_VERSION_AT_LEAST(major,minor,patch)                  \
(((major) < XML_MAJOR_VERSION)                                       \
 || ((major) == XML_MAJOR_VERSION && (minor) < XML_MINOR_VERSION)    \
 || ((major) == XML_MAJOR_VERSION && (minor) == XML_MINOR_VERSION && \
     (patch) <= XML_MICRO_VERSION))
#endif /* XML_VERSION_AT_LEAST */

#ifdef XML_UNICODE
#error Expat is unusable -- it has been compiled for wide characters
#endif

const char *
svn_xml__compiled_version(void)
{
  static const char xml_version_str[] = APR_STRINGIFY(XML_MAJOR_VERSION)
                                        "." APR_STRINGIFY(XML_MINOR_VERSION)
                                        "." APR_STRINGIFY(XML_MICRO_VERSION);

  return xml_version_str;
}

const char *
svn_xml__runtime_version(void)
{
  const char *expat_version = XML_ExpatVersion();

  if (!strncmp(expat_version, "expat_", 6))
    expat_version += 6;

  return expat_version;
}


/* The private internals for a parser object. */
struct svn_xml_parser_t
{
  /** the expat parser */
  XML_Parser parser;

  /** the SVN callbacks to call from the Expat callbacks */
  svn_xml_start_elem start_handler;
  svn_xml_end_elem end_handler;
  svn_xml_char_data data_handler;

  /** the user's baton for private data */
  void *baton;

  /** if non-@c NULL, an error happened while parsing */
  svn_error_t *error;

  /** where this object is allocated, so we can free it easily */
  apr_pool_t *pool;

};


/*** Map from the Expat callback types to the SVN XML types. ***/

static void expat_start_handler(void *userData,
                                const XML_Char *name,
                                const XML_Char **atts)
{
  svn_xml_parser_t *svn_parser = userData;

  (*svn_parser->start_handler)(svn_parser->baton, name, atts);

#if XML_VERSION_AT_LEAST(1, 95, 8)
  /* Stop XML parsing if svn_xml_signal_bailout() was called.
     We cannot do this in svn_xml_signal_bailout() because Expat
     documentation states that XML_StopParser() must be called only from
     callbacks. */
  if (svn_parser->error)
    (void) XML_StopParser(svn_parser->parser, 0 /* resumable */);
#endif
}

static void expat_end_handler(void *userData, const XML_Char *name)
{
  svn_xml_parser_t *svn_parser = userData;

  (*svn_parser->end_handler)(svn_parser->baton, name);

#if XML_VERSION_AT_LEAST(1, 95, 8)
  /* Stop XML parsing if svn_xml_signal_bailout() was called.
     We cannot do this in svn_xml_signal_bailout() because Expat
     documentation states that XML_StopParser() must be called only from
     callbacks. */
  if (svn_parser->error)
    (void) XML_StopParser(svn_parser->parser, 0 /* resumable */);
#endif
}

static void expat_data_handler(void *userData, const XML_Char *s, int len)
{
  svn_xml_parser_t *svn_parser = userData;

  (*svn_parser->data_handler)(svn_parser->baton, s, (apr_size_t)len);

#if XML_VERSION_AT_LEAST(1, 95, 8)
  /* Stop XML parsing if svn_xml_signal_bailout() was called.
     We cannot do this in svn_xml_signal_bailout() because Expat
     documentation states that XML_StopParser() must be called only from
     callbacks. */
  if (svn_parser->error)
    (void) XML_StopParser(svn_parser->parser, 0 /* resumable */);
#endif
}

#if XML_VERSION_AT_LEAST(1, 95, 8)
static void expat_entity_declaration(void *userData,
                                     const XML_Char *entityName,
                                     int is_parameter_entity,
                                     const XML_Char *value,
                                     int value_length,
                                     const XML_Char *base,
                                     const XML_Char *systemId,
                                     const XML_Char *publicId,
                                     const XML_Char *notationName)
{
  svn_xml_parser_t *svn_parser = userData;

  /* Stop the parser if an entity declaration is hit. */
  XML_StopParser(svn_parser->parser, 0 /* resumable */);
}
#else
/* A noop default_handler. */
static void expat_default_handler(void *userData, const XML_Char *s, int len)
{
}
#endif

/*** Making a parser. ***/

static apr_status_t parser_cleanup(void *data)
{
  svn_xml_parser_t *svn_parser = data;

  /* Free Expat parser. */
  if (svn_parser->parser)
    {
      XML_ParserFree(svn_parser->parser);
      svn_parser->parser = NULL;
    }
  return APR_SUCCESS;
}

svn_xml_parser_t *
svn_xml_make_parser(void *baton,
                    svn_xml_start_elem start_handler,
                    svn_xml_end_elem end_handler,
                    svn_xml_char_data data_handler,
                    apr_pool_t *pool)
{
  svn_xml_parser_t *svn_parser;
  XML_Parser parser = XML_ParserCreate(NULL);

  XML_SetElementHandler(parser,
                        start_handler ? expat_start_handler : NULL,
                        end_handler ? expat_end_handler : NULL);
  XML_SetCharacterDataHandler(parser,
                              data_handler ? expat_data_handler : NULL);

#if XML_VERSION_AT_LEAST(1, 95, 8)
  XML_SetEntityDeclHandler(parser, expat_entity_declaration);
#else
  XML_SetDefaultHandler(parser, expat_default_handler);
#endif

  svn_parser = apr_pcalloc(pool, sizeof(*svn_parser));

  svn_parser->parser = parser;
  svn_parser->start_handler = start_handler;
  svn_parser->end_handler = end_handler;
  svn_parser->data_handler = data_handler;
  svn_parser->baton = baton;
  svn_parser->pool = pool;

  /* store our parser info as the UserData in the Expat parser */
  XML_SetUserData(parser, svn_parser);

  /* Register pool cleanup handler to free Expat XML parser on cleanup,
     if svn_xml_free_parser() was not called explicitly. */
  apr_pool_cleanup_register(svn_parser->pool, svn_parser,
                            parser_cleanup, apr_pool_cleanup_null);

  return svn_parser;
}


/* Free a parser */
void
svn_xml_free_parser(svn_xml_parser_t *svn_parser)
{
  apr_pool_cleanup_run(svn_parser->pool, svn_parser, parser_cleanup);
}




svn_error_t *
svn_xml_parse(svn_xml_parser_t *svn_parser,
              const char *buf,
              apr_size_t len,
              svn_boolean_t is_final)
{
  svn_error_t *err;
  int success;

  /* Parse some xml data */
  success = XML_Parse(svn_parser->parser, buf, (int) len, is_final);

  /* Did an error occur somewhere *inside* the expat callbacks? */
  if (svn_parser->error)
    {
      /* Kill all parsers and return the error */
      svn_xml_free_parser(svn_parser);
      return svn_parser->error;
    }

  /* If expat choked internally, return its error. */
  if (! success)
    {
      /* Line num is "int" in Expat v1, "long" in v2; hide the difference. */
      long line = XML_GetCurrentLineNumber(svn_parser->parser);

      err = svn_error_createf
        (SVN_ERR_XML_MALFORMED, NULL,
         _("Malformed XML: %s at line %ld"),
         XML_ErrorString(XML_GetErrorCode(svn_parser->parser)), line);

      /* Kill all parsers and return the expat error */
      svn_xml_free_parser(svn_parser);
      return err;
    }

  return SVN_NO_ERROR;
}



void svn_xml_signal_bailout(svn_error_t *error,
                            svn_xml_parser_t *svn_parser)
{
  /* This will cause the current XML_Parse() call to finish quickly! */
  XML_SetElementHandler(svn_parser->parser, NULL, NULL);
  XML_SetCharacterDataHandler(svn_parser->parser, NULL);
#if XML_VERSION_AT_LEAST(1, 95, 8)
  XML_SetEntityDeclHandler(svn_parser->parser, NULL);
#endif
  /* Once outside of XML_Parse(), the existence of this field will
     cause svn_delta_parse()'s main read-loop to return error. */
  svn_parser->error = error;
}








/*** Attribute walking. ***/

const char *
svn_xml_get_attr_value(const char *name, const char *const *atts)
{
  while (atts && (*atts))
    {
      if (strcmp(atts[0], name) == 0)
        return atts[1];
      else
        atts += 2; /* continue looping */
    }

  /* Else no such attribute name seen. */
  return NULL;
}



/*** Printing XML ***/

void
svn_xml_make_header2(svn_stringbuf_t **str, const char *encoding,
                     apr_pool_t *pool)
{

  if (*str == NULL)
    *str = svn_stringbuf_create_empty(pool);
  svn_stringbuf_appendcstr(*str, "<?xml version=\"1.0\"");
  if (encoding)
    {
      encoding = apr_psprintf(pool, " encoding=\"%s\"", encoding);
      svn_stringbuf_appendcstr(*str, encoding);
    }
  svn_stringbuf_appendcstr(*str, "?>\n");
}



/*** Creating attribute hashes. ***/

/* Combine an existing attribute list ATTS with a HASH that itself
   represents an attribute list.  Iff PRESERVE is true, then no value
   already in HASH will be changed, else values from ATTS will
   override previous values in HASH. */
static void
amalgamate(const char **atts,
           apr_hash_t *ht,
           svn_boolean_t preserve,
           apr_pool_t *pool)
{
  const char *key;

  if (atts)
    for (key = *atts; key; key = *(++atts))
      {
        const char *val = *(++atts);
        size_t keylen;
        assert(key != NULL);
        /* kff todo: should we also insist that val be non-null here?
           Probably. */

        keylen = strlen(key);
        if (preserve && ((apr_hash_get(ht, key, keylen)) != NULL))
          continue;
        else
          apr_hash_set(ht, apr_pstrndup(pool, key, keylen), keylen,
                       val ? apr_pstrdup(pool, val) : NULL);
      }
}


apr_hash_t *
svn_xml_ap_to_hash(va_list ap, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_hash_make(pool);
  const char *key;

  while ((key = va_arg(ap, char *)) != NULL)
    {
      const char *val = va_arg(ap, const char *);
      svn_hash_sets(ht, key, val);
    }

  return ht;
}


apr_hash_t *
svn_xml_make_att_hash(const char **atts, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_hash_make(pool);
  amalgamate(atts, ht, 0, pool);  /* third arg irrelevant in this case */
  return ht;
}


void
svn_xml_hash_atts_overlaying(const char **atts,
                             apr_hash_t *ht,
                             apr_pool_t *pool)
{
  amalgamate(atts, ht, 0, pool);
}


void
svn_xml_hash_atts_preserving(const char **atts,
                             apr_hash_t *ht,
                             apr_pool_t *pool)
{
  amalgamate(atts, ht, 1, pool);
}



/*** Making XML tags. ***/


void
svn_xml_make_open_tag_hash(svn_stringbuf_t **str,
                           apr_pool_t *pool,
                           enum svn_xml_open_tag_style style,
                           const char *tagname,
                           apr_hash_t *attributes)
{
  apr_hash_index_t *hi;
  apr_size_t est_size = strlen(tagname) + 4 + apr_hash_count(attributes) * 30;

  if (*str == NULL)
    *str = svn_stringbuf_create_ensure(est_size, pool);

  svn_stringbuf_appendcstr(*str, "<");
  svn_stringbuf_appendcstr(*str, tagname);

  for (hi = apr_hash_first(pool, attributes); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;

      apr_hash_this(hi, &key, NULL, &val);
      assert(val != NULL);

      svn_stringbuf_appendcstr(*str, "\n   ");
      svn_stringbuf_appendcstr(*str, key);
      svn_stringbuf_appendcstr(*str, "=\"");
      svn_xml_escape_attr_cstring(str, val, pool);
      svn_stringbuf_appendcstr(*str, "\"");
    }

  if (style == svn_xml_self_closing)
    svn_stringbuf_appendcstr(*str, "/");
  svn_stringbuf_appendcstr(*str, ">");
  if (style != svn_xml_protect_pcdata)
    svn_stringbuf_appendcstr(*str, "\n");
}


void
svn_xml_make_open_tag_v(svn_stringbuf_t **str,
                        apr_pool_t *pool,
                        enum svn_xml_open_tag_style style,
                        const char *tagname,
                        va_list ap)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *ht = svn_xml_ap_to_hash(ap, subpool);

  svn_xml_make_open_tag_hash(str, pool, style, tagname, ht);
  svn_pool_destroy(subpool);
}



void
svn_xml_make_open_tag(svn_stringbuf_t **str,
                      apr_pool_t *pool,
                      enum svn_xml_open_tag_style style,
                      const char *tagname,
                      ...)
{
  va_list ap;

  va_start(ap, tagname);
  svn_xml_make_open_tag_v(str, pool, style, tagname, ap);
  va_end(ap);
}


void svn_xml_make_close_tag(svn_stringbuf_t **str,
                            apr_pool_t *pool,
                            const char *tagname)
{
  if (*str == NULL)
    *str = svn_stringbuf_create_empty(pool);

  svn_stringbuf_appendcstr(*str, "</");
  svn_stringbuf_appendcstr(*str, tagname);
  svn_stringbuf_appendcstr(*str, ">\n");
}
