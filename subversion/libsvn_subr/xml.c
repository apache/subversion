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
#include "svn_xml.h"



/*** Making a parser. ***/

XML_Parser
svn_xml_make_parser (void *userData,
                     XML_StartElementHandler start_handler,
                     XML_EndElementHandler end_handler,
                     XML_CharacterDataHandler data_handler)
{
  XML_Parser parser = XML_ParserCreate (NULL);

  XML_SetUserData (parser, userData);
  XML_SetElementHandler (parser, start_handler, end_handler); 
  XML_SetCharacterDataHandler (parser, data_handler);

  return parser;
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

/* Print an XML tag named TAGNAME into FILE.  Varargs are used to
   specify a NULL-terminated list of {const char *attribute, const
   char *value}.  TAGTYPE must be one of 

              svn_xml__open_tag         ... <tagname>
              svn_xml__close_tag        ... </tagname>
              svn_xml__self_close_tag   ... <tagname/>

   FILE is assumed to be already open for writing.
*/
svn_error_t *
svn_xml_write_tag (apr_file_t *file,
                   apr_pool_t *pool,
                   const int tagtype,
                   const char *tagname,
                   ...)
{
  apr_status_t status;
  apr_size_t bytes_written;
  va_list argptr;
  char *attribute, *value;
  svn_string_t *xmlstring;

  apr_pool_t *subpool = apr_make_sub_pool (pool, NULL);

  xmlstring = svn_string_create ("<", subpool);

  if (tagtype == svn_xml__close_tag)
    svn_string_appendcstr (xmlstring, "/", subpool);

  svn_string_appendcstr (xmlstring, tagname, subpool);

  va_start (argptr, tagname);
  attribute = (char *) argptr;

  while (attribute)
    {
      value = va_arg (argptr, char *);
      
      svn_string_appendcstr (xmlstring, "\n   ", subpool);
      svn_string_appendcstr (xmlstring, attribute, subpool);
      svn_string_appendcstr (xmlstring, "=\"", subpool);
      svn_string_appendcstr (xmlstring, value, subpool);
      svn_string_appendcstr (xmlstring, "\"", subpool);
      
      attribute = va_arg (argptr, char *);
    }
  va_end (argptr);

  if (tagtype == svn_xml__self_close_tag)
    svn_string_appendcstr (xmlstring, "/", subpool);

  svn_string_appendcstr (xmlstring, ">\n", subpool);

  /* Do the write */
  status = apr_full_write (file, xmlstring->data, xmlstring->len,
                           &bytes_written);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "svn_xml_write_tag:  file write error.");

  return SVN_NO_ERROR;
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
