/*
 * delta_parse.h: create an svn_delta_t from an XML stream
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
 */

/* ==================================================================== */


/*
  This library contains callbacks to use with the "expat-lite" XML
  parser.  The callbacks produce an svn_delta_t structure from a
  stream containing Subversion's XML delta representation.

  To use this library, see "deltaparse-test.c" in tests/.
  
  Essentially, one must 
  
  * create an XML_Parser
  * register the callbacks (below) with the parser
  * call XML_Parse() on a bytestream
  
*/

#include <stdio.h>
#include <string.h>
#include "svn_types.h"
#include "svn_string.h"
#include "xmlparse.h"




/* kff todo: all these constructors need corresponding destructors. */

/* kff todo: path, version, new? */
svn_ancestor_t * svn_delta_ancestor_create (apr_pool_t *pool);


/* kff todo: kind, ancestor, etc? */
svn_edit_content_t * svn_delta_edit_content_create (apr_pool_t *pool);


/* kff todo: action name, content? */
svn_edit_t * svn_delta_edit_create (apr_pool_t *pool);


/* kff todo: perhaps should take the context args, source_root &
   base_version, and set them in the new delta */
svn_delta_t * svn_delta_create (apr_pool_t *pool);


/* kff todo: again, take the callbacks, and set them?  Need a policy
   w.r.t. constructors such as this. */
svn_delta_digger_t * svn_delta_digger_create (apr_pool_t *pool);


/* Walk down a delta, append object to the end of it, whereever that
   may be.  :)   Cast the object when we attach it.  */
svn_error_t * svn_append_to_delta (svn_delta_t *d,
                                   void *object,
                                   svn_XML_elt_t elt_kind);




/* Callback:  called whenever we find a new tag (open paren).

    The *name argument contains the name of the tag,
    and the **atts list is a dumb list of name/value pairs, all
    null-terminated Cstrings, and ending with an extra final NULL.

*/  
      
void svn_xml_startElement(void *userData, const char *name, const char **atts);


/*  Callback:  called whenever we find a close tag (close paren) */

void svn_xml_endElement(void *userData, const char *name);


/* Callback: called whenever we find data within a tag.  
   (Of course, we only care about data within the "text-delta" tag.)  */

void svn_xml_DataHandler(void *userData, const char *data, int len);




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
