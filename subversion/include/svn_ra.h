/*
 * svn_ra.h :  structures related to repository access
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
 *
 * Portions of this software were originally written by Greg Stein as a
 * sourceXchange project sponsored by SapphireCreek.
 */



#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>
#include "svn_error.h"
#include "svn_delta.h"


typedef struct svn_ra_session_t svn_ra_session_t;

svn_error_t * svn_ra_open (svn_ra_session_t **p_ras,
                           const char *repository,
                           apr_pool_t *pool);

void svn_ra_close (svn_ra_session_t *ras);

svn_error_t * svn_ra_update (svn_ra_session_t *ras,
                             const char *start_at,
                             int recurse,
                             svn_delta_walk_t *walker,
                             void *walk_baton,
                             void *dir_baton,
                             apr_pool_t *pool);

svn_error_t * svn_ra_checkout (svn_ra_session_t *ras,
                               const char *start_at,
                               int recurse,
                               svn_delta_walk_t *walker,
                               void *walk_baton,
                               void *dir_baton,
                               apr_pool_t *pool);

#endif  /* SVN_RA_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
