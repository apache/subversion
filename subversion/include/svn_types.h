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

/* a string of bytes.  "bytes is bytes" */
typedef struct svn_string_t
{
  char *data;                /* pointer to the bytestring */
  size_t len;                /* length of bytestring */
  size_t blocksize;          /* total size of buffer allocated */
} svn_string_t;


/* a property is a pair of strings */
typedef struct svn_prop_t
{
  svn_string_t *name;
  svn_string_t *value;
} svn_prop_t;


/* a property list is an unordered list of properties */
typedef struct svn_proplist_t
{
  svn_prop_t *list;         /* an array of props */
  size_t len;               /* length of array */
} svn_proplist_t;


/* a file is a proplist and a string */
typedef struct svn_file_t
{
  svn_proplist_t *proplist;  /* the file's properties */
  svn_string_t *text;        /* the file's main content */
} svn_file_t;


/* a directory entry points to a node */
typedef struct svn_dirent_t
{
  unsigned long node_num;    /* a node pointed to */
  svn_string_t *name;        /* name of the node pointed to */
  svn_proplist_t *proplist;  /* entry's properties */
} svn_dirent_t;


/* a directory is an unordered list of directory entries, and a proplist */
typedef struct svn_directory_t
{
  svn_dirent_t *list;        /* an array of dirents */
  size_t len;         /* length of array */
  svn_proplist_t *proplist;  /* directory's properties */
} svn_directory_t;


/* a node is either a file or directory, a distinguished union  */
typedef struct svn_node_t
{
  enum node_kind {file, directory} kind;  /* am I a file or directory? */
  union node_union 
  {
    svn_file_t *file;
    svn_directory_t *directory;
  } contents;                             /* my contents */
} svn_node_t;


/* a version is a node number and property list */
typedef struct svn_ver_t
{
  unsigned long node_num;             /* the root node of a tree */
  svn_proplist_t *proplist;           /* version's properties */
} svn_ver_t


/* These things aren't critical to define yet; I'll leave them to
   jimb, who's writing the filesystem: */

/* a node table is a mapping of some set of natural numbers to nodes. */

/* a history is an array of versions */

/* a repository is a node table and a history */


/* This is totally wrong right now; these should be filesystem-level
   actions, not client-level actions.  */

typedef enum svr_action {add, rm, mv, checkout, 
                         commit, import, update} svn_svr_action_t;



/* This structure defines a client 'user' to be used by any security
   plugin on the Subversion server.  This structure is created by the
   network layer when it performs initial authentication with some
   database.  */

typdef struct svn_user_t
{
  /* The first three fields are filled in by the network layer */

  svn_string_t auth_username;       /* the authenticated username */
  svn_string_t auth_method;         /* the authentication system used */
  svn_string_t auth_domain;         /* where the user comes from */


  /* This field is used by all of the server's "wrappered" fs calls */

  svn_string_t svn_username;        /* the username which will
                                       actually be used when making
                                       filesystem calls */

  void *username_data;              /* if a security plugin needs to
                                       store extra data, such as a
                                       WinNT SID */

} svn_user_t;


/* temporary placeholders, till we write the real thing!  */
typedef unsigned long                 svn_token_t;
typedef int                           svn_skelta_t;
typedef int                           svn_delta_t;
typedef int                           svn_diff_t;



#endif  /* __SVN_TYPES_H__ */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
