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
#include "apr_hash.h"
#include "svn_xml.h"

#ifndef DELTA_H
#define DELTA_H


/* Create a new window from PARSER->SUBPOOL, and send it off to the
   caller's consumer routine, then create a new SUBPOOL in PARSER so
   that it can continue buffering data.  */
svn_error_t *svn_vcdiff__send_window (svn_vcdiff_parser_t *parser, 
                                      apr_size_t len);

/* Send a null window to parser's consumer, signifying the end of this
   window stream. */
svn_error_t *svn_vcdiff__send_terminal_window (svn_vcdiff_parser_t *parser);


/* Private interface for text deltas. */

/* Allocate and initalize a delta window. */
svn_error_t *svn_txdelta__init_window (svn_txdelta_window_t **window,
                                       svn_txdelta_stream_t *stream);

/* Insert a delta op into the delta window. If the opcode is
   svn_delta_new, bytes from new_data are copied into the window
   data and offset is ignored, as it's maintained in the winfo struct.
   Otherwise new_data is ignored and may be NULL. */
svn_error_t *svn_txdelta__insert_op (svn_txdelta_window_t *window,
                                     int opcode,
                                     apr_off_t offset,
                                     apr_off_t length,
                                     const char *new_data);

/* Create a vdelta window. Allocate temporary data from `pool'. */
svn_error_t *svn_txdelta__vdelta (svn_txdelta_window_t *window,
                                  const char *const start,
                                  apr_size_t source_len,
                                  apr_size_t target_len,
                                  apr_pool_t *pool);

/* The delta window size. */
extern apr_size_t svn_txdelta__window_size;



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
  svn_delta__XML_deltapkg,
  svn_delta__XML_treedelta,
  svn_delta__XML_add,
  svn_delta__XML_delete,
  svn_delta__XML_replace,
  svn_delta__XML_file,
  svn_delta__XML_dir,
  svn_delta__XML_textdelta,
  svn_delta__XML_textdeltaref,
  svn_delta__XML_propdelta,
  svn_delta__XML_set

} svn_delta__XML_t;





typedef struct svn_xml__stackframe_t
{
  svn_delta__XML_t tag;  /* this stackframe represents an open <tag> */

  svn_string_t *name;    /* if the tag had a "name" attribute attached */
  svn_string_t *ancestor_path;     /* Explicit, else inherited from parent */ 
  svn_vernum_t ancestor_version;   /* Explicit, else inherited from parent */ 

  void *baton;           /* holds caller data for the _current_ subdirectory */
  void *file_baton;      /* holds caller data for the _current_ file */

  apr_hash_t *namespace; /* if this frame is a tree-delta, use this
                            hash to detect collisions in the
                            dirent-namespace */

  svn_string_t *ref_id;  /* if this frame is a postfix text-delta,
                            here is its ID string */

  svn_boolean_t hashed;  /* TRUE iff this is a <file> tag whose
                            file_baton has been stored in a postfix
                            hashtable. */
  
  struct svn_xml__stackframe_t *next;
  struct svn_xml__stackframe_t *previous;
  
} svn_xml__stackframe_t;



/* An svn_xml__digger_t is passed as *userData to Expat (and from
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
 * the uber-caller of "svn_delta_parse", such as batons and a editor_t
 * structure which tells us what to do in the case of certain parse
 * events.
 * */

typedef struct svn_xml__digger_t
{
  /* Pool to do allocations from. */
  apr_pool_t *pool;

  /* A mirror of the stack we're getting from the XML structure, used
     for storing XML attributes and for XML validation. 
     
     NOTE that this is the *YOUNGEST* frame on the stack, not the oldest! */
  svn_xml__stackframe_t *stack;

  /* Callbacks to use when we discover interesting XML events */
  const svn_delta_edit_fns_t *editor;

  /* General "context variables" used when evaluating a tree-delta */
  svn_string_t *base_path;
  svn_vernum_t base_version;

  /* Userdata structures that we need to keep track of while we parse,
     given to us by either the SVN filesystem or the SVN client */
  void *edit_baton;  /* (global data from our caller) */
  void *rootdir_baton; /* (local info about root directory;  local subdir
                          info will be stored in each stackframe structure) */
  void *dir_baton;   /* (temporary info about current working dir, also
                        stored within stackframes.) */
  void *file_baton;  /* (local info about current file) */

  /* Has a validation error happened in the middle of an expat
     callback?  signal_expat_bailout() fills in this field, and
     svn_delta_parse() checks this value between calls to expat's
     parser. */
  svn_error_t *validation_error;

  /* The expat parser (wrapped), so that our expat callbacks have the
     power to set themselves to NULL in the case of an error.  (Again,
     this is done by svn_xml_signal_bailout(). */
  svn_xml_parser_t *svn_parser;  

  /* A vcdiff parser, called whenever we receive binary data from
     expat.  Specifically, this is the _current_ vcdiff parser that
     we're using to handle the data within the _current_ file being
     added or replaced.*/
  svn_vcdiff_parser_t *vcdiff_parser;

  /* A hashtable: text-delta-ref-IDs ==> file_batons.  
     Used for "postfix" text-deltas. */
  apr_hash_t *postfix_hash;
  
  /* An in-memory prop-delta, possibly in the process of being
     buffered up */
  struct svn_propdelta_t *current_propdelta;

} svn_xml__digger_t;





/* An in-memory property delta */
typedef struct svn_propdelta_t
{
  enum {
    svn_propdelta_file,
    svn_propdelta_dir,
  } kind;                    /* what kind of object does this
                                prop-delta affect? */

  svn_string_t *entity_name; /* The name of the file, dir, or dirent
                                which is being patched. */
  
  svn_string_t *name;        /* name of property to change */
  svn_string_t *value;       /* new value of property; if NULL, then
                                this property should be deleted. */

} svn_propdelta_t;






/* A object representing a delta-specific XML parser; opaque to
   outside callers, this object is passed to svn_delta_xml_parsebytes(). 

   This is typedef'ed in public "svn_delta.h".
*/

struct svn_delta_xml_parser_t
{
  apr_pool_t *my_pool;            /* the pool which contains the parser */
  svn_xml_parser_t *svn_parser;   /* a standard subversion xml parser */
  svn_xml__digger_t *digger;      /* maintains stack state, etc. */
};







#endif  /* DELTA_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
