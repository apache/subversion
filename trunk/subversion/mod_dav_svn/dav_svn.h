/*
 * dav_svn.h: types, functions, macros for the DAV/SVN Apache module
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#ifndef DAV_SVN_H
#define DAV_SVN_H

#include <httpd.h>
#include <apr_tables.h>
#include <apr_xml.h>
#include <mod_dav.h>

#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"

#define DAV_SVN_DEFAULT_VCC_NAME        "default"

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
  const char *root_path;

  /* Remember an absolute URL for constructing other URLs. In the above
     example, this would be "http://host" (note: no trailing slash)
  */
  const char *base_url;

  /* Remember the special URI component for this repository */
  const char *special_uri;

  /* This records the filesystem path to the SVN FS */
  const char *fs_path;

  /* the open repository */
  svn_repos_t *repos;

  /* a cached copy of REPOS->fs above. */
  svn_fs_t *fs;

  /* the user operating against this repository */
  const char *username;

} dav_svn_repos;


/*
** dav_svn_private_restype: identifiers for our different private resources
**
** There are some resources within mod_dav_svn that are "privately defined".
** This isn't so much to prevent other people from knowing what they are,
** but merely that mod_dav doesn't have a standard name for them.
*/
enum dav_svn_private_restype {
  DAV_SVN_RESTYPE_UNSET,

  DAV_SVN_RESTYPE_ROOT_COLLECTION,      /* .../$svn/     */
  DAV_SVN_RESTYPE_VER_COLLECTION,       /* .../$svn/ver/ */
  DAV_SVN_RESTYPE_HIS_COLLECTION,       /* .../$svn/his/ */
  DAV_SVN_RESTYPE_WRK_COLLECTION,       /* .../$svn/wrk/ */
  DAV_SVN_RESTYPE_ACT_COLLECTION,       /* .../$svn/act/ */
  DAV_SVN_RESTYPE_VCC_COLLECTION,       /* .../$svn/vcc/ */
  DAV_SVN_RESTYPE_BC_COLLECTION,        /* .../$svn/bc/  */
  DAV_SVN_RESTYPE_BLN_COLLECTION,       /* .../$svn/bln/ */
  DAV_SVN_RESTYPE_WBL_COLLECTION,       /* .../$svn/wbl/ */
  DAV_SVN_RESTYPE_VCC                   /* .../$svn/vcc/NAME */
};


/* store info about a root in a repository */
typedef struct {
  /* If a root within the FS has been opened, the value is stored here.
     Otherwise, this field is NULL. */
  svn_fs_root_t *root;

  /* If the root has been opened, and it was opened for a specific revision,
     then it is contained in REV. If the root is unopened or corresponds to
     a transaction, then REV will be SVN_INVALID_REVNUM. */
  svn_revnum_t rev;

  /* If this resource is an activity or part of an activity, this specifies
     the ID of that activity. It may not (yet) correspond to a transaction
     in the FS.

     WORKING and ACTIVITY resources use this field.
  */
  const char *activity_id;

  /* If the root is part of a transaction, this contains the FS's tranaction
     name. It may be NULL if this root corresponds to a specific revision.
     It may also be NULL if we have not opened the root yet.

     WORKING and ACTIVITY resources use this field.
  */
  const char *txn_name;

  /* If the root is part of a transaction, this contains the FS's transaction
     handle. It may be NULL if this root corresponds to a specific revision.
     It may also be NULL if we have not opened the transaction yet.

     WORKING resources use this field.
  */
  svn_fs_txn_t *txn;

} dav_svn_root;

/* internal structure to hold information about this resource */
struct dav_resource_private {
  /* Path from the SVN repository root to this resource. This value has
     a leading slash. It will never have a trailing slash, even if the
     resource represents a collection.

     For example: URI is http://host/repos/file -- path will be "/file".

     NOTE: this path is from the URI and does NOT necessarily correspond
           to a path within the FS repository.
  */
  svn_stringbuf_t *uri_path;

  /* The FS repository path to this resource, with a leading "/". Note
     that this is "/" the root. This value will be NULL for resources
     that have no corresponding resource within the repository (such as
     the PRIVATE resources, Baselines, or Working Baselines). */
  const char *repos_path;

  /* the FS repository this resource is associated with */
  dav_svn_repos *repos;

  /* what FS root this resource occurs within */
  dav_svn_root root;

  /* for VERSION resources: the node ID. may be NULL if the resource was
     fetched via a Baseline Collection (so use root.rev and repos_path). if
     the VERSION refers to a Baseline (.baselined==1), then node_id and
     repos_path will be NULL. */
  const svn_fs_id_t *node_id;
  const char *node_id_str;

  /* for PRIVATE resources: the private resource type */
  enum dav_svn_private_restype restype;

  /* ### hack to deal with the Content-Type header on a PUT */
  int is_svndiff;

  /* ### record the base for computing a delta during a GET */
  const char *delta_base;
};


/*
  For a given resource, return the path that should be used when talking
  to the FS. If a NODE_ID is present, then we must have opened an ID root,
  and that NODE_ID should be used. Otherwise, we opened a revision or txn
  root and should use a normal REPOS_PATH.
*/
#define DAV_SVN_REPOS_PATH(res) ((res)->info->node_id_str != NULL \
                                 ? (res)->info->node_id_str \
                                 : (res)->info->repos_path)

/*
  LIVE PROPERTY HOOKS

  These are standard hooks defined by mod_dav. We implement them to expose
  various live properties on the resources under our control.

  gather_propsets: appends URIs into the array; the property set URIs are
                   used to specify which sets of custom properties we
                   define/expose.
  find_liveprop: given a namespace and name, return the hooks for the
                 provider who defines that property.
  insert_all_liveprops: for a given resource, insert all of the live
                        properties defined on that resource. The properties
                        are inserted according to the WHAT parameter.
*/
void dav_svn_gather_propsets(apr_array_header_t *uris);
int dav_svn_find_liveprop(const dav_resource *resource,
                          const char *ns_uri, const char *name,
                          const dav_hooks_liveprop **hooks);
void dav_svn_insert_all_liveprops(request_rec *r, const dav_resource *resource,
                                  dav_prop_insert what, apr_text_header *phdr);

/* register our live property URIs with mod_dav. */
void dav_svn_register_uris(apr_pool_t *p);

/* generate an ETag for the given resource and return it. */
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
const char *dav_svn_get_txn(const dav_svn_repos *repos,
                            const char *activity_id);
dav_error *dav_svn_store_activity(const dav_svn_repos *repos,
                                  const char *activity_id,
                                  const char *txn_name);
dav_error *dav_svn_create_activity(const dav_svn_repos *repos,
                                   const char **ptxn_name,
                                   apr_pool_t *pool);

/*
  Construct a working resource for a given resource.

  The internal information (repository, URL parts, etc) for the new
  resource comes from BASE, the activity to use is specified by
  ACTIVITY_ID, and the name of the transaction is specified by
  TXN_NAME. These will be assembled into a new dav_resource and
  returned.
*/
dav_resource *dav_svn_create_working_resource(const dav_resource *base,
                                              const char *activity_id,
                                              const char *txn_name);


enum dav_svn_build_what {
  DAV_SVN_BUILD_URI_ACT_COLLECTION, /* the collection of activities */
  DAV_SVN_BUILD_URI_BASELINE,   /* a Baseline */
  DAV_SVN_BUILD_URI_BC,         /* a Baseline Collection */
  DAV_SVN_BUILD_URI_PUBLIC,     /* the "public" VCR */
  DAV_SVN_BUILD_URI_VERSION,    /* a Version Resource */
  DAV_SVN_BUILD_URI_VCC         /* a Version Controlled Configuration */
};

/*
  Construct various kinds of URIs.

  REPOS is always required, as all URIs will be built to refer to elements
  within that repository. WHAT specifies the type of URI to build. The
  ADD_HREF flag determines whether the URI is to be wrapped inside of
  <D:href>uri</D:href> elements (for inclusion in a response).

  Different pieces of information are required for the various URI types:

  ACT_COLLECTION: no additional params required
  BASELINE:       REVISION should be specified
  BC:             REVISION should be specified
  PUBLIC:         PATH should be specified with a leading slash
  VERSION:        PATH should be specified as a STABLE_ID ("/ID/PATH")
  VCC:            no additional params required
*/
const char *dav_svn_build_uri(const dav_svn_repos *repos,
                              enum dav_svn_build_what what,
                              svn_revnum_t revision,
                              const char *path,
                              int add_href,
                              apr_pool_t *pool);


/*
** Simple parsing of a URI. This is used for URIs which appear within a
** request body. It enables us to verify and break out the necessary pieces
** to figure out what is being referred to.
**
** ### this is horribly duplicative with the parsing functions in repos.c
** ### for now, this implements only a minor subset of the full range of
** ### URIs which we may need to parse. it also ignores any scheme, host,
** ### and port in the URI and simply assumes it refers to the same server.
*/
typedef struct {
  svn_revnum_t rev;
  const char *repos_path;
  const char *activity_id;
  svn_fs_id_t *node_id;
} dav_svn_uri_info;

svn_error_t *dav_svn_simple_parse_uri(dav_svn_uri_info *info,
                                      const dav_resource *relative,
                                      const char *uri,
                                      apr_pool_t *pool);

/* Generate the HTTP response body for a successful MERGE. */
/* ### more docco */
dav_error * dav_svn__merge_response(ap_filter_t *output,
                                    const dav_svn_repos *repos,
                                    svn_revnum_t new_rev,
                                    apr_xml_elem *prop_elem,
                                    apr_pool_t *pool);

dav_error * dav_svn__update_report(const dav_resource *resource,
				   const apr_xml_doc *doc,
				   apr_text_header *report);

/* ### todo: document this, as soon as understand what the heck it
   does :-).  -kff */   
dav_error * dav_svn__log_report(const dav_resource *resource,
                                const apr_xml_doc *doc,
                                apr_text_header *report);

int dav_svn_find_ns(apr_array_header_t *namespaces, const char *uri);

#endif /* DAV_SVN_H */


/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
