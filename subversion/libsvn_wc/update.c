/*
 * update.c :  routines for update and checkout
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



#include <stdio.h>       /* for sprintf() */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"



svn_error_t *
svn_update_data_handler (svn_digger_t *diggy, const char *data, int len)
{
  /* kff todo */
}


svn_error_t *
svn_update_dir_handler (svn_digger_t *diggy, const char *data, int len)
{
  /* kff todo */
}



/* Do an update/checkout, with src delta streaming from SRC, to DST.
 * 
 * SRC must be already opened.
 * 
 * If DST exists and is a working copy, or a subtree of a working
 * copy, then it is massaged into the updated state.
 *
 * If DST does not exist, a working copy is created there.
 *
 * If DST exists but is not a working copy, return error.
 *
 * (And if DST is NULL, the above rules apply with DST set to the top
 * directory mentioned in the delta.) 
 */
svn_error_t *
update (ap_file_t *src, svn_string_t *dst, apr_pool_t *pool)
{
  char buf[BUFSIZ];
  int done;
  svn_delta_digger_t diggy;

  XML_Parser parsimonious = svn_delta_make_xml_parser (&diggy);

  diggy.pool = pool;
  diggy.data_handler = svn_update_data_handler;
  diggy.dir_handler = svn_update_dir_handler;

  do {
    /* Grab some stream. */
    err = apr_full_read (src, buf, sizeof (buf), &len);
    done = (len < sizeof (buf));
    
    /* Parse the chunk of stream. */
    if (XML_Parse (parser, buf, len, done) != 0)
    {
      char *message
        = apr_psprintf (pool, 
                        "%s at line %d",
                        XML_ErrorString (XML_GetErrorCode (parser)),
                        XML_GetCurrentLineNumber (parser));

      svn_error_t *err
        = svn_create_error (SVN_ERR_MALFORMED_XML, 0, message, NULL, pool);

      return err;
    }
  } while (! done);

  XML_ParserFree (parser);
  return 0;
}






/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

