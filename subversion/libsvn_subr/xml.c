/*
 * xml.c:  xml helper code shared among the Subversion libraries.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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



#include <string.h>
#include <assert.h>
#include "svn_pools.h"
#include "svn_xml.h"




/*** XML escaping. ***/

static void
xml_escape (svn_stringbuf_t **outstr,
            const char *data,
            apr_size_t len,
            apr_pool_t *pool)
{
  const char *end = data + len;
  const char *p = data, *q;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create ("", pool);

  while (1)
    {
      /* Find a character which needs to be quoted and append bytes up
         to that point.  Strictly speaking, '>' only needs to be
         quoted if it follows "]]", but it's easier to quote it all
         the time.  */
      q = p;
      while (q < end && *q != '&' && *q != '<' && *q != '>'
             && *q != '"' && *q != '\'')
        q++;
      svn_stringbuf_appendbytes (*outstr, p, q - p);

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (*q == '&')
        svn_stringbuf_appendcstr (*outstr, "&amp;");
      else if (*q == '<')
        svn_stringbuf_appendcstr (*outstr, "&lt;");
      else if (*q == '>')
        svn_stringbuf_appendcstr (*outstr, "&gt;");
      else if (*q == '"')
        svn_stringbuf_appendcstr (*outstr, "&quot;");
      else if (*q == '\'')
        svn_stringbuf_appendcstr (*outstr, "&apos;");

      p = q + 1;
    }
}


static void
xml_unescape (svn_stringbuf_t **outstr,
              const char *data,
              apr_size_t len,
              apr_pool_t *pool)
{
  const char *end = data + len;
  const char *p = data, *q;

  if (*outstr == NULL)
    *outstr = svn_stringbuf_create ("", pool);

  while (1)
    {
      /* Search for magical, mystical escaped xml characters, and
         replace them with their single-byte equivalents.  Currently,
         apr_xml_quote_string only escape `<', `>', `&', and
         optionally `"'.  We'll add `'' to this because
         svn_xml_escape_*() escapes it.  */
      q = p;

      /* Advance to the next '&'.  */
      while (q < end && *q != '&')
        q++;
      svn_stringbuf_appendbytes (*outstr, p, q - p);

      /* We may already be a winner.  */
      if (q == end)
        break;

      /* Append the entity reference for the character.  */
      if (((end - q) >= 5)
          && (q[0] == '&')
          && (q[1] == 'a')
          && (q[2] == 'm')
          && (q[3] == 'p')
          && (q[4] == ';'))
        {
          svn_stringbuf_appendcstr (*outstr, "&");
          p = q + 5;
        }
      else if (((end - q) >= 4)
          && (q[0] == '&')
          && (q[1] == 'l')
          && (q[2] == 't')
          && (q[3] == ';'))
        {
          svn_stringbuf_appendcstr (*outstr, "<");
          p = q + 4;
        }
      else if (((end - q) >= 4)
          && (q[0] == '&')
          && (q[1] == 'g')
          && (q[2] == 't')
          && (q[3] == ';'))
        {
          svn_stringbuf_appendcstr (*outstr, ">");
          p = q + 4;
        }
      else if (((end - q) >= 6)
          && (q[0] == '&')
          && (q[1] == 'q')
          && (q[2] == 'u')
          && (q[3] == 'o')
          && (q[4] == 't')
          && (q[5] == ';'))
        {
          svn_stringbuf_appendcstr (*outstr, "\"");
          p = q + 6;
        }
      else if (((end - q) >= 6)
          && (q[0] == '&')
          && (q[1] == 'a')
          && (q[2] == 'p')
          && (q[3] == 'o')
          && (q[4] == 's')
          && (q[5] == ';'))
        {
          svn_stringbuf_appendcstr (*outstr, "'");
          p = q + 6;
        }
      else
        {
          p = q + 1;
        }
    }
}


void
svn_xml_escape_stringbuf (svn_stringbuf_t **outstr,
                          const svn_stringbuf_t *string,
                          apr_pool_t *pool)
{
  xml_escape (outstr, string->data, string->len, pool);
}


void
svn_xml_escape_string (svn_stringbuf_t **outstr,
                       const svn_string_t *string,
                       apr_pool_t *pool)
{
  xml_escape (outstr, string->data, string->len, pool);
}


void
svn_xml_escape_cstring (svn_stringbuf_t **outstr,
                        const char *string,
                        apr_pool_t *pool)
{
  xml_escape (outstr, string, (apr_size_t) strlen (string), pool);
}


void
svn_xml_unescape_stringbuf (svn_stringbuf_t **outstr,
                            const svn_stringbuf_t *string,
                            apr_pool_t *pool)
{
  xml_unescape (outstr, string->data, string->len, pool);
}


void
svn_xml_unescape_string (svn_stringbuf_t **outstr,
                         const svn_string_t *string,
                         apr_pool_t *pool)
{
  xml_unescape (outstr, string->data, string->len, pool);
}


void
svn_xml_unescape_cstring (svn_stringbuf_t **outstr,
                          const char *string,
                          apr_pool_t *pool)
{
  xml_unescape (outstr, string, (apr_size_t) strlen (string), pool);
}



/*** Making a parser. ***/

svn_xml_parser_t *
svn_xml_make_parser (void *userData,
                     XML_StartElementHandler start_handler,
                     XML_EndElementHandler end_handler,
                     XML_CharacterDataHandler data_handler,
                     apr_pool_t *pool)
{
  svn_xml_parser_t *svn_parser;
  apr_pool_t *subpool;

  XML_Parser parser = XML_ParserCreate (NULL);

  XML_SetUserData (parser, userData);
  XML_SetElementHandler (parser, start_handler, end_handler); 
  XML_SetCharacterDataHandler (parser, data_handler);

  subpool = svn_pool_create (pool);

  svn_parser = apr_pcalloc (subpool, sizeof (*svn_parser));

  svn_parser->parser = parser;
  svn_parser->pool   = subpool;

  return svn_parser;
}


/* Free a parser */
void
svn_xml_free_parser (svn_xml_parser_t *svn_parser)
{
  /* Free the expat parser */
  XML_ParserFree (svn_parser->parser);        

  /* Free the subversion parser */
  svn_pool_destroy (svn_parser->pool);
}




svn_error_t *
svn_xml_parse (svn_xml_parser_t *svn_parser,
               const char *buf,
               apr_size_t len,
               svn_boolean_t is_final)
{
  svn_error_t *err;
  int success;

  /* Parse some xml data */
  success = XML_Parse (svn_parser->parser, buf, len, is_final);

  /* If expat choked internally, return its error. */
  if (! success)
    {
      err = svn_error_createf
        (SVN_ERR_XML_MALFORMED, NULL, 
         "%s at line %d",
         XML_ErrorString (XML_GetErrorCode (svn_parser->parser)),
         XML_GetCurrentLineNumber (svn_parser->parser));
      
      /* Kill all parsers and return the expat error */
      svn_xml_free_parser (svn_parser);
      return err;
    }

  /* Did an error occur somewhere *inside* the expat callbacks? */
  if (svn_parser->error)
    {
      err = svn_parser->error;
      svn_xml_free_parser (svn_parser);
      return err;
    }
  
  return SVN_NO_ERROR;
}



void svn_xml_signal_bailout (svn_error_t *error,
                             svn_xml_parser_t *svn_parser)
{
  /* This will cause the current XML_Parse() call to finish quickly! */
  XML_SetElementHandler (svn_parser->parser, NULL, NULL);
  XML_SetCharacterDataHandler (svn_parser->parser, NULL);
  
  /* Once outside of XML_Parse(), the existence of this field will
     cause svn_delta_parse()'s main read-loop to return error. */
  svn_parser->error = error;
}








/*** Attribute walking. ***/

const char *
svn_xml_get_attr_value (const char *name, const char **atts)
{
  while (atts && (*atts))
    {
      if (strcmp (atts[0], name) == 0)
        return atts[1];
      else
        atts += 2; /* continue looping */
    }

  /* Else no such attribute name seen. */
  return NULL;
}



/*** Printing XML ***/

void
svn_xml_make_header (svn_stringbuf_t **str, apr_pool_t *pool)
{
  if (*str == NULL)
    *str = svn_stringbuf_create ("", pool);
  svn_stringbuf_appendcstr (*str, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
}



/*** Creating attribute hashes. ***/

/* Combine an existing attribute list ATTS with a HASH that itself
   represents an attribute list.  Iff PRESERVE is true, then no value
   already in HASH will be changed, else values from ATTS will
   override previous values in HASH. */
static void
amalgamate (const char **atts,
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
        assert (key != NULL);
        /* kff todo: should we also insist that val be non-null here? 
           Probably. */

        keylen = strlen (key);
        if (preserve && ((apr_hash_get (ht, key, keylen)) != NULL))
          continue;
        else
          apr_hash_set (ht, apr_pstrndup(pool, key, keylen), keylen, 
                        val ? apr_pstrdup (pool, val) : NULL);
      }
}


apr_hash_t *
svn_xml_ap_to_hash (va_list ap, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_hash_make (pool);
  const char *key;
  
  while ((key = va_arg (ap, char *)) != NULL)
    {
      const char *val = va_arg (ap, const char *);
      apr_hash_set (ht, key, APR_HASH_KEY_STRING, val);
    }

  return ht;
}


apr_hash_t *
svn_xml_make_att_hash (const char **atts, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_hash_make (pool);
  amalgamate (atts, ht, 0, pool);  /* third arg irrelevant in this case */
  return ht;
}


void
svn_xml_hash_atts_overlaying (const char **atts,
                              apr_hash_t *ht,
                              apr_pool_t *pool)
{
  amalgamate (atts, ht, 0, pool);
}


void
svn_xml_hash_atts_preserving (const char **atts,
                              apr_hash_t *ht,
                              apr_pool_t *pool)
{
  amalgamate (atts, ht, 1, pool);
}



/*** Making XML tags. ***/


void
svn_xml_make_open_tag_hash (svn_stringbuf_t **str,
                            apr_pool_t *pool,
                            enum svn_xml_open_tag_style style,
                            const char *tagname,
                            apr_hash_t *attributes)
{
  apr_hash_index_t *hi;

  if (*str == NULL)
    *str = svn_stringbuf_create ("", pool);

  svn_stringbuf_appendcstr (*str, "<");
  svn_stringbuf_appendcstr (*str, tagname);

  for (hi = apr_hash_first (pool, attributes); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;

      apr_hash_this (hi, &key, NULL, &val);
      assert (val != NULL);

      svn_stringbuf_appendcstr (*str, "\n   ");
      svn_stringbuf_appendcstr (*str, key);
      svn_stringbuf_appendcstr (*str, "=\"");
      svn_xml_escape_cstring (str, val, pool);
      svn_stringbuf_appendcstr (*str, "\"");
    }

  if (style == svn_xml_self_closing)
    svn_stringbuf_appendcstr (*str, "/");
  svn_stringbuf_appendcstr (*str, ">");
  if (style != svn_xml_protect_pcdata)
    svn_stringbuf_appendcstr (*str, "\n");
}


void
svn_xml_make_open_tag_v (svn_stringbuf_t **str,
                         apr_pool_t *pool,
                         enum svn_xml_open_tag_style style,
                         const char *tagname,
                         va_list ap)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *ht = svn_xml_ap_to_hash (ap, subpool);

  svn_xml_make_open_tag_hash (str, pool, style, tagname, ht);
  svn_pool_destroy (subpool);
}



void
svn_xml_make_open_tag (svn_stringbuf_t **str,
                       apr_pool_t *pool,
                       enum svn_xml_open_tag_style style,
                       const char *tagname,
                       ...)
{
  va_list ap;

  va_start (ap, tagname);
  svn_xml_make_open_tag_v (str, pool, style, tagname, ap);
  va_end (ap);
}


void svn_xml_make_close_tag (svn_stringbuf_t **str,
                             apr_pool_t *pool,
                             const char *tagname)
{
  if (*str == NULL)
    *str = svn_stringbuf_create ("", pool);

  svn_stringbuf_appendcstr (*str, "</");
  svn_stringbuf_appendcstr (*str, tagname);
  svn_stringbuf_appendcstr (*str, ">\n");
}
