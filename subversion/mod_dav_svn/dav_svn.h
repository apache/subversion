/*
 * dav_svn.h: types, functions, macros for the DAV/SVN Apache module
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
  svn_fs_node_t *root_dir;
  
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

  /* the repository this resource is associated with */
  dav_svn_repos *repos;

  /* resource-type-specific data */
  const char *object_name;      /* ### not really defined right now */

  /* for REGULAR resources: an open node for the revision */
  svn_fs_node_t *node;

  /* for ACTIVITY resources: the transaction name */
  const char *txn_name;
};


void dav_svn_gather_propsets(apr_array_header_t *uris);
int dav_svn_find_liveprop(const dav_resource *resource,
                          const char *ns_uri, const char *name,
                          const dav_hooks_liveprop **hooks);
void dav_svn_insert_all_liveprops(request_rec *r, const dav_resource *resource,
                                  dav_prop_insert what, ap_text_header *phdr);
void dav_svn_register_uris(apr_pool_t *p);

const char * dav_svn_getetag(const dav_resource *resource);

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

/* activity functions for looking up and storing ACTIVITY->TXN mappings */
const char *dav_svn_get_txn(dav_svn_repos *repos, const char *activity_id);
dav_error *dav_svn_store_activity(dav_svn_repos *repos,
                                  const char *activity_id,
                                  const char *txn_name);

#endif /* DAV_SVN_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
