/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file mod_dav_svn.h
 * @brief Subversion's backend for Apache's mod_dav module
 */


#ifndef MOD_DAV_SVN_H
#define MOD_DAV_SVN_H

#include <httpd.h>
#include <mod_dav.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Given an apache request R, a URI, and a ROOT_PATH to the svn
   location block, process URI and return many things, allocated in
   r->pool:

   * CLEANED_URI:     The uri with duplicate and trailing slashes removed.

   * TRAILING_SLASH:  Whether the uri had a trailing slash on it.

   Three special substrings of the uri are returned for convenience:

   * REPOS_NAME:      The single path component that is the directory
                      which contains the repository.

   * RELATIVE_PATH:   The remaining imaginary path components.

   * REPOS_PATH:      The actual path within the repository filesystem, or
                      NULL if no part of the uri refers to a path in
                      the repository (e.g. "!svn/vcc/default" or
                      "!svn/bln/25").


   For example, consider the uri

       /svn/repos/proj1/!svn/blah/13//A/B/alpha

   In the SVNPath case, this function would receive a ROOT_PATH of
   '/svn/repos/proj1', and in the SVNParentPath case would receive a
   ROOT_PATH of '/svn/repos'.  But either way, we would get back:

     * CLEANED_URI:    /svn/repos/proj1/!svn/blah/13/A/B/alpha
     * REPOS_NAME:     proj1
     * RELATIVE_PATH:  /!svn/blah/13/A/B/alpha
     * REPOS_PATH:     A/B/alpha
     * TRAILING_SLASH: FALSE
*/
AP_MODULE_DECLARE(dav_error *) dav_svn_split_uri(request_rec *r,
                                                 const char *uri,
                                                 const char *root_path,
                                                 const char **cleaned_uri,
                                                 int *trailing_slash,
                                                 const char **repos_name,
                                                 const char **relative_path,
                                                 const char **repos_path);


/* Given an apache request R and a ROOT_PATH to the svn location
   block sets *REPOS_PATH to the path of the repository on disk.
*/
AP_MODULE_DECLARE(dav_error *) dav_svn_get_repos_path(request_rec *r,
                                                      const char *root_path,
                                                      const char **repos_path);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MOD_DAV_SVN_H */
