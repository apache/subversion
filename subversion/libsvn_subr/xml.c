/*
 * xml.c:  xml helper code shared among the Subversion libraries.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <string.h>
#include <assert.h>
#include "apr_pools.h"
#include "svn_xml.h"




/*** XML escaping. ***/

/* Return an xml-safe version of STRING. */
static svn_string_t *
escape_string (svn_string_t *string, apr_pool_t *pool)
{
  /* kff todo: do real quoting soon. */
  return svn_string_dup (string, pool);
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

  subpool = svn_pool_create (pool, NULL);

  svn_parser = apr_pcalloc (subpool, sizeof (svn_xml_parser_t));

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
  apr_destroy_pool (svn_parser->pool);
}




/* Push LEN bytes of xml data in BUF at SVN_PARSER.  If this is the
   final push, IS_FINAL must be set.  */
svn_error_t *
svn_xml_parse (svn_xml_parser_t *svn_parser,
               const char *buf,
               apr_ssize_t len,
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
        (SVN_ERR_MALFORMED_XML, 0, NULL, svn_parser->pool, 
         "%s at line %d",
         XML_ErrorString (XML_GetErrorCode (svn_parser->parser)),
         XML_GetCurrentLineNumber (svn_parser->parser));
      
      /* Kill all parsers and return the expat error */
      svn_xml_free_parser (svn_parser);
      return err;
    }

  /* Did an an error occur somewhere *inside* the expat callbacks? */
  if (svn_parser->error)
    {
      err = svn_parser->error;
      svn_xml_free_parser (svn_parser);
      return err;
    }
  
  return SVN_NO_ERROR;
}



/* The way to officially bail out of xml parsing.
   Store ERROR in SVN_PARSER and set all expat callbacks to NULL. */
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

/* See svn_xml.h for details. */
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

svn_error_t *
svn_xml_write_header (apr_file_t *file, apr_pool_t *pool)
{
  apr_status_t apr_err;
  const char header[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

  apr_err = apr_full_write (file, header, (sizeof (header) - 1), NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool,
                             "svn_xml_write_header: file write error.");
  
  return SVN_NO_ERROR;
}



/*** Creating attribute hashes. ***/

apr_hash_t *
svn_xml_make_att_hash (const char **atts, apr_pool_t *pool)
{
  apr_hash_t *ht = apr_make_hash (pool);
  const char *key;

  if (atts)
    for (key = *atts; key; key = *(++atts))
      {
        const char *val = *(++atts);
        assert (key != NULL);
        
        /* kff todo: should we also insist that val be non-null here? 
           Probably. */

        apr_hash_set (ht, key, strlen (key),
                      val ? svn_string_create (val, pool) : NULL);
      }

  return ht;
}


apr_hash_t *
svn_xml_make_att_hash_overlaying (const char **atts,
                                  va_list ap,
                                  apr_pool_t *pool)
{
  apr_hash_t *ht = svn_xml_make_att_hash (atts, pool);
  const char *key;

  if (ap)
    while ((key = va_arg (ap, char *)) != NULL)
      {
        svn_string_t *val = va_arg (ap, svn_string_t *);
        apr_hash_set (ht, key, strlen (key), val);
      }
  
  return ht;
}



/*** Writing out tags. ***/


svn_error_t *
svn_xml_write_tag_hash (apr_file_t *file,
                        apr_pool_t *pool,
                        const int tagtype,
                        const char *tagname,
                        apr_hash_t *attributes)
{
  apr_status_t status;
  apr_size_t bytes_written;
  svn_string_t *xmlstring;
  apr_hash_index_t *hi;

  apr_pool_t *subpool = svn_pool_create (pool, NULL);

  xmlstring = svn_string_create ("<", subpool);

  if (tagtype == svn_xml__close_tag)
    svn_string_appendcstr (xmlstring, "/", subpool);

  svn_string_appendcstr (xmlstring, tagname, subpool);

  for (hi = apr_hash_first (attributes); hi; hi = apr_hash_next (hi))
    {
      const char *key;
      size_t keylen;
      svn_string_t *val;
      
      apr_hash_this (hi, (const void **) &key, &keylen, (void **) &val);
      assert (val != NULL);
      
      svn_string_appendcstr (xmlstring, "\n   ", subpool);
      svn_string_appendcstr (xmlstring, key, subpool);
      svn_string_appendcstr (xmlstring, "=\"", subpool);
      svn_string_appendstr  (xmlstring, escape_string (val, subpool), subpool);
      svn_string_appendcstr (xmlstring, "\"", subpool);
    }

  if (tagtype == svn_xml__self_close_tag)
    svn_string_appendcstr (xmlstring, "/", subpool);

  svn_string_appendcstr (xmlstring, ">\n", subpool);

  /* Do the write */
  status = apr_full_write (file, xmlstring->data, xmlstring->len,
                           &bytes_written);
  if (status)
    return svn_error_create (status, 0, NULL, subpool,
                             "svn_xml_write_tag:  file write error.");

  apr_destroy_pool (subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_xml_write_tag_v (apr_file_t *file,
                     apr_pool_t *pool,
                     const int tagtype,
                     const char *tagname,
                     va_list ap)
{
  apr_hash_t *ht = svn_xml_make_att_hash_overlaying (NULL, ap, pool);
  return svn_xml_write_tag_hash (file, pool, tagtype, tagname, ht);
}



svn_error_t *
svn_xml_write_tag (apr_file_t *file,
                   apr_pool_t *pool,
                   const int tagtype,
                   const char *tagname,
                   ...)
{
  svn_error_t *err;
  va_list ap;

  va_start (ap, tagname);
  err = svn_xml_write_tag_v (file, pool, tagtype, tagname, ap);
  va_end (ap);

  return err;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
