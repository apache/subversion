/*
 * delta.h:  private delta library things
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


#include "apr_pools.h"
#include "xmlparse.h"

#ifndef DELTA_H
#define DELTA_H


svn_error_t * svn_vcdiff_send_window (svn_vcdiff_parser_t *parser, 
                                      apr_size_t len);



/* These are the in-memory tree-delta stackframes; they are used to
 * keep track of a delta's state while the XML stream is being parsed.
 * 
 * The XML representation has certain space optimizations.  For
 * example, if an ancestor is omitted, it means the same path at the
 * same version (taken from the surrounding delta context).  We may
 * well decide to use corresponding optimizations here -- an absent
 * svn_ancestor_t object means use the path and ancestor from the
 * delta, etc -- or we may not.  In any case it doesn't affect the
 * definitions of these data structures.  However, once we do know
 * what interpretive conventions we're using in code, we should
 * probably record them here.  */

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


/* Types of XML tags we'll encounter */
typedef enum svn_delta__XML_t
{
  svn_delta__XML_treedelta,
  svn_delta__XML_new,
  svn_delta__XML_delete,
  svn_delta__XML_replace,
  svn_delta__XML_file,
  svn_delta__XML_dir,
  svn_delta__XML_textdelta,
  svn_delta__XML_propdelta,
  svn_delta__XML_set

} svn_delta__XML_t;





typedef struct svn_delta__stackframe_t
{
  svn_delta__XML_t tag;  /* this stackframe represents an open <tag> */

  svn_string_t *name;    /* if the tag had a "name" attribute attached */
  svn_string_t *ancestor_path;     /* Explicit, else inherited from parent */ 
  svn_vernum_t ancestor_version;   /* Explicit, else inherited from parent */ 

  void *baton;           /* holds caller data for the _current_ subdirectory */
  void *file_baton;      /* holds caller data for the _current_ file */

  struct svn_delta__stackframe_t *next;
  struct svn_delta__stackframe_t *previous;
  
} svn_delta__stackframe_t;



/* An svn_delta__digger_t is passed as *userData to Expat (and from
 * there to registered callback functions).
 *
 * As the callbacks see various XML elements, they construct
 * digger->stack.  This "stack" keeps track of the XML nesting and
 * aids in the validation of the XML.
 *
 *    Note ("heads we win, tails we lose"):
 *    =====================================
 *    A digger only stores the head of the stack, even though the
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
 *
 * The digger structure also holds critical information given to us by
 * the uber-caller of "svn_delta_parse", such as batons and a walker_t
 * structure which tells us what to do in the case of certain parse
 * events.
 *
 */

typedef struct svn_delta__digger_t
{
  /* Pool to do allocations from. */
  apr_pool_t *pool;

  /* A mirror of the stack we're getting from the XML structure, used
     for storing XML attributes and for XML validation. 
     
     NOTE that this is the *YOUNGEST* frame on the stack, not the oldest! */
  svn_delta__stackframe_t *stack;

  /* Callbacks to use when we discover interesting XML events */
  const svn_delta_walk_t *walker;

  /* Userdata structures that we need to keep track of while we parse,
     given to us by either the SVN filesystem or the SVN client */
  void *walk_baton;  /* (global data from our caller) */
  void *dir_baton;   /* (local info about root directory;  local subdir
                         info will be stored in each stackframe structure ) */
  void *file_baton;  /* (local info about current file) */

  /* Has a validation error happened in the middle of an expat
     callback?  signal_expat_bailout() fills in this field, and
     svn_delta_parse() checks this value between calls to expat's
     parser. */
  svn_error_t *validation_error;

  /* The expat parser itself, so that our expat callbacks have the
     power to set themselves to NULL in the case of an error.  (Again,
     this is done by signal_expat_bailout(). */
  XML_Parser expat_parser;   /* (note: this is a pointer in disguise!) */

  /* A vcdiff parser, called whenever we receive binary data from
     expat.  Specifically, this is the _current_ vcdiff parser that
     we're using to handle the data within the _current_ file being
     added or replaced.*/
  svn_vcdiff_parser_t *vcdiff_parser;

  /* An in-memory prop-delta, possibly in the process of being
     buffered up */
  struct svn_propdelta_t *current_propdelta;

} svn_delta__digger_t;





/* An in-memory property delta */
typedef struct svn_propdelta_t
{
  enum {
    svn_propdelta_file,
    svn_propdelta_dir,
    svn_propdelta_dirent
  } kind;                    /* what kind of object does this
                                prop-delta affect? */

  svn_string_t *entity_name; /* The name of the file, dir, or dirent
                                which is being patched. */
  
  svn_string_t *name;        /* name of property to change */
  svn_string_t *value;       /* new value of property; if NULL, then
                                this property should be deleted. */

} svn_propdelta_t;




#endif  /* DELTA_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
