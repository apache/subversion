/*
 * svn_wc.h :  public interface for the Subversion Working Copy Library
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
 * Requires:  
 *            A working copy
 * 
 * Provides: 
 *            - Ability to manipulate working copy's versioned data.
 *            - Ability to manipulate working copy's administrative files.
 *
 * Used By:   
 *            Clients.
 */

#ifndef SVN_WC_H
#define SVN_WC_H

#include <svn_types.h>



/* Functions taking an argument (apr_array_header_t *)PATHS are taking
 * an array of (svn_string_t *) file and/or directory names.  
 */

ap_status_t svn_wc_rename (svn_string_t *src, svn_string_t *dst);
ap_status_t svn_wc_copy   (svn_string_t *src, svn_string_t *dst);
ap_status_t svn_wc_add    (apr_array_header_t *paths);
ap_status_t svn_wc_delete (apr_array_header_t *paths);

svn_skelta_t svn_wc_make_skelta (apr_array_header_t *paths);

/* Turn SKELTA into a full delta. */
svn_delta_t svn_wc_fill_skelta (svn_skelta_t *skelta);

/* Update working copy to reflect the changes in DELTA. */
svn_boolean_t svn_wc_apply_delta (svn_delta_t *delta);

/* Return local value of PROPNAME for the file or directory PATH. */
svn_prop_t *svn_wc_get_node_prop (svn_string_t *path,
                                  svn_string_t *propname);

/* Return local value of PROPNAME for the directory entry PATH. */
svn_prop_t *svn_wc_get_dirent_prop (svn_string_t *path,
                                    svn_string_t *propname);

/* Return all properties (names and values) of file or directory PATH. */
ap_hash_t *svn_wc_get_node_proplist (svn_string_t *path);

/* Return all properties (names and values) of directory entry PATH. */
ap_hash_t *svn_wc_get_dirent_proplist (svn_string_t *path);

/* Return all property names of file or directory PATH. */
ap_hash_t *svn_wc_get_node_propnames (path);

/* Return all property names of directory entry PATH. */
ap_hash_t *svn_wc_get_dirent_propnames (path);

#endif  /* SVN_WC_H */

/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */

