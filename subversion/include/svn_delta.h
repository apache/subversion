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
#include <apr_file_io.h>
#include <xmlparse.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"


/* Text deltas.  */

/* A text delta represents the difference between two strings of
   bytes, the `source' string and the `target' string.  Given a source
   string and a target string, we can compute a text delta; given a
   source string and a delta, we can reconstruct the target string.
   However, note that deltas are not reversible: you cannot always
   reconstruct the source string given the target string and delta.

   Since text deltas can be very large, we make it possible to
   generate them in pieces.  Each piece, represented by an
   `svn_delta_window_t' structure, describes how to produce the next
   section of the target string.

   We begin delta generation by calling `svn_text_delta' on the
   strings we want to compare.  That returns us an
   `svn_delta_stream_t' object.  We then call `svn_next_delta_window'
   on the stream object repeatedly; each call generates a new
   `svn_delta_window_t' object which describes the next portion of the
   target string.  When `svn_next_delta_window' returns zero, we are
   done building the target string.  */

/* An `svn_delta_window' object describes how to reconstruct a section
   of the target string.  It contains a series of instructions which
   assemble new target string text by pulling together substrings from:
     - the source file,
     - the target file text so far, and
     - a string of new data (accessible to this window only).  */

/* A single text delta instruction.  */
typedef struct svn_delta_op_t {
  enum {
    /* Append the LEN bytes at OFFSET in the source string to the
       target.  It must be the case that 0 <= OFFSET < OFFSET + LEN <=
       size of source string.  */
    svn_delta_source,

    /* Append the LEN bytes at OFFSET in the target file, to the
       target file.  It must be the case that 0 <= OFFSET < current
       size of the target string.

       However!  OFFSET + LEN may be *beyond* the end of the existing
       target data.  "Where the heck does the text come from, then?"
       If you start at OFFSET, and append LEN bytes one at a time,
       it'll work out --- you're adding new bytes to the end at the
       same rate you're reading them from the middle.  Thus, if your
       current target text is "abcdefgh", and you get an
       `svn_delta_target' instruction whose OFFSET is 6 and whose LEN
       is 7, the resulting string is "abcdefghghghghg".  */
    svn_delta_target,

    /* Append the LEN bytes at OFFSET in the window's NEW string to
       the target file.  It must be the case that 0 <= OFFSET < OFFSET
       + LEN <= length of NEW.  */
    svn_delta_new
  } op;
  
  apr_off_t offset;
  apr_off_t length;
} svn_delta_op_t;


/* How to produce the next stretch of the target string.  */
typedef struct svn_delta_window_t {

  /* The number of instructions in this window.  */
  int num_ops;
  
  /* The instructions for this window.  */
  svn_delta_op_t *ops;

  /* New data, for use by any `svn_delta_new' instructions.  */
  svn_string_t *new;

} svn_delta_window_t;


/* A delta stream --- this is the hat from which we pull a series of
   svn_delta_window_t objects, which, taken in order, describe the
   entire target string.  This type is defined within libsvn_delta, and
   opaque outside that library.  */
typedef struct svn_delta_stream_t svn_delta_stream_t;


/* Set *WINDOW to a pointer to the next window from the delta stream
   STREAM.  When we have completely reconstructed the target string,
   set *WINDOW to zero.  */
extern svn_error_t *svn_next_delta_window (svn_delta_stream_t *stream,
                                           svn_delta_window_t **window);

/* Free the delta window WINDOW.  */
extern void svn_free_delta_window (svn_delta_window_t *window);

/* A function resembling the POSIX `read' system call --- DATA is some
   opaque structure indicating what we're reading, BUFFER is a buffer
   to hold the data, and *LEN indicates how many bytes to read.  Upon
   return, the function should set *LEN to the number of bytes
   actually read, or zero at the end of the data stream.  */
typedef svn_error_t *svn_delta_read_fn_t (void *data,
                                          char *buffer,
                                          apr_off_t *len);

/* Set *STREAM to a pointer to a delta stream that will turn the text
   from SOURCE into the text from TARGET.

   SOURCE_FN and TARGET_FN are both functions which act like the Unix
   `read' system call, given SOURCE_DATA or TARGET_DATA as their first
   argument.  We will need to compute deltas for text drawn from
   files, memory, sockets, and so on; the data may be huge --- too
   large to read into memory at one time.  Using `read'-like functions
   allows us to process the data as we go.  When we call
   `svn_next_delta_window' on STREAM, it will call upon its SOURCE and
   TARGET `read'-like functions to gather as much data as it needs.  */
extern svn_error_t *svn_text_delta (svn_delta_read_fn_t *source_fn,
                                    void *source_data,
                                    svn_delta_read_fn_t *target_fn,
                                    void *target_data,
                                    svn_delta_stream_t **stream);

/* Free the delta stream STREAM.  */
extern void svn_free_delta_stream (svn_delta_stream_t *stream);

/* Given a delta stream STREAM, set *READ_FN and *DATA to a `read'-like
   function that will return a VCDIFF-format byte stream.
   (Do we need a `free' function for disposing of DATA somehow?)  */
extern svn_error_t *svn_delta_to_vcdiff (svn_delta_stream_t *stream,
                                         svn_delta_read_fn_t **read_fn,
                                         void **data);

/* Given READ_FN and DATA, a `read'-like function and data pointer that
   yield a VCDIFF-format byte stream, set *STREAM to a pointer to a
   delta stream carrying the data from the VCDIFF stream.  */
extern svn_error_t *svn_vcdiff_to_delta (svn_delta_read_fn_t *read_fn,
                                         void *data,
                                         svn_delta_stream_t **stream);



/* Property deltas.  */

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


typedef struct svn_delta_stackframe_t
{
  enum {                     /* The type of XML object we're representing */
    svn_XML_tree = 1,
    svn_XML_edit,
    svn_XML_content
  } kind;

  enum {                     /* If this is an svn_XML_edit kind, */
    svn_edit_add = 1,        /* this is the type of edit in progress */
    svn_edit_del,
    svn_edit_replace
  } edit_kind;

  enum {                     /* If this is an svn_XML_content kind, */
    svn_content_file = 1,    /* this is the type of content being processed */
    svn_content_dir
  } content_kind;

  svn_string_t *name;        /* Used by svn_XML_edit and svn_XML_content */
  
  /* Used by svn_XML_content only */

  svn_string_t *ancestor_path;
  svn_version_t ancestor_ver;
  svn_boolean_t inside_textdelta;
  svn_boolean_t inside_propdelta;

  /* Our stackframe is a doubly-linked list */

  struct svn_delta_stackframe_t *next;
  struct svn_delta_stackframe_t *previous;

} svn_delta_stackframe_t;



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
  
  svn_delta_stackframe_t *stack;

  /* Caller uses delta context to determine if prop data or text data. */
  svn_error_t *(*data_handler) (struct svn_delta_digger_t *digger,
                                const char *data,
                                int len);

  /* Call handles dirs specially, because might want to create them. 
   * It gets the digger for context, but also the current edit_content
   * because that's a faster way to get this edit. 
   */
  svn_error_t *(*dir_handler) (struct svn_delta_digger_t *digger,
                               svn_delta_stackframe_t *frame);

  /* Caller optionally decides what to do with unrecognized elements. */
  svn_error_t *(*unknown_elt_handler) (struct svn_delta_digger_t *digger,
                                       const char *name);

} svn_delta_digger_t;




/* Creates a parser with the common callbacks and userData registered. */
XML_Parser svn_delta_make_xml_parser (svn_delta_digger_t *diggy);


#endif  /* SVN_DELTA_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
