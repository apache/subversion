/*
 * dav_svn.h: types, functions, macros for the DAV/SVN Apache module
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
 * software developed by CollabNet (http://www.Collab.Net/)."
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


#ifndef DAV_SVN_H
#define DAV_SVN_H

#include <httpd.h>
#include <util_xml.h>
#include <apr_tables.h>
#include <mod_dav.h>

#include "svn_error.h"
#include "svn_fs.h"


/* dav_svn_repos
 *
 * Record information about the repository that a resource belongs to.
 * This structure will be shared between multiple resources so that we
 * can optimized our FS access.
 *
 * Note that we do not refcount this structure. Presumably, we will need
 * it throughout the life of the request. Therefore, we can just leave it
 * for the request pool to cleanup/close.
 *
 * Also, note that it is possible that two resources may have distinct
 * dav_svn_repos structures, yet refer to the same repository. This is
 * allowed by the SVN FS interface.
 *
 * ### should we attempt to merge them when we detect this situation in
 * ### places like is_same_resource, is_parent_resource, or copy/move?
 * ### I say yes: the FS will certainly have an easier time if there is
 * ### only a single FS open; otherwise, it will have to work a bit harder
 * ### to keep the things in sync.
 */
typedef struct {
  apr_pool_t *pool;     /* request_rec -> pool */

  /* Remember the root URL path of this repository (just a path; no
     scheme, host, or port).

     Example: the URI is "http://host/repos/file", this will be "/repos".
  */
  const char *root_uri;

  /* This records the filesystem path to the SVN FS */
  const char *fs_path;

  /* the open repository */
  svn_fs_t *fs;

  /* NOTE: root_rev and root_dir may be 0/NULL if we don't open the root
     of the repository (e.g. we're dealing with activity resources) */
  /* ### these fields may make better sense elsewhere; a repository may
     ### need two roots open for some operations(?) */

  /* what revision did we open for the root? */
  svn_revnum_t root_rev;

  /* the root of the revision tree */
  svn_fs_dir_t *root_dir;
  
} dav_svn_repos;

/* internal structure to hold information about this resource */
struct dav_resource_private {
  apr_pool_t *pool;     /* request_rec -> pool */

  /* Path from the SVN repository root to this resource. This value has
     a leading slash. It will never have a trailing slash, even if the
     resource represents a collection.

     For example: URI is http://host/repos/file -- path will be "/file".

     Note that the SVN FS does not like absolute paths, so we
     generally skip the first char when talking with the FS.
  */
  svn_string_t *path;

  /* resource-type-specific data */
  const char *object_name;      /* ### not really defined right now */

  /* for REGULAR resources: an open node for the revision */
  svn_fs_node_t *node;

  dav_svn_repos *repos;
};


void dav_svn_gather_propsets(apr_array_header_t *uris);
int dav_svn_find_liveprop(request_rec *r, const char *ns_uri, const char *name,
                          const dav_hooks_liveprop **hooks);
void dav_svn_insert_all_liveprops(request_rec *r, const dav_resource *resource,
                                  int insvalue, ap_text_header *phdr);
void dav_svn_register_uris(apr_pool_t *p);

/* our hooks structures; these are gathered into a dav_provider */
extern const dav_hooks_repository dav_svn_hooks_repos;
extern const dav_hooks_propdb dav_svn_hooks_propdb;
extern const dav_hooks_liveprop dav_svn_hooks_liveprop;
extern const dav_hooks_vsn dav_svn_hooks_vsn;

/* for the repository referred to by this request, where is the SVN FS? */
const char *dav_svn_get_fs_path(request_rec *r);

/* SPECIAL URI

   SVN needs to create many types of "pseudo resources" -- resources
   that don't correspond to the users' files/directories in the
   repository. Specifically, these are:

   - working resources
   - activities
   - version resources
   - version history resources

   Each of these will be placed under a portion of the URL namespace
   that defines the SVN repository. For example, let's say the user
   has configured an SVN repository at http://host/svn/repos. The
   special resources could be configured to live at .../$svn/ under
   that repository. Thus, an activity might be located at
   http://host/svn/repos/$svn/act/1234.

   The special URI is configurable on a per-server basis and defaults
   to "$svn".

   NOTE: the special URI is RELATIVE to the "root" of the
   repository. The root is generally available only to
   dav_svn_get_resource(). This is okay, however, because we can cache
   the root_dir when the resource structure is built.
*/

/* Return the special URI to be used for this resource. */
const char *dav_svn_get_special_uri(request_rec *r);


/* convert an svn_error_t into a dav_error, possibly pushing a message. use
   the provided HTTP status for the DAV errors */
dav_error * dav_svn_convert_err(const svn_error_t *serr, int status,
                                const char *message);

#endif /* DAV_SVN_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
