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


#define SVN_XML_NAMESPACE "http://subversion.tigris.org/xmlns/"

/* Used as type argument to svn_xml_write_tag() */
enum {
  svn_xml__open_tag,         /* write <tag ...>                    */
  svn_xml__close_tag,        /* write </tag>                       */
  svn_xml__self_close_tag    /* write <tag .../>                   */
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


/* Push LEN bytes of xml data in BUF at SVN_PARSER.  

   If this is the final push, IS_FINAL must be set.  

   An error will be returned if there was a syntax problem in the XML,
   or if any of the callbacks set an error using
   svn_xml_signal_bailout().  

   If an error is returned, the svn_xml_parser_t will have been freed
   automatically, so the caller should not call svn_xml_free_parser(). */ 
svn_error_t *svn_xml_parse (svn_xml_parser_t *parser,
                            const char *buf,
                            apr_ssize_t len,
                            svn_boolean_t is_final);



/* The way to officially bail out of xml parsing:
   Store ERROR in SVN_PARSER and set all expat callbacks to NULL. */
void svn_xml_signal_bailout (svn_error_t *error,
                             svn_xml_parser_t *svn_parser);





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



/*** Converting between Expat attribute lists and APR hash tables. ***/


/* Create an attribute hash from va_list AP. 
 * The contents of AP are alternating char *'s and svn_string_t *'s,
 * terminated by a final null falling on an odd index (zero-based).
 */
apr_hash_t *svn_xml_ap_to_hash (va_list ap, apr_pool_t *pool);

/* Create a hash that corresponds to Expat xml attribute list ATTS.
 * The hash's keys will be char *'s, the values svn_string_t *'s.
 *
 * ATTS may be null, in which case you just get an empty hash back
 * (this makes life more convenient for some callers).
 */
apr_hash_t *svn_xml_make_att_hash (const char **atts, apr_pool_t *pool);


/* Like svn_xml_make_att_hash(), but takes a hash and preserves any
   key/value pairs already in it. */
void svn_xml_hash_atts_preserving (const char **atts,
                                   apr_hash_t *ht,
                                   apr_pool_t *pool);

/* Like svn_xml_make_att_hash(), but takes a hash and overwrites
   key/value pairs already in it that also appear in ATTS. */
void svn_xml_hash_atts_overlaying (const char **atts,
                                   apr_hash_t *ht,
                                   apr_pool_t *pool);



/*** Printing XML ***/

/* Fully-formed XML files should start out with a header, something
 * like 
 *         <?xml version="1.0" encoding="utf-8"?>
 * 
 * If you're writing such a file, call this before you make any calls
 * to svn_xml_write_tag().
 */
svn_error_t *svn_xml_write_header (apr_file_t *file, apr_pool_t *pool);


/* Print an XML tag named TAGNAME into FILE.  Varargs are used to
   specify a NULL-terminated list of alternating const char *Key
   and svn_string_t *Val.

   TAGTYPE must be one of 

              svn_xml__open_tag         ... <tagname>
              svn_xml__close_tag        ... </tagname>
              svn_xml__self_close_tag   ... <tagname/>

   FILE is assumed to be already open for writing.
*/
svn_error_t *svn_xml_write_tag (apr_file_t *file,
                                apr_pool_t *pool,
                                int tagtype,
                                const char *tagname,
                                ...);


/* Like svn_xml_write_tag, but takes a va_list instead of being variadic. */
svn_error_t *svn_xml_write_tag_v (apr_file_t *file,
                                  apr_pool_t *pool,
                                  const int tagtype,
                                  const char *tagname,
                                  va_list ap);


/* Like svn_xml_write_tag, but takes a hash table of attributes. 
 *
 * You might ask, why not just provide svn_xml_write_tag_atts()?
 *
 * The reason is that a hash table is the most natural interface to an
 * attribute list; the fact that Expat uses char **atts instead is
 * certainly a defensible implementation decision, but since we'd have
 * to have special code to support such lists throughout Subversion
 * anyway, we might as well write that code for the natural interface
 * (hashes) and then convert in the few cases where conversion is
 * needed.  Someday it might even be nice to change expat-lite to work
 * with apr hashes.
 *
 * See conversion functions svn_xml_make_att_hash() and
 * svn_xml_make_att_hash_overlaying().  Callers should use those to
 * convert Expat attr lists into hashes when necessary.
 */
svn_error_t *svn_xml_write_tag_hash (apr_file_t *file,
                                     apr_pool_t *pool,
                                     const int tagtype,
                                     const char *tagname,
                                     apr_hash_t *attributes);



/*** Producing xml output. ***/

/* Return an editor that will output xml to OUTFILE (which is already
   opened, and which the caller is also responsible for closing). */
svn_error_t *
svn_xml_get_editor (apr_file_t *outfile,
                    const svn_delta_edit_fns_t **editor,
                    void **edit_baton,
                    apr_pool_t *pool);


#endif /* SVN_XML_H */
