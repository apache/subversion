/*
 * svn_delta.h :  routines for Subversion delta objects
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


#include "svn_types.h"
#include "apr_pools.h"

/* An svn_delta_digger_t is passed as *userData to Expat (and from
 * there to registered callback functions).
 *
 * As the callbacks see various XML elements, they construct
 * digger->delta.  Most elements merely require a new component of the
 * delta to be built and hooked in, with no further action.  Other
 * elements, such as a directory or actual file contents, require
 * special action from the caller.  For example, if the caller is from
 * the working copy library, it might create the directory or the file
 * on disk; or if the caller is from the repository, it might want to
 * start building nodes for a commit.  The digger holds function
 * pointers for such callbacks, and the delta provides context to
 * those callbacks -- e.g., the name of the directory or file to
 * create.
 *
 *    Note ("heads we win, tails we lose"):
 *    =====================================
 *    A digger only stores the head of the delta, even though the
 *    place we hook things onto is the tail.  While it would be
 *    technically more efficient to keep a pointer to tail, it would
 *    also be more error-prone, since it's another thing to keep track
 *    of.  And the maximum chain length of the delta is proportional
 *    to the max directory depth of the tree the delta represents,
 *    since we always snip off any completed portion of the delta
 *    (i.e., every time we encounter a closing tag, we remove what it
 *    closed from the delta).  So cdr'ing down the chain to the end is
 *    not so bad.  Given that deltas usually result in file IO of some
 *    kind, a little pointer chasing should be lost in the noise.
 */
typedef struct svn_delta_digger_t
{
  apr_pool_t *pool;
  
  svn_delta_t *delta;

  /* Caller uses delta context to determine if prop data or text data. */
  svn_error_t (*data_handler) (svn_delta_digger_t *digger,
                               const char *data,
                               int len);

  /* Call handles dirs specially, because might want to create them. */
  svn_error_t (*dir_handler) (svn_delta_digger_t *digger,
                              svn_ancestor_t *ancestor);

  /* Caller optionally decides what to do with unrecognized elements. */
  svn_error_t (*unknown_elt_handler) (svn_delta_digger_t *digger,
                                      const char *name,
                                      const char **atts);

} svn_delta_digger_t;





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
