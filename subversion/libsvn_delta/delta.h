/*
 * delta.h:  private delta library things
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


/* Return a vcdiff parser object, PARSER.  If we're receiving a
   vcdiff-format byte stream, one block of bytes at a time, we can
   pass each block in succession to svn_vcdiff_parse, with PARSER as
   the other argument.  PARSER keeps track of where we are in the
   stream; each time we've received enough data for a complete
   svn_delta_window_t, we pass it to HANDLER, along with
   HANDLER_BATON.  */
extern svn_vcdiff_parser_t *svn_make_vcdiff_parser (svn_delta_handler_t
                                                    * handler,
                                                    void *handler_baton);

/* Parse another block of bytes in the vcdiff-format stream managed by
   PARSER.  When we've accumulated enough data for a complete window,
   call PARSER's consumer function.  */
extern svn_error_t *svn_vcdiff_parse (svn_vcdiff_parser_t *parser,
                                      const char *buffer,
                                      apr_off_t *len);

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

typedef size_t svn_version_t;   /* Would they ever need to be signed? */

typedef enum svn_XML_t
{
  svn_XML_treedelta = 1,
  svn_XML_new,
  svn_XML_delete,
  svn_XML_replace,
  svn_XML_file,
  svn_XML_dir,
  svn_XML_textdelta,
  svn_XML_propdelta
} svn_XML_t;


typedef struct svn_delta_stackframe_t
{
  svn_XML_t tag;      /* represents an open <tag> */
  void *baton;        /* holds caller data for a particular subdirectory */
  svn_string_t *name; /* if the tag had a "name" attribute attached */
  
  struct svn_delta_stackframe_t *next;
  struct svn_delta_stackframe_t *previous;
  
} svn_delta_stackframe_t;



/* An svn_delta_digger_t is passed as *userData to Expat (and from
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

typedef struct svn_delta_digger_t
{
  /* Pool to do allocations from */
  apr_pool_t *pool;

  /* A mirror of the stack we're getting from the XML structure, used
     for storing XML attributes and for XML validation.  */
  svn_delta_stackframe_t *stack;

  /* Callbacks to use when we discover interesting XML events */
  svn_delta_walk_t *walker;

  /* Userdata structures that we need to keep track of while we parse,
     given to us by either the SVN filesystem or the SVN client */
  void *walk_baton;  /* (global data from our caller) */
  void *dir_baton;   /* (local info about root directory;  local subdir
                         info will be stored in each stackframe structure ) */

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


} svn_delta_digger_t;









/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
