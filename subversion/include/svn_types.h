/*
 * svn_types.h :  Subversion's data types
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

/* This is more or less an implementation of the filesystem "schema"
   defined tin the design doc. */


#ifndef __SVN_TYPES_H__
#define __SVN_TYPES_H__


#include <stdlib.h>          /* defines size_t */
#include <apr_pools.h>       /* APR memory pools for everyone. */
#include <apr_hash.h>        /* proplists are hashtables */


/* useful macro, suggested by Greg Stein */
#define AP_ARRAY_GET_ITEM(ary,i,type) (((type *)(ary)->elts)[i])



/* a string of bytes  */

typedef struct svn_string_t
{
  char *data;                /* pointer to the bytestring */
  size_t len;                /* length of bytestring */
  size_t blocksize;          /* total size of buffer allocated */
} svn_string_t;


/* a file is a proplist and a string */
typedef struct svn_file_t
{
  ap_hash_t *proplist;  /* the file's properties */
  svn_string_t *text;        /* the file's main content */
} svn_file_t;


/* a directory entry points to a node */
typedef struct svn_dirent_t
{
  unsigned long node_num;    /* a node pointed to */
  svn_string_t *name;        /* name of the node pointed to */
  ap_hash_t *proplist;  /* entry's properties */
} svn_dirent_t;


/* TODO:  use an actual ap_array below */
/* a directory is an unordered list of directory entries, and a proplist */
typedef struct svn_directory_t
{
  svn_dirent_t *list;        /* an array of dirents */
  size_t len;                /* length of array */
  ap_hash_t *proplist;  /* directory's properties */
} svn_directory_t;


/* a node is either a file or directory, a distinguished union  */
typedef struct svn_node_t
{
  enum node_kind {svn_file_kind, svn_directory_kind} kind;
  union node_union 
  {
    svn_file_t *file;
    svn_directory_t *directory;
  } contents;                             /* my contents */
} svn_node_t;


/* a version is a node number and property list */
typedef struct svn_ver
{
  unsigned long node_num;             /* the root node of a tree */
  ap_hash_t *proplist;           /* version's properties */
} svn_ver_t;


/* These things aren't critical to define yet; I'll leave them to
   jimb, who's writing the filesystem: */

/* a node table is a mapping of some set of natural numbers to nodes. */

/* a history is an array of versions */

/* a repository is a node table and a history */


/* A list of all filesystem calls that users can perform.  Each
   ACL/authorization system must create its own concept of
   "permissions" around these filesystem calls. */

typedef enum 
{
  svn_action_latest,
  svn_action_get_ver_prop,
  svn_action_get_ver_proplist,
  svn_action_get_ver_propnames,
  svn_action_read,
  svn_action_get_node_prop,
  svn_action_get_dirent_prop,
  svn_action_get_node_proplist,
  svn_action_get_dirent_proplist,
  svn_action_get_node_propnames,
  svn_action_get_dirent_propnames,
  svn_action_submit,
  svn_action_write,
  svn_action_abandon,
  svn_action_get_delta,
  svn_action_get_diff,
  svn_action_status,
  svn_action_update
} svn_svr_action_t;




/* This structure defines a client 'user' to be used by any security
   plugin on the Subversion server.  This structure is created by the
   network layer when it performs initial authentication with some
   database.  */

typedef struct svn_user
{
  /* The first three fields are filled in by the network layer,
     and possibly used by the server for informational or matching purposes */

  svn_string_t auth_username;       /* the authenticated username */
  svn_string_t auth_method;         /* the authentication system used */
  svn_string_t auth_domain;         /* where the user comes from */


  /* This field is used by all of the server's "wrappered" fs calls */

  svn_string_t svn_username;        /* the username which will
                                       >actually< be used when making
                                       filesystem calls */

  void *username_data;              /* if a security plugin needs to
                                       store extra data, such as a
                                       WinNT SID */

} svn_user_t;




/* YABT:  Yet Another Boolean Type */

typedef int svn_boolean_t;

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */


typedef unsigned long svn_token_t;



/* ******** Deltas and friends. ******** */

/* These are the in-memory tree deltas; you can convert them to and
 * from XML.
 * 
 * The XML representation has certain space optimizations.  For
 * example, if an ancestor is omitted, it means the same path at the
 * same version (taken from the surrounding delta context).  We may
 * well decide to use corresponding optimizations here -- an absent
 * svn_ancestor_t object means use the path and ancestor from the
 * delta, etc -- or we may not.  In any case it doesn't affect the
 * definitions of these data structures.  However, once we do know
 * what interpretive conventions we're using in code, we should
 * probably record them here.
 */

/* todo: We'll need a way to stream these, so when you do a checkout
 * of comp-tools, the client doesn't wait for an entire 200 meg tree
 * delta to arrive before doing anything.
 * 
 * Proposal:
 * 
 * A caller (say, the working copy library) is given the tree delta as
 * soon as there is at least one svn_change_t in its list ready to
 * use.  The callee may continue to append svn_change_t objects to the
 * list even while the caller is using the ones already there.  The
 * callee signals that it is done by adding a change of the special
 * type `done' (see the enumeration `svn_delta_action_t' below).
 *
 * Since the caller can tell by inspection whether or not it's done
 * yet, the callee could tack on new change objects in an unscheduled
 * fashion (i.e., as a separate thread), or the caller could make an
 * explicit call each time it finishes available changes.  Either way
 * works; the important thing is to give the network time to catch up.
 */

typedef size_t svn_version_t;   /* Would they ever need to be signed? */
typedef int pdelta_t;           /* todo: for now */
typedef int vdelta_t;           /* todo: for now */

/* It would have been more consistent to name this `svn_change_action_t', 
   but the ambiguity is too great -- is "change" a noun or a verb? */
typedef enum { 
  svn_delta_action_delete = 1,  /* Delete the file or directory. */
  svn_delta_action_new,         /* Create a new file or directory. */
  svn_delta_action_replace,     /* Commit to an existing file or directory. */
  changes_done                  /* End of change chain -- no more action. */
} svn_delta_action_t;

typedef enum { 
  file_type = 1,
  directory_type
} svn_change_content_type_t;


/* Change content is delta(s) against ancestors.  This is one kind of delta. */
typedef struct svn_pdelta_t {
  int todo;
} svn_pdelta_t;


/* Change content is delta(s) against ancestors.  This is one kind of delta. */
typedef struct svn_vdelta_t {
  int todo;
} svn_vdelta_t;


/* Change content is delta(s) against ancestors.  This is an ancestor. */
typedef struct svn_ancestor_t
{
  svn_string_t *path;
  svn_version_t version;
  svn_boolean_t new;
} svn_ancestor_t;


/* A change is an action and some content.  This is the content. */
typedef struct svn_change_content_t
{
  svn_change_content_type_t type;   /* One of the enumerated values. */
  svn_ancestor_t *ancestor;         /* "Hoosier paw?!" */
  svn_pdelta_t *pdelta;             /* Change to property list, or NULL. */
  svn_vdelta_t *vdelta;             /* Change to file contents, or NULL. */
} svn_change_content_t;


/* A tree delta is a list of changes.  This is a change. */
typedef struct svn_change_t
{
  svn_delta_action_t action;      /* One of the enumerated values. */
  svn_string_t *new_name;         /* Only for `new' and `replace'. */
  svn_change_content_t *content;
  struct svn_change_t *next;      /* Next one in the list, or NULL. */
} svn_change_t;


/* This is a tree delta. */
typedef struct svn_delta_t
{
  svn_version_t version;       /* Directory to which this delta applies */
  svn_string_t *source_root;   /* Indicates a particular version of... */
  svn_string_t *source_dir;    /* ...this, which we're modifying to yield... */
  svn_string_t *target_dir;    /* ...the directory we're constructing. */
} svn_delta_t;


/* A skelta is just a tree delta with empty pdeltas and vdeltas. */
typedef svn_delta_t svn_skelta_t;

/* A line-based diff is just a huge wad of text. */
typedef svn_string_t svn_diff_t;

#endif  /* __SVN_TYPES_H__ */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
