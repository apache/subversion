/*  svn_xml.h:  xml code shared by various Subversion libraries.
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
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#ifndef SVN_XML_H
#define SVN_XML_H

#include "xmlparse.h"
#include "svn_error.h"


/* Used as an argument to svn_xml_write_tag() */
enum {
  svn_xml__open_tag = 1,         /* write <tag>   */
  svn_xml__close_tag,            /* write </tag>  */
  svn_xml__self_close_tag        /* write <tag/>  */
};


/*---------------------------------------------------------------*/

/*** Generalized Subversion XML Parsing ***/

/* A generalized Subversion XML parser object */
typedef struct svn_xml_parser_t
{
  XML_Parser parser;         /* the expat parser */
  svn_error_t *error;        /* if non-NULL, an error happened while parsing */
  apr_pool_t *pool;          /* where this object is allocated, so we
                                can free it easily */

} svn_xml_parser_t;


/* Create a general Subversion XML parser */
svn_xml_parser_t *svn_xml_make_parser (void *userData,
                                       XML_StartElementHandler  start_handler,
                                       XML_EndElementHandler    end_handler,
                                       XML_CharacterDataHandler data_handler,
                                       apr_pool_t *pool);


/* Free a general Subversion XML parser */
void svn_xml_free_parser (svn_xml_parser_t *svn_parser);


/* Push LEN bytes of xml data in BUF at SVN_PARSER.  If this is the
   final push, IS_FINAL must be set.  */
svn_error_t *svn_xml_parse (svn_xml_parser_t *parser,
                            const char *buf,
                            apr_ssize_t len,
                            svn_boolean_t is_final);



/* The way to officially bail out of xml parsing:
   Store ERROR in SVN_PARSER and set all expat callbacks to NULL. */
void signal_expat_bailout (svn_error_t *error, svn_xml_parser_t *svn_parser);





/*** Helpers for dealing with the data Expat gives us. ***/

/* Return the value associated with NAME in expat attribute array ATTS,
 * else return NULL.  (There could never be a NULL attribute value in
 * the XML, although the empty string is possible.)
 * 
 * ATTS is an array of c-strings: even-numbered indexes are names,
 * odd-numbers hold values.  If all is right, it should end on an
 * even-numbered index pointing to NULL.
 */
const char *svn_xml_get_attr_value (const char *name, const char **atts);





/*** Printing XML ***/

/* Print an XML tag named TAGNAME into FILE.  Varargs are used to
   specify a NULL-terminated list of {const char *attribute, const
   char *value}.  TAGTYPE must be one of 

              svn_xml__open_tag         ... <tagname>
              svn_xml__close_tag        ... </tagname>
              svn_xml__self_close_tag   ... <tagname/>

   FILE is assumed to be already open for writing.
*/
svn_error_t * svn_xml_write_tag (apr_file_t *file,
                                 apr_pool_t *pool,
                                 int tagtype,
                                 const char *tagname,
                                 ...);









#endif /* SVN_XML_H */
