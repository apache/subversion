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

/* Return a writable generic stream which will encode binary data in
   base64 format and write the encoded data to OUTPUT.  */
svn_stream_t *svn_base64_encode (svn_stream_t *output, apr_pool_t *pool);

/* Return a writable generic stream which will decode base64-encoded
   data and write the decoded data to OUTPUT.  */
svn_stream_t *svn_base64_decode (svn_stream_t *output, apr_pool_t *pool);


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
