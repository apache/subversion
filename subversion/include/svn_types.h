/*
 * svn_types.h :  Subversion's data types
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
 */

/* ==================================================================== */

/* This is more or less an implementation of the filesystem "schema"
   defined tin the design doc. */


#ifndef SVN_TYPES_H
#define SVN_TYPES_H


#include <stdlib.h>          /* defines size_t */
#include <apr_pools.h>       /* APR memory pools for everyone. */
#include <apr_hash.h>        /* proplists are hashtables */



/* useful macro, suggested by Greg Stein */
#define APR_ARRAY_GET_ITEM(ary,i,type) (((type *)(ary)->elts)[i])



/* a file is a proplist and a string */
typedef struct svn_file_t
{
  apr_hash_t *proplist;          /* the file's properties */
  struct svn_string_t *text;     /* the file's main content */
} svn_file_t;


/* a directory entry points to a node */
typedef struct svn_dirent_t
{
  unsigned long node_num;     /* a node pointed to */
  struct svn_string_t *name;  /* name of the node pointed to */
  apr_hash_t *proplist;       /* entry's properties */
} svn_dirent_t;


/* TODO:  use an actual apr_array below */
/* a directory is an unordered list of directory entries, and a proplist */
typedef struct svn_directory_t
{
  svn_dirent_t *list;        /* an array of dirents */
  size_t len;                /* length of array */
  apr_hash_t *proplist;  /* directory's properties */
} svn_directory_t;


/* a node is either a file or directory, a distinguished union  */
typedef struct svn_node_t
{
  enum svn_node_kind {svn_file_kind = 1, svn_dir_kind} kind;
  union node_union 
  {
    svn_file_t *file;
    svn_directory_t *directory;
  } contents;                             /* my contents */
} svn_node_t;


/* a version object is a node number and property list */
typedef struct svn_ver_t
{
  unsigned long node_num;             /* the root node of a tree */
  apr_hash_t *proplist;           /* version's properties */
} svn_ver_t;


typedef long int svn_vernum_t;

/* This is never a valid version number.  (Actually, anything less
   than 0 is never a valid version number.) */
#define SVN_INVALID_VERNUM -1


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

typedef struct svn_user_t
{
  /* The first three fields are filled in by the network layer,
     and possibly used by the server for informational or matching purposes */

  struct svn_string_t *auth_username;  /* the authenticated username */
  struct svn_string_t *auth_method;    /* the authentication system used */
  struct svn_string_t *auth_domain;    /* where the user comes from */


  /* This field is used by all of the server's "wrappered" fs calls */

  struct svn_string_t *svn_username;   /* the username which will
                                          >actually< be used when making
                                          filesystem calls */

  void *username_data;                 /* if a security plugin needs to
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



#endif  /* SVN_TYPES_H */



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
