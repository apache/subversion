/*  svn_base64.h:  base64 encoding and decoding functions.
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#ifndef SVN_BASE64_H
#define SVN_BASE64_H

#include "svn_io.h"

/* Prepare to encode binary data in base64 format.  On return, ENCODE
   and ENCODE_BATON will be set to a handler which encodes input and
   sends the encoded data to OUTPUT and OUTPUT_BATON.  */
void svn_base64_encode (svn_write_fn_t *output, void *output_baton,
			apr_pool_t *pool,
			svn_write_fn_t **encode, void **encode_baton);

/* Prepare to decode base64-encoded data.  On return, DECODE and
   DECODE_BATON will be set to a handler which decodes input and sends
   the decoded data to OUTPUT and OUTPUT_BATON.  */
void svn_base64_decode (svn_write_fn_t *output, void *output_baton,
			apr_pool_t *pool,
			svn_write_fn_t **decode, void **decode_baton);


/* Simpler interfaces for encoding and decoding base64 data assuming
   we have all of it present at once.  The returned string will be
   allocated from POOL.  */
svn_string_t *svn_base64_encode_string (svn_string_t *str, apr_pool_t *pool);
svn_string_t *svn_base64_decode_string (svn_string_t *str, apr_pool_t *pool);


#endif /* SVN_BASE64_H */



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
