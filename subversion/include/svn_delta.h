/*
 * svn_delta.h :  the delta structure and friends
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



#ifndef SVN_DELTA_H
#define SVN_DELTA_H

#include <apr_pools.h>
#include <xmlparse.h>
#include "svn_types.h"


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

/* Note that deltas are constructed and deconstructed streamily.  That
 * way when you do a checkout of comp-tools, for example, the client
 * doesn't wait for an entire 200 meg tree delta to arrive before
 * doing anything.
 *
 * The delta being {de}constructed is passed along as one of the
 * arguments to the XML parser callbacks; the callbacks use the
 * existing delta, plus whatever the parser just saw that caused the
 * callback to be invoked, to figure out what to do next.
 */

typedef size_t svn_version_t;   /* Would they ever need to be signed? */

/* A property diff */
typedef struct svn_pdelta_t {
  enum {
    svn_prop_set = 1,
    svn_prop_delete
  } kind;
  svn_string_t *name;
  svn_string_t *value;
  struct svn_pdelta_t *next;
} svn_pdelta_t;


/* A binary diff */
typedef struct svn_vdelta_t {
  int todo;
} svn_vdelta_t;


/* An edit is an action and some content.  This is the content. */
typedef struct svn_edit_content_t
{
  enum { 
    svn_file_type = 1,
    svn_directory_type
  } kind;                           /* what kind of object is this? */

  /* An ancestor is a path rooted from a version. */
  svn_string_t *ancestor_path;      /* If NULL, this object is `new'. */
  svn_version_t ancestor_version;

  svn_boolean_t prop_delta;         /* flag: upcoming prop delta data */
  svn_boolean_t text_delta;         /* flag: upcoming text delta data */
  struct svn_delta_t *tree_delta;   /* A further tree delta, or NULL. */
} svn_edit_content_t;


/* A tree delta is a list of edits.  This is an edit. */
typedef struct svn_edit_t
{
  enum { 
    svn_action_delete = 1,            /* Delete a file or directory. */
    svn_action_new,                   /* Create a new file or directory. */
    svn_action_replace,               /* Replace an existing file or dir */
  } kind;
  svn_string_t *name;             /* name to add/del/replace */
  svn_edit_content_t *content;    /* the object we're adding/replacing */
} svn_edit_t;


/* This is a tree delta. */
typedef struct svn_delta_t
{
  svn_string_t *source_root;   /* Directory to which this delta applies */
  svn_version_t base_version;  /* Base version of this directory */
  svn_edit_t *edit;            /* latest edit we're holding */
} svn_delta_t;



/* An enumerated type that can indicates one of the Subversion-delta
   XML tag categories; needed for walking & building a
   delta-in-progress */

typedef enum
{
  svn_XML_treedelta = 1,
  svn_XML_edit,
  svn_XML_editcontent,
  svn_XML_propdelta,
  svn_XML_textdelta
  
} svn_XML_elt_t



/* An svn_delta_digger_t is passed as *userData to Expat (and from
 * there to registered callback functions).
 *
 * As the callbacks see various XML elements, they construct
 * digger->delta.  Most elements merely require a new component of the
 * delta to be built and hooked in, with no further action.  Other
 * elements, such as a directory or actual file contents, require
 * special action from the caller.  For example, if the caller is from
 * the working copy library, it might create the directory or the file
 * on disk; or if the caller is from the repository, it might want to
 * start building nodes for a commit.  The digger holds function
 * pointers for such callbacks, and the delta provides context to
 * those callbacks -- e.g., the name of the directory or file to
 * create.
 *
 *    Note ("heads we win, tails we lose"):
 *    =====================================
 *    A digger only stores the head of the delta, even though the
 *    place we hook things onto is the tail.  While it would be
 *    technically more efficient to keep a pointer to tail, it would
 *    also be more error-prone, since it's another thing to keep track
 *    of.  And the maximum chain length of the delta is proportional
 *    to the max directory depth of the tree the delta represents,
 *    since we always snip off any completed portion of the delta
 *    (i.e., every time we encounter a closing tag, we remove what it
 *    closed from the delta).  So cdr'ing down the chain to the end is
 *    not so bad.  Given that deltas usually result in file IO of some
 *    kind, a little pointer chasing should be lost in the noise.
 */
typedef struct svn_delta_digger_t
{
  apr_pool_t *pool;
  
  svn_delta_t *delta;

  /* TODO: might want to declare a 
   * 
   *    svn_edit_content_t *context;
   *
   * or something for the data handler to use as instantly available
   * context; otherwise it's cdr'ing down the delta each time.  Not
   * horrible, since depth is never very great, but not the most
   * efficient thing either. 
   */

  /* Caller uses delta context to determine if prop data or text data. */
  svn_error_t (*data_handler) (svn_delta_digger_t *digger,
                               const char *data,
                               int len);

  /* Call handles dirs specially, because might want to create them. 
   * It gets the digger for context, but also the current edit_content
   * because that's a faster way to get this edit. 
   */
  svn_error_t (*dir_handler) (svn_delta_digger_t *digger,
                              svn_edit_content_t *this_edit_content);

  /* Caller optionally decides what to do with unrecognized elements. */
  svn_error_t (*unknown_elt_handler) (svn_delta_digger_t *digger,
                                      const char *name,
                                      const char **atts);

} svn_delta_digger_t;




/* Creates a parser with the common callbacks and userData registered. */
XML_Parser svn_delta_make_xml_parser (svn_delta_digger_t *diggy);


#endif  /* SVN_DELTA_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
