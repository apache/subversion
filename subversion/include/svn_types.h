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


/* a string of bytes.  "bytes is bytes" */
struct _svn_string_t
{
  char *data;                /* pointer to the bytestring */
  unsigned long len;         /* length of bytestring */
  unsigned long blocksize;   /* total size of buffer */
}


/* a property is a pair of strings */
struct _svn_prop_t
{
  struct _svn_string_t *name;
  struct _svn_string_t *value;
}


/* a property list is an unordered list of properties */
struct _svn_proplist_t
{
  struct _svn_prop_t *list; /* an array of props */
  unsigned long len;        /* length of array */
}


/* a file is a proplist and a string */
struct _svn_file_t
{
  struct _svn_proplist_t *proplist;  /* the file's properties */
  struct _svn_string_t *text;        /* the file's main content */
}


/* a directory entry points to a node */
struct _svn_direent_t
{
  unsigned long node_num;            /* a node pointed to */
  struct _svn_string_t *name;        /* name of the node pointed to */
  struct _svn_proplist_t *proplist;  /* entry's properties */
}


/* a directory is an unordered list of directory entries, and a proplist */
struct _svn_directory_t
{
  struct _svn_direent_t *list;       /* an array of direents */
  unsigned long len;                 /* length of array */
  struct _svn_proplist_t *proplist;  /* directory's properties */
}


/* a node is either a file or directory, a distinguished union  */
struct _svn_node_t
{
  enum node_kind {file, directory} kind;  /* am I a file or directory? */
  union node_union 
  {
    struct _svn_file_t *file;
    struct _svn_directory_t *directory;
  } contents;                             /* my contents */
}


/* a version is a node number and property list */
struct _svn_ver_t
{
  unsigned long node_num;             /* the root node of a tree */
  struct _svn_proplist_t *proplist;   /* version's properties */
}


/* These things aren't critical to define yet; I'll leave them to
   jimb, who's writing the filesystem: */

/* a node table is a mapping of some set of natural numbers to nodes. */

/* a history is an array of versions */

/* a repository is a node table and a history */




/* Now we typedef all our structures, removing the prefix. */

typedef struct _svn_string_t          svn_string_t;
typedef struct _svn_prop_t            svn_prop_t;
typedef struct _svn_proplist_t        svn_proplist_t;
typedef struct _svn_file_t            svn_file_t;
typedef struct _svn_direent_t         svn_dirrent_t;
typedef struct _svn_directory_t       svn_directory_t;
typedef struct _svn_node_t            svn_node_t;
typedef struct _svn_ver_t             svn_ver_t;

/* temporary placeholders, till we write the real thing!  */
typedef unsigned long                 svn_token_t;
typedef int                           svn_skelta_t;
typedef int                           svn_delta_t;
typedef int                           svn_diff_t;


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
