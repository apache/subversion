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
extern svn_error_t *svn_next_delta_window (svn_delta_window_t **window,
                                           svn_delta_stream_t *stream);

/* Free the delta window WINDOW.  */
extern void svn_free_delta_window (svn_delta_window_t *window);

/* A function resembling the POSIX `read' system call --- DATA is some
   opaque structure indicating what we're reading, BUFFER is a buffer
   to hold the data, and *LEN indicates how many bytes to read.  Upon
   return, the function should set *LEN to the number of bytes
   actually read, or zero at the end of the data stream.

   We will need to compute deltas for text drawn from files, memory,
   sockets, and so on; the data may be huge --- too large to read into
   memory at one time.  Using `read'-like functions allows us to
   process the data as we go.  */
typedef svn_error_t *svn_delta_read_fn_t (void *data,
                                          char *buffer,
                                          apr_off_t *len);

/* Set *STREAM to a pointer to a delta stream that will turn the byte
   string from SOURCE into the byte stream from TARGET.

   SOURCE_FN and TARGET_FN are both `read'-like functions, as
   described above.  When we call `svn_next_delta_window' on *STREAM,
   it will call upon its SOURCE_FN and TARGET_FN `read'-like functions
   to gather as much data as it needs.  */
extern svn_error_t *svn_text_delta (svn_delta_stream_t **stream,
                                    svn_delta_read_fn_t *source_fn,
                                    void *source_data,
                                    svn_delta_read_fn_t *target_fn,
                                    void *target_data);

/* Free the delta stream STREAM.  */
extern void svn_free_delta_stream (svn_delta_stream_t *stream);

/* Given a delta stream STREAM, set *READ_FN and *DATA to a `read'-like
   function that will return a VCDIFF-format byte stream.
   (Do we need a `free' function for disposing of DATA somehow?)  */
extern svn_error_t *svn_delta_to_vcdiff (svn_delta_read_fn_t **read_fn,
                                         void **data,
                                         svn_delta_stream_t *stream);

/* Given READ_FN and DATA, a `read'-like function and data pointer that
   yield a VCDIFF-format byte stream, set *STREAM to a pointer to a
   delta stream carrying the data from the VCDIFF stream.  */
extern svn_error_t *svn_vcdiff_to_delta (svn_delta_stream_t **stream,
                                         svn_delta_read_fn_t *read_fn,
                                         void *data);


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


/* Traversing tree deltas.  */

/* A structure of callback functions the parser will invoke as it
   reads in the delta.  */
typedef struct svn_delta_walk_t
{
  /* In the following callback functions:

     - NAME is a single path component, not a full directory name.  The
       caller should use its PARENT_BATON pointers to keep track of
       the current complete subdirectory name, if necessary.

     - WALK_BATON is the baton for the overall delta walk.  It is the
       same value passed to `svn_delta_parse'.

     - PARENT_BATON is the baton for the current directory, whose
       entries we are adding/removing/replacing.

     - If BASE_PATH is non-zero, then BASE_PATH and BASE_VERSION
       indicate the ancestor of the resulting object.

     - PDELTA is a property delta structure, describing either changes
       to the existing object's properties (for the `replace_FOO'
       functions), or a new object's property list as a delta against
       the empty property list (for the `add_FOO' functions).

     So there.  */
       
  /* Remove the directory entry named NAME.  */
  svn_error_t *(*delete) (svn_string_t *name,
			  void *walk_baton, void *parent_baton);

  /* Apply the property delta ENTRY_PDELTA to the property list of the
     directory entry named NAME.  */
  svn_error_t *(*entry_pdelta) (svn_string_t *name,
				void *walk_baton, void *parent_baton,
				svn_pdelta_t *entry_pdelta);

  /* We are going to add a new subdirectory named NAME.  We will use
     the value this callback stores in *CHILD_BATON as the
     PARENT_BATON for further changes in the new subdirectory.  The
     subdirectory is described as a series of changes to the base; if
     BASE_PATH is zero, the changes are relative to an empty directory.  */
  svn_error_t *(*add_directory) (svn_string_t *name,
				 void *walk_baton, void *parent_baton,
				 svn_string_t *base_path,
				 svn_version_t base_version,
				 svn_pdelta_t *pdelta,
				 void **child_baton);

  /* We are going to change the directory entry named NAME to a
     subdirectory.  The callback must store a value in *CHILD_BATON
     that will be used as the PARENT_BATON for subsequent changes in
     this subdirectory.  The subdirectory is described as a series of
     changes to the base; if BASE_PATH is zero, the changes are
     relative to an empty directory.  */
  svn_error_t *(*replace_directory) (svn_string_t *name,
				     void *walk_baton, void *parent_baton,
				     svn_string_t *base_path,
				     svn_version_t base_version,
				     svn_pdelta_t *pdelta,
				     void **child_baton);

  /* We are done processing a subdirectory, whose baton is
     CHILD_BATON.  This lets the caller do any cleanups necessary,
     since CHILD_BATON won't be used any more.  */
  svn_error_t *(*finish_directory) (void *child_baton);

  /* We are going to add a new file named NAME.  TEXT specifies the
     file contents as a text delta versus the base text; if BASE_PATH
     is zero, the changes are relative to the empty file.  */
  svn_error_t *(*add_file) (svn_string_t *name,
			    void *walk_baton, void *parent_baton,
			    svn_string_t *base_path,
			    svn_version_t base_version,
			    svn_pdelta_t *pdelta,
			    svn_delta_stream_t *text);

  /* We are going to change the directory entry named NAME to a file.
     TEXT_DELTA specifies the file contents as a delta relative to the
     base, or the empty file if BASE_PATH is zero.  */
  svn_error_t *(*replace_file) (svn_string_t *name,
				void *walk_baton, void *parent_baton,
				svn_string_t *base_path,
				svn_version_t base_version,
				svn_pdelta_t *pdelta,
				svn_delta_stream_t *text);

} svn_delta_walk_t;

/* Create a delta parser that consumes data from SOURCE_FN and
   SOURCE_BATON, and invokes the callback functions in WALKER as
   appropriate.  CALLER_WALK is a data passthrough for the entire
   traversal.  CALLER_DIR is a data passthrough for the root
   directory; the callbacks can establish new CALLER_DIR values for
   subdirectories.  Use POOL for allocations.  */
extern svn_error_t *svn_delta_parse (svn_delta_read_fn_t *source_fn,
				     void *source_baton,
				     svn_delta_walk_t *walker,
				     void *walk_baton,
				     void *dir_baton,
				     apr_pool_t *pool);

#endif  /* SVN_DELTA_H */



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
