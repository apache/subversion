/*  svn_xml.h:  xml code shared by various Subversion libraries.
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



#ifndef SVN_XML_H
#define SVN_XML_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>

#ifdef SVN_HAVE_OLD_EXPAT
#include "xmlparse.h"
#else
#include "expat.h"
#endif

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define SVN_XML_NAMESPACE "svn:"

/* Used as style argument to svn_xml_make_open_tag() and friends. */
enum svn_xml_open_tag_style {
  svn_xml_normal = 1,           /* <tag ...> */
  svn_xml_protect_pcdata,       /* <tag ...>, no cosmetic newline */
  svn_xml_self_closing          /* <tag .../>  */
};


/* Create or append in *OUTSTR an xml-escaped version of STRING,
 * suitable for output as character data or as an attribute value.
 * If *OUTSTR is NULL, store a new stringbuf, else append to the
 * existing stringbuf there.
 */
void svn_xml_escape_stringbuf (svn_stringbuf_t **outstr,
                               const svn_stringbuf_t *string,
                               apr_pool_t *pool);

/* Same as `svn_xml_escape_stringbuf', but STRING is an svn_string_t.
 */
void svn_xml_escape_string (svn_stringbuf_t **outstr,
                            const svn_string_t *string,
                            apr_pool_t *pool);

/* Same as `svn_xml_escape_stringbuf', but STRING is a null-terminated
 * C string.
 */
void svn_xml_escape_nts (svn_stringbuf_t **outstr,
                         const char *string,
                         apr_pool_t *pool);


/* Create or append in *OUTSTR the unescaped version of the
 * xml-escaped string STRING.  If *OUTSTR is NULL, store a new
 * stringbuf, else append to the existing stringbuf there.
 *
 * NOTE:  This function recognizes only the following XML escapes:
 *
 *    &amp;    - &
 *    &apos;   - '
 *    &gt;     - >
 *    &lt;     - <
 *    &quot;   - "
 */
void svn_xml_unescape_stringbuf (svn_stringbuf_t **outstr,
                                 const svn_stringbuf_t *string,
                                 apr_pool_t *pool);

/* Same as `svn_xml_unescape_stringbuf', but STRING is an svn_string_t.
 */
void svn_xml_unescape_string (svn_stringbuf_t **outstr,
                              const svn_string_t *string,
                              apr_pool_t *pool);

/* Same as `svn_xml_unescape_stringbuf', but STRING is a
 * null-terminated C string.
 */
void svn_xml_unescape_nts (svn_stringbuf_t **outstr,
                           const char *string,
                           apr_pool_t *pool);



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
                            apr_size_t len,
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
 * The contents of AP are alternating char *'s and svn_stringbuf_t *'s,
 * terminated by a final null falling on an odd index (zero-based).
 */
apr_hash_t *svn_xml_ap_to_hash (va_list ap, apr_pool_t *pool);

/* Create a hash that corresponds to Expat xml attribute list ATTS.
 * The hash's keys will be char *'s, the values svn_stringbuf_t *'s.
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

/* Fully-formed XML documents should start out with a header,
   something like 
           <?xml version="1.0" encoding="utf-8"?>
   
   This function returns such a header.  *STR must either be NULL, in
   which case a new string is created, or it must point to an existing
   string to be appended to.  */
void svn_xml_make_header (svn_stringbuf_t **str, apr_pool_t *pool);


/* Store a new xml tag TAGNAME in *STR.

   If STR is NULL, allocate *STR in POOL; else append the new tag to
   *STR, allocating in STR's pool

   Take the tag's attributes from varargs, a NULL-terminated list of
   alternating const char *Key and svn_stringbuf_t *Val.  Do
   xml-escaping on each Val.

   STYLE is one of the enumerated styles in svn_xml_open_tag_style. */
void svn_xml_make_open_tag (svn_stringbuf_t **str,
			    apr_pool_t *pool,
			    enum svn_xml_open_tag_style style,
			    const char *tagname,
			    ...);


/* Like svn_xml_make_open_tag, but takes a va_list instead of being
   variadic. */
void svn_xml_make_open_tag_v (svn_stringbuf_t **str,
			      apr_pool_t *pool,
			      enum svn_xml_open_tag_style style,
			      const char *tagname,
			      va_list ap);


/* Like svn_xml_make_tag, but takes a hash table of attributes. 
 *
 * You might ask, why not just provide svn_xml_make_tag_atts()?
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
void svn_xml_make_open_tag_hash (svn_stringbuf_t **str,
				 apr_pool_t *pool,
				 enum svn_xml_open_tag_style style,
				 const char *tagname,
				 apr_hash_t *attributes);


/* Makes a close tag.  */
void svn_xml_make_close_tag (svn_stringbuf_t **str,
			     apr_pool_t *pool,
			     const char *tagname);



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_XML_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: 
 */
