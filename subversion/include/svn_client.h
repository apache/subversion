/*
 * svn_client.h :  public interface for libsvn_client
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



/*** Includes ***/

/* 
 * Requires:  The working copy library and repository access library.
 * Provides:  Broad wrappers around working copy library functionality.
 * Used By:   Client programs.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_CLIENT_H
#define SVN_CLIENT_H

#include <apr_tables.h>
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_string.h"
#include "svn_error.h"



/* ### TODO:  Multiple Targets

    - Up for debate:  an update on multiple targets is *not* atomic.
    Right now, svn_client_update only takes one path.  What's
    debatable is whether this should ever change.  On the one hand,
    it's kind of losing to have the client application loop over
    targets and call svn_client_update() on each one;  each call to
    update initializes a whole new repository session (network
    overhead, etc.)  On the other hand, it's this is a very simple
    implementation, and allows for the possibility that different
    targets may come from different repositories.  */




/*** Various ways of specifying revisions. ***/

enum svn_client_revision_kind {
  svn_client_revision_unspecified,   /* No revision information given. */
  svn_client_revision_number,        /* revision given as number */
  svn_client_revision_date,          /* revision given as date */
  svn_client_revision_commited,      /* .svn/entries:commited-rev */
  svn_client_revision_previous,      /* .svn/entries:commited-rev - 1 :-) */
  svn_client_revision_current,       /* .svn/entries revision */
  svn_client_revision_head           /* repository youngest */
};


/* A revision, specified in one of `svn_client_revision_kind' ways. */
typedef struct svn_client_revision_t {
  enum svn_client_revision_kind kind;
  union {
    svn_revnum_t number;
    apr_time_t date;
  } value;
} svn_client_revision_t;



/*** Authentication stuff -- new M4 Edition  ***/

/*  The new authentication system allows the RA layer to "pull"
    information as needed from libsvn_client.  See svn_ra.h */

/*  A callback function type defined by the top-level client
    application (the user of libsvn_client.)

    If libsvn_client is unable to retrieve certain authorization
    information, it can use this callback; the application will then
    directly query the user with PROMPT and return the answer in INFO,
    allocated in POOL.  BATON is provided at the same time as the
    callback, and HIDE indicates that the user's answer should not be
    displayed on the screen. */
typedef svn_error_t *(*svn_client_prompt_t)
       (char **info,
        const char *prompt,
        svn_boolean_t hide,
        void *baton,
        apr_pool_t *pool);


/* This is a baton that contains information from the calling
   application, passed to libsvn_client to aid in authentication. 

   Applications must build and pass one of these to any routine that
   may require authentication.  */
typedef struct svn_client_auth_baton_t
{
  /* auth info that the app -may- already have, e.g. from argv[] */
  char *username;    
  char *password; 
  
  /* a callback provided by the app layer, for prompting the user */
  svn_client_prompt_t prompt_callback;
  void *prompt_baton;

  /* if it's ok to overwrite wc auth info */
  svn_boolean_t overwrite;

} svn_client_auth_baton_t;


/* This is a structure which stores a filename and a hash of property
   names and values. */

typedef struct svn_client_proplist_item_s
{
  /* The name of the node on which these properties are set. */
  svn_stringbuf_t *node_name;  

  /* A hash of (const char *) property names, and (svn_stringbuf_t *) property
     values. */
  apr_hash_t *prop_hash;

} svn_client_proplist_item_t;


/* Information about commits passed back to client from this module. */
typedef struct svn_client_commit_info_t
{
  svn_revnum_t revision; /* just-committed revision. */
  const char *date;      /* server-side date of the commit. */
  const char *author;    /* author of the commit. */
} svn_client_commit_info_t;


/* Names of files that contain authentication information.

   These filenames are decided by libsvn_client, since this library
   implements all the auth-protocols;  libsvn_wc does nothing but
   blindly store and retrieve these files from protected areas. */
#define SVN_CLIENT_AUTH_USERNAME            "username"
#define SVN_CLIENT_AUTH_PASSWORD            "password"




/*** Milestone 4 Interfaces ***/

/* Perform a checkout from URL, providing pre- and post-checkout hook
   editors and batons (BEFORE_EDITOR, BEFORE_EDIT_BATON /
   AFTER_EDITOR, AFTER_EDIT_BATON).  These editors are purely optional
   and exist only for extensibility;  pass four NULLs here if you
   don't need them.

   PATH will be the root directory of your checked out working copy.

   If XML_SRC is NULL, then the checkout will come from the repository
   and subdir specified by URL.  An invalid REVISION will cause the
   "latest" tree to be fetched, while a valid REVISION will fetch a
   specific tree.  Alternatively, a time TM can be used to implicitly
   select a revision.  TM cannot be used at the same time as REVISION.

   If XML_SRC is non-NULL, it is an xml file to check out from; in
   this case, the working copy will record the URL as artificial
   ancestry information.  An invalid REVISION implies that the
   revision *must* be present in the <delta-pkg> tag, while a valid
   REVISION will be simply be stored in the wc. (Note:  a <delta-pkg>
   revision will *always* override the one passed in.)

   This operation will use the provided memory POOL. */
svn_error_t *
svn_client_checkout (const svn_delta_edit_fns_t *before_editor,
                     void *before_edit_baton,
                     const svn_delta_edit_fns_t *after_editor,
                     void *after_edit_baton,
                     svn_client_auth_baton_t *auth_baton,
                     svn_stringbuf_t *URL,
                     svn_stringbuf_t *path,
                     svn_revnum_t revision,
                     svn_boolean_t recurse,
                     apr_time_t tm,
                     svn_stringbuf_t *xml_src,
                     apr_pool_t *pool);


/* Perform an update of PATH (part of a working copy), providing pre-
   and post-checkout hook editors and batons (BEFORE_EDITOR,
   BEFORE_EDIT_BATON / AFTER_EDITOR, AFTER_EDIT_BATON).  These editors
   are purely optional and exist only for extensibility; pass four
   NULLs here if you don't need them.

   If XML_SRC is NULL, then the update will come from the repository
   that PATH was originally checked-out from.  An invalid REVISION
   will cause the PATH to be updated to the "latest" revision, while a
   valid REVISION will update to a specific tree.  Alternatively, a
   time TM can be used to implicitly select a revision.  TM cannot be
   used at the same time as REVISION.

   If XML_SRC is non-NULL, it is an xml file to update from.  An
   invalid REVISION implies that the revision *must* be present in the
   <delta-pkg> tag, while a valid REVISION will be simply be stored in
   the wc. (Note: a <delta-pkg> revision will *always* override the
   one passed in.)

   This operation will use the provided memory POOL. */
svn_error_t *
svn_client_update (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *path,
                   svn_stringbuf_t *xml_src,
                   svn_revnum_t revision,
                   apr_time_t tm,
                   svn_boolean_t recurse,
                   apr_pool_t *pool);



/* Perform an update of PATH (part of a working copy), providing pre-
   and post-checkout hook editors and batons (BEFORE_EDITOR,
   BEFORE_EDIT_BATON / AFTER_EDITOR, AFTER_EDIT_BATON).  These editors
   are purely optional and exist only for extensibility; pass four
   NULLs here if you don't need them.

   SWITCH_URL is the new URL that the working copy will be 'switched' to.

   An invalid REVISION will cause the PATH to be updated to the
   "latest" revision of SWITCH_URL, while a valid REVISION will update
   to a specific revision of SWITCH_URL.  Alternatively, a time TM can
   be used to implicitly select a revision.  TM cannot be used at the
   same time as REVISION.

   This operation will use the provided memory POOL. */
svn_error_t *
svn_client_switch (const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *path,
                   svn_stringbuf_t *switch_url,
                   svn_revnum_t revision,
                   apr_time_t tm,
                   svn_boolean_t recurse,
                   apr_pool_t *pool);


/* Schedule a working copy PATH for addition to the repository.
   PATH's parent must be under revision control already, but PATH is
   not.  If RECURSIVE is set, then assuming PATH is a directory, all
   of its contents will be scheduled for addition as well.

   Important:  this is a *scheduling* operation.  No changes will
   happen to the repository until a commit occurs.  This scheduling
   can be removed with svn_client_revert. */
svn_error_t *
svn_client_add (svn_stringbuf_t *path,
                svn_boolean_t recursive,
                apr_pool_t *pool);

/* If PATH is a URL, use the AUTH_BATON and MESSAGE to immediately
   attempt to commit the creation of the directory URL in the
   repository.  If the commit succeeds, allocate (in POOL) and
   populate *COMMIT_INFO.

   Else, create the directory on disk, and attempt to schedule it for
   addition (using svn_client_add, whose docstring you should
   read). */
svn_error_t *
svn_client_mkdir (svn_client_commit_info_t **commit_info,
                  svn_stringbuf_t *path,
                  svn_client_auth_baton_t *auth_baton,
                  svn_stringbuf_t *message,
                  apr_pool_t *pool);
                  

/* If PATH is a URL, use the AUTH_BATON and MESSAGE to immediately
   attempt to commit a deletion of the URL from the repository.  If
   the commit succeeds, allocate (in POOL) and populate *COMMIT_INFO.
  
   Else, schedule a working copy PATH for removal from the repository.
   PATH's parent must be under revision control.  If FORCE is set,
   then PATH itself will be recursively removed as well; otherwise
   PATH simply stops being tracked by the working copy.  This is just
   a *scheduling* operation.  No changes will happen to the repository
   until a commit occurs.  This scheduling can be removed with
   svn_client_revert. */
svn_error_t *
svn_client_delete (svn_client_commit_info_t **commit_info,
                   svn_stringbuf_t *path,
                   svn_boolean_t force,
                   svn_client_auth_baton_t *auth_baton,
                   svn_stringbuf_t *message,
                   apr_pool_t *pool);


/* Import a tree, using optional pre- and post-commit hook editors
   (BEFORE_EDITOR, BEFORE_EDIT_BATON, and AFTER_EDITOR,
   AFTER_EDIT_BATON).  These editors are purely optional and exist
   only for extensibility; pass four NULLs here if you don't need
   them.
  
   If the import succeeds, allocate (in POOL) and populate
   *COMMIT_INFO.
  
   Store LOG_MSG as the log of the commit.
   
   PATH is the path to local tree being imported.  PATH can be a file
   or directory.
  
   URL is the repository directory where the imported data is placed.
  
   NEW_ENTRY is the new entry created in the repository directory
   identified by URL.
  
   If PATH is a file, that file is imported as NEW_ENTRY.  If PATH is
   a directory, the contents of that directory are imported, under a
   new directory the NEW_ENTRY in the repository.  Note and the
   directory itself is not imported; that is, the basename of PATH is
   not part of the import.
  
   If PATH is a directory and NEW_ENTRY is null, then the contents of
   PATH are imported directly into the repository directory identified
   by URL.  NEW_ENTRY may not be the empty string.
  
   If NEW_ENTRY already exists in the youngest revision, return error.
   
   If XML_DST is non-NULL, it is a file in which to store the xml
   result of the commit, and REVISION is used as the revision.
   
   Use POOL for all allocation.
   
   ### kff todo: This import is similar to cvs import, in that it does
   not change the source tree into a working copy.  However, this
   behavior confuses most people, and I think eventually svn _should_
   turn the tree into a working copy, or at least should offer the
   option. However, doing so is a bit involved, and we don't need it
   right now.  */
svn_error_t *svn_client_import (svn_client_commit_info_t **commit_info,
                                const svn_delta_edit_fns_t *before_editor,
                                void *before_edit_baton,
                                const svn_delta_edit_fns_t *after_editor,
                                void *after_edit_baton, 
                                svn_client_auth_baton_t *auth_baton,   
                                svn_stringbuf_t *path,
                                svn_stringbuf_t *url,
                                svn_stringbuf_t *new_entry,
                                svn_stringbuf_t *log_msg,
                                svn_stringbuf_t *xml_dst,
                                svn_revnum_t revision,
                                apr_pool_t *pool);


/* Perform an commit, providing pre- and post-commit hook editors and
   batons (BEFORE_EDITOR, BEFORE_EDIT_BATON, and AFTER_EDITOR,
   AFTER_EDIT_BATON).  These editors are purely optional and exist
   only for extensibility; pass four NULLs here if you don't need
   them.

   If the commit succeeds, allocate (in POOL) and populate
   *COMMIT_INFO.
  
   TARGETS is an array of svn_stringbuf_t * paths to commit.  They need
   not be canonicalized nor condensed; this function will take care of
   that.

   If XML_DST is NULL, then the commit will write to a repository, and
   the REVISION argument is ignored.

   If XML_DST is non-NULL, it is a file path to commit to.  In this
   case, if REVISION is valid, the working copy's revision numbers
   will be updated appropriately.  If REVISION is invalid, the working
   copy remains unchanged.

   This operation will use the provided memory POOL.

   If no error is returned, and *COMMITTED_REV is set to
   SVN_INVALID_REVNUM, then the commit was a no-op;  nothing needed to
   be committed.
 */
svn_error_t *
svn_client_commit (svn_client_commit_info_t **commit_info,
                   const svn_delta_edit_fns_t *before_editor,
                   void *before_edit_baton,
                   const svn_delta_edit_fns_t *after_editor,
                   void *after_edit_baton,
                   svn_client_auth_baton_t *auth_baton,
                   const apr_array_header_t *targets,
                   svn_stringbuf_t *log_msg,
                   svn_stringbuf_t *xml_dst,
                   svn_revnum_t revision,  /* this param is temporary */
                   apr_pool_t *pool);


/* Given PATH to a working copy directory (or single file), allocate
   and return a hash STATUSHASH which maps (char *) paths to
   (svn_wc_status_t *) structures.

   This is a purely local operation; only information found in the
   administrative `entries' files is used to initially build the
   structures.

      - If DESCEND is non-zero, recurse fully, else do only immediate
        children.  This (inversely) corresponds to the "-n"
        (--nonrecursive) flag in the commandline client app.

      - If GET_ALL is set, then all entries are retrieved; otherwise
        only "interesting" entries (local mods and/or out-of-date)
        will be fetched.  This directly corresponds to the "-v"
        (--verbose) flag in the commandline client app.

      - If UPDATE is set, then the repository will be contacted, so
        that the structures in STATUSHASH are augmented with
        information about out-of-dateness, and *YOUNGEST is set to the
        youngest repository revision (*YOUNGEST is not touched unless
        UPDATE is set).  This directly corresponds to the "-u"
        (--show-updates) flag in the commandline client app.

  */
svn_error_t *
svn_client_status (apr_hash_t **statushash,
                   svn_revnum_t *youngest,  /* only touched if `update' set */
                   svn_stringbuf_t *path,
                   svn_client_auth_baton_t *auth_baton,
                   svn_boolean_t descend,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   apr_pool_t *pool);


/* Invoke RECEIVER with RECEIVER_BATON on each log message from START
   to END in turn.  
  
   PATHS contains all the working copy paths (as svn_stringbuf_t *'s)
   for which log messages are desired; the common prefix of PATHS
   determines the repository and auth info.  RECEIVER is invoked only
   on messages whose revisions involved a change to some path in
   PATHS.
  
   ### todo: the above paragraph is not fully implemented yet.
  
   If DISCOVER_CHANGED_PATHS is set, then the `changed_paths' argument
   to RECEIVER will be passed on each invocation.
  
   Use POOL for any temporary allocation.
 */
svn_error_t *
svn_client_log (svn_client_auth_baton_t *auth_baton,
                const apr_array_header_t *targets,
                svn_revnum_t start,
                svn_revnum_t end,
                svn_boolean_t discover_changed_paths,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                apr_pool_t *pool);


/* Given a TARGET which is either a path in the working copy or an URL,
   compare it against the given repository version(s).
  
   START_REVISION/START_DATE and END_REVISION/END_DATE are the two
   repository versions, for each specify either the revision of the
   date. If the two revisions are the different the two repository versions
   are compared. If the two revisions are the same the working copy is
   compared against the repository.
  
   If TARGET is a directory and RECURSE is true, this will be a recursive
   operation.
  
   DIFF_OPTIONS is used to pass additional command line options to the diff
   processes invoked to compare files. DIFF_OPTIONS is an array of
   svn_stringbuf_t * items.
  
   AUTH_BATON is used to communicate with the repository.
 */
svn_error_t *svn_client_diff (svn_stringbuf_t *target,
                              const apr_array_header_t *diff_options,
                              svn_client_auth_baton_t *auth_baton,
                              svn_revnum_t start_revision,
                              apr_time_t start_date,
                              svn_revnum_t end_revision,
                              apr_time_t end_date,
                              svn_boolean_t recurse,
                              apr_pool_t *pool);


/* Recursively cleanup a working copy directory DIR, finishing any
   incomplete operations, removing lockfiles, etc. */
svn_error_t *
svn_client_cleanup (svn_stringbuf_t *dir,
                    apr_pool_t *pool);


/* Restore the pristine version of a working copy PATH, effectively
   undoing any local mods.  If PATH is a directory, and RECURSIVE is
   TRUE, this will be a recursive operation.  */
svn_error_t *
svn_client_revert (svn_stringbuf_t *path,
                   svn_boolean_t recursive,
                   apr_pool_t *pool);


/* Copy SRC_PATH to DST_PATH.

   SRC_PATH must be a file or directory under version control, or the
   URL of a versioned item in the repository.  If SRC_PATH is a URL,
   SRC_REV is used to choose the revision from which to copy the
   SRC_PATH.  DST_PATH must be a file or directory under version
   control, or a repository URL, existent or not.

   If either SRC_PATH or DST_PATH are URLs, use the AUTH_BATON and
   MESSAGE to immediately attempt to commit the copy action in the
   repository.  If the commit succeeds, allocate (in POOL) and
   populate *COMMIT_INFO.

   If the operation involves interaction between the working copy and
   the repository, there may be an editor drive, in which case the
   BEFORE_EDITOR, BEFORE_EDIT_BATON and AFTER_EDITOR, AFTER_EDIT_BATON
   will be wrapped around the edit using svn_delta_wrap_editor()
   (which see in svn_delta.h).

   If neither SRC_PATH nor DST_PATH is a URL, then this is just a
   variant of svn_client_add, where the DST_PATH items are scheduled
   for addition as copies.  No changes will happen to the repository
   until a commit occurs.  This scheduling can be removed with
   svn_client_revert.  */
svn_error_t *
svn_client_copy (svn_client_commit_info_t **commit_info,
                 svn_stringbuf_t *src_path,
                 svn_revnum_t src_rev,
                 svn_stringbuf_t *dst_path,
                 svn_client_auth_baton_t *auth_baton,
                 svn_stringbuf_t *message,
                 const svn_delta_edit_fns_t *before_editor,
                 void *before_edit_baton,
                 const svn_delta_edit_fns_t *after_editor,
                 void *after_edit_baton,
                 apr_pool_t *pool);


/* Move SRC_PATH to DST_PATH.

   SRC_PATH must be a file or directory under version control, or the
   URL of a versioned item in the repository.  

   If SRC_PATH is a repository URL:

     - DST_PATH must also be a repository URL (existent or not).

     - SRC_REV is used to choose the revision from which to copy the
       SRC_PATH.

     - AUTH_BATON and MESSAGE are used to commit the move.

     - The move operation will be immediately committed.  If the
       commit succeeds, allocate (in POOL) and populate *COMMIT_INFO.

   If SRC_PATH is a working copy path

     - DST_PATH must also be a working copy path (existent or not).

     - SRC_REV, AUTH and MESSAGE are ignored.

     - This is a scheduling operation.  No changes will happen to the
       repository until a commit occurs.  This scheduling can be
       removed with svn_client_revert. */
svn_error_t *
svn_client_move (svn_client_commit_info_t **commit_info,
                 svn_stringbuf_t *src_path,
                 svn_revnum_t src_rev,
                 svn_stringbuf_t *dst_path,
                 svn_client_auth_baton_t *auth_baton,
                 svn_stringbuf_t *message,
                 apr_pool_t *pool);



/* Set PROPNAME to PROPVAL on TARGET.  If RECURSE is true, then PROPNAME
   will be set on recursively on TARGET and all children.  If RECURSE is false,
   and TARGET is a directory, PROPNAME will be set on _only_ TARGET.
 
   Use POOL for all memory allocation. */
svn_error_t *
svn_client_propset (const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    apr_pool_t *pool);

/* Set *PROPS to a hash table whose keys are `char *' paths,
   prefixed by TARGET, of items in the working copy on which 
   property PROPNAME is set, and whose values are `svn_string_t *'
   representing the property value for PROPNAME at that path.
   Allocate *PROPS, its keys, and its values in POOL.
             
   Don't store any path, not even TARGET, if it does not have a
   property named PROPNAME.

   If TARGET is a file or RECURSE is false, *PROPS will have
   at most one element.

   If error, don't touch *PROPS, otherwise *PROPS is a hash table even if
   empty. */
svn_error_t *
svn_client_propget (apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    svn_boolean_t recurse,
                    apr_pool_t *pool);

/* Returns an apr_array_header_t of svn_client_proplist_item_t's in *PROPS,
   allocated from POOL. Each item will contain the node_name relative to the
   same base as target in item->node_name, and a property hash of 
   (const char *) property names, and (svn_stringbuf_t *) property values.

   If recurse is false, or TARGET is a file, *PROPS will contain only a single
   element.  Otherwise, it will contain one for each versioned entry below
   (and including) TARGET. */
svn_error_t *
svn_client_proplist (apr_array_header_t **props,
                     const char *target, 
                     svn_boolean_t recurse,
                     apr_pool_t *pool);

#endif  /* SVN_CLIENT_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
