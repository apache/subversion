/*
 * svn_delta.h :  structures related to delta-parsing
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



#ifndef SVN_DELTA_H
#define SVN_DELTA_H

#include <apr_pools.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"



/*** Stuff used throughout the delta interface.  ***/

/* A typedef for functions resembling the POSIX `read' system call,
   representing a incoming stream of bytes, in caller-pulls form.

   We will need to compute text deltas for data drawn from files,
   memory, sockets, and so on.  We will need to read tree deltas from
   various sources.  The data may be huge --- too large to read into
   memory at one time.  Using a `read'-like function like this to
   represent the input data allows us to process the data as we go.

   BATON is some opaque structure representing what we're reading.
   Whoever provided the function gets to use BATON however they
   please.

   BUFFER is a buffer to hold the data, and *LEN indicates how many
   bytes to read.  Upon return, the function should set *LEN to the
   number of bytes actually read, or zero at the end of the data
   stream.  */
typedef svn_error_t *svn_delta_read_fn_t (void *baton,
                                          char *buffer,
                                          apr_off_t *len,
                                          apr_pool_t *pool);



/*** Text deltas.  ***/

/* A text delta represents the difference between two strings of
   bytes, the `source' string and the `target' string.  Given a source
   string and a target string, we can compute a text delta; given a
   source string and a delta, we can reconstruct the target string.
   However, note that deltas are not reversible: you cannot always
   reconstruct the source string given the target string and delta.

   Since text deltas can be very large, the interface here allows us
   to produce and consume them in pieces.  Each piece, represented by
   an `svn_txdelta_window_t' structure, describes how to produce the
   next section of the target string.

   To compute a new text delta:

   - We call `svn_txdelta' on the strings we want to compare.  That
     returns us an `svn_txdelta_stream_t' object.

   - We then call `svn_txdelta_next_window' on the stream object
     repeatedly.  Each call returns a new `svn_txdelta_window_t'
     object, which describes the next portion of the target string.
     When `svn_txdelta_next_window' returns zero, we are done building
     the target string.  */


/* An `svn_txdelta_window_t' object describes how to reconstruct a
   contiguous section of the target string.  It contains a series of
   instructions which assemble the new target string text by pulling
   together substrings from:
     - the source file,
     - the target file text so far, and
     - a string of new data (accessible to this window only).  */

/* A single text delta instruction.  */
typedef struct svn_txdelta_op_t {
  enum {
    /* Append the LEN bytes at OFFSET in the source string to the
       target.  It must be the case that 0 <= OFFSET < OFFSET + LEN <=
       size of source string.  */
    svn_txdelta_source,

    /* Append the LEN bytes at OFFSET in the target file, to the
       target.  It must be the case that 0 <= OFFSET < current size of
       the target string.

       However!  OFFSET + LEN may be *beyond* the end of the existing
       target data.  "Where the heck does the text come from, then?"
       If you start at OFFSET, and append LEN bytes one at a time,
       it'll work out --- you're adding new bytes to the end at the
       same rate you're reading them from the middle.  Thus, if your
       current target text is "abcdefgh", and you get an
       `svn_delta_target' instruction whose OFFSET is 6 and whose LEN
       is 7, the resulting string is "abcdefghghghghg".  This trick is
       actually useful in encoding long runs of consecutive
       characters, long runs of CR/LF pairs, etc.  */
    svn_txdelta_target,

    /* Append the LEN bytes at OFFSET in the window's NEW string to
       the target.  It must be the case that 0 <= OFFSET < OFFSET +
       LEN <= length of NEW.  */
    svn_txdelta_new
  } action_code;
  
  apr_off_t offset;
  apr_off_t length;
} svn_txdelta_op_t;


/* How to produce the next stretch of the target string.  */
typedef struct svn_txdelta_window_t {

  /* The number of instructions in this window.  */
  int num_ops;
  
  /* The instructions for this window.  */
  svn_txdelta_op_t *ops;

  /* New data, for use by any `svn_delta_new' instructions.  */
  svn_string_t *new;

  /* The sub-pool that this window is living in, needed for
     svn_txdelta_free_window() */
  apr_pool_t *pool;

} svn_txdelta_window_t;


/* A delta stream --- this is the hat from which we pull a series of
   svn_txdelta_window_t objects, which, taken in order, describe the
   entire target string.  This type is defined within libsvn_delta, and
   opaque outside that library.  */
typedef struct svn_txdelta_stream_t svn_txdelta_stream_t;


/* Set *WINDOW to a pointer to the next window from the delta stream
   STREAM.  When we have completely reconstructed the target string,
   set *WINDOW to zero.  */
extern svn_error_t *svn_txdelta_next_window (svn_txdelta_window_t **window,
                                             svn_txdelta_stream_t *stream);


/* Free the delta window WINDOW.  */
extern void svn_txdelta_free_window (svn_txdelta_window_t *window);
     

/* Set *STREAM to a pointer to a delta stream that will turn the byte
   string from SOURCE into the byte stream from TARGET.

   SOURCE_FN and TARGET_FN are both `read'-like functions; see the
   description of `svn_delta_read_fn_t' above.  When we call
   `svn_txdelta_next_window' on *STREAM, it will call upon SOURCE_FN
   and TARGET_FN to gather as much data as it needs.

   Do any necessary allocation in a sub-pool of POOL.  */
extern svn_error_t *svn_txdelta (svn_txdelta_stream_t **stream,
                                 svn_delta_read_fn_t *source_fn,
                                 void *source_baton,
                                 svn_delta_read_fn_t *target_fn,
                                 void *target_baton,
                                 apr_pool_t *pool);


/* Free the delta stream STREAM.  */
extern void svn_txdelta_free (svn_txdelta_stream_t *stream);



/*** Producing and consuming VCDIFF-format text deltas.  ***/

/* Given a delta stream STREAM, set *READ_FN and *READ_BATON to a
   `read'-like function that will return a VCDIFF-format byte stream.
   Do all allocation for the conversion in POOL.  */
extern svn_error_t *svn_txdelta_to_vcdiff (svn_delta_read_fn_t **read_fn,
                                           void **read_baton,
                                           svn_txdelta_stream_t *stream,
                                           apr_pool_t *pool);
     

/* A function to consume a series of delta windows, in caller-pushes
   form.  This function will typically apply each delta window to
   produce some file, or save it somewhere.  */
typedef svn_error_t *(svn_txdelta_window_handler_t)
                     (svn_txdelta_window_t *window, void *baton);



/*----------------------------------------------------------------
    vcdiff parsing, "push" interface
*/

/* A vcdiff parser object.  */
typedef struct svn_vcdiff_parser_t
{
  /* Once the vcdiff parser has enough data buffered to create a
     "window", it passes this window to the caller's consumer routine.  */
  svn_txdelta_window_handler_t *consumer_func;
  void *consumer_baton;

  /* Pool to create subpools from; each developing window will be a
     subpool */
  apr_pool_t *pool;

  /* The current subpool which contains our current window-buffer */
  apr_pool_t *subpool;

  /* The actual vcdiff data buffer, living within subpool. */
  svn_string_t *buffer;

} svn_vcdiff_parser_t;



/* Return a vcdiff parser object, PARSER, in caller-pushes form.  If
   we're receiving a vcdiff-format byte stream, one block of bytes at
   a time, we can pass each block in succession to `svn_vcdiff_parse',
   with PARSER as the other argument.  PARSER keeps track of where we
   are in the stream; each time we've received enough data for a
   complete svn_txdelta_window_t, we pass it to HANDLER, along with
   HANDLER_BATON.  */
extern svn_vcdiff_parser_t *svn_make_vcdiff_parser
                            (svn_txdelta_window_handler_t *handler,
                             void *handler_baton,
                             apr_pool_t *pool);


/* Parse the LEN bytes at BUFFER as the next block of data in the
   VCDIFF-format stream being parsed by PARSER.  When we've
   accumulated enough data for a complete window, call PARSER's
   HANDLER function.

   If LEN is zero, that indicates the end of the VCDIFF data stream.
   You *must* call svn_vcdiff_parse with LEN == 0 at the end of your
   data stream; otherwise, the parser may still have buffered data it
   hasn't passed to HANDLER yet.  */
extern svn_error_t *svn_vcdiff_parse (svn_vcdiff_parser_t *parser,
                                      const char *buffer,
                                      apr_off_t len);


/*** Traversing tree deltas. ***/


/* In Subversion, we've got various producers and consumers of tree
   deltas.

   In processing a `commit' command:
   - The client examines its working copy data, and produces a tree
     delta describing the changes to be committed.
   - The client networking library consumes that delta, and sends them
     across the wire as an equivalent series of WebDAV requests.
   - The Apache WebDAV module receives those requests and produces a
     tree delta --- hopefully equivalent to the one the client
     produced above.
   - The Subversion server module consumes that delta and commits an
     appropriate transaction to the filesystem.

   In processing an `update' command, the process is reversed:
   - The Subversion server module talks to the filesystem and produces
     a tree delta describing the changes necessary to bring the
     client's working copy up to date.
   - The Apache WebDAV module consumes this delta, and assembles a
     WebDAV reply representing the appropriate changes.
   - The client networking library receives that WebDAV reply, and
     produces a tree delta --- hopefully equivalent to the one the
     Subversion server produced above.
   - The working copy library consumes that delta, and makes the
     appropriate changes to the working copy.

   The simplest approach would be to represent tree deltas as the
   obvious data structure.  To do an update, the server would
   construct a delta structure, the working copy library would apply
   it to the working copy; WebDAV's job would simply be to get the
   structure across the net intact.

   However, we expect that these deltas will occasionally be too large
   to fit in a typical workstation's swap area.  For example, in
   checking out a 200Mb source tree, the entire source tree is
   represented by a single tree delta.  So it's important to handle
   deltas that are too large to fit in swap all at once.

   So instead of representing the tree delta explicitly, we define a
   standard way for a consumer to process each piece of a tree delta
   as soon as the producer creates it.  The `svn_delta_walk_t'
   structure is a set of callback functions to be defined by a delta
   consumer, and invoked by a delta producer.  Each invocation of a
   callback function describes a piece of the delta --- a file's
   contents changing, something being renamed, etc.  */


/* A structure full of callback functions the delta source will invoke
   as it produces the delta.  */
typedef struct svn_delta_walk_t
{
  /* Here's how to use these functions to express a tree delta.

     The delta consumer implements the callback functions described in
     this structure, and the delta producer invokes them.  So the
     caller (producer) is pushing tree delta data at the callee
     (consumer).

     At the start of traversal, the consumer provides two batons:

     - WALK_BATON, a baton global to the entire delta walk.  The
       producer should pass this value as the WALK_BATON argument to
       every callback.

     - DIR_BATON, a baton representing the root directory.  Each
       callback function that adds, deletes, or changes objects in
       some directory takes a directory baton argument representing
       the directory in which the change takes place.  Given the
       initial root baton, the producer can use `add_directory' or
       `replace_directory', which return batons for subdirectories,
       as explained below.

     In the case of `svn_XML_parse', these would be the WALK_BATON
     and DIR_BATON arguments.  Other producers will work differently.

     Most of the callbacks work in the obvious way:

         delete
         add_file        add_directory    
         replace_file    replace_directory

     These all take a PARENT_BATON argument, indicating the directory
     the change takes place in, and a NAME argument, giving the name
     of the file, directory, or directory entry to change.  (NAME is
     always a single path component, never a full directory path.)

     While the consumer provides a directory baton for the root of the
     tree being changed (as explained above), the `add_directory' and
     `replace_directory' callbacks provide batons for other
     directories.  Like the callbacks above, they take a PARENT_BATON
     and a single path component NAME, and then return a new baton for
     the subdirectory being created / modified --- CHILD_BATON.  The
     producer can then use CHILD_BATON to make further changes in that
     subdirectory.

     So, if we already have subdirectories named `foo' and `foo/bar',
     then the producer can create a new file named `foo/bar/baz.c' by
     calling:
        replace_directory (ROOT, "foo") --- yielding a baton F for `foo'
        replace_directory (F, "bar") --- yielding a baton B for `foo/bar'
        add_file (B, "baz.c")
     
     The producer needn't use the batons in any particular order.  It
     would be fine for the producer to then call:
        add_file (ROOT, "qux.c") --- to create `qux.c' in the root
     and then
        add_file (F, "quux.c") --- to create `foo/quux.c'.
     The only restriction is that the producer must call
     `add_directory' or `replace_directory', so as to obtain a baton
     for each directory before making changes there.

     When the producer is finished making changes to a directory, it
     should call `finish_directory'.  This lets the consumer do any
     necessary cleanup, and free the baton's storage.

     The `add_file' and `replace_file' callbacks each return a baton
     for the file being created or changed.  This baton can then be
     passed to `apply_textdelta' to change the file's contents, or
     `change_file_prop' to change the file's properties.  When the
     producer is finished making changes to a file, it should call
     `finish_file', to let the consumer clean up and free the baton.

     The `add_file', `add_directory', `replace_file', and
     `replace_directory' functions all take arguments ANCESTOR_PATH
     and ANCESTOR_VERSION.  If ANCESTOR_PATH is non-zero, then
     ANCESTOR_PATH and ANCESTOR_VERSION indicate the ancestor of the
     resulting object.  */


  /* Deleting things.  */
       
  /* Remove the directory entry named NAME.  */
  svn_error_t *(*delete) (svn_string_t *name,
			  void *walk_baton,
                          void *parent_baton);


  /* Creating and modifying directories.  */
  
  /* We are going to add a new subdirectory named NAME.  We will use
     the value this callback stores in *CHILD_BATON as the
     PARENT_BATON for further changes in the new subdirectory.  The
     subdirectory is described as a series of changes to the base; if
     ANCESTOR_PATH is zero, the changes are relative to an empty
     directory. */
  svn_error_t *(*add_directory) (svn_string_t *name,
				 void *walk_baton,
                                 void *parent_baton,
				 svn_string_t *ancestor_path,
				 svn_vernum_t ancestor_version,
				 void **child_baton);

  /* We are going to change the directory entry named NAME to a
     subdirectory.  The callback must store a value in *CHILD_BATON
     that will be used as the PARENT_BATON for subsequent changes in
     this subdirectory.  The subdirectory is described as a series of
     changes to the base; if ANCESTOR_PATH is zero, the changes are
     relative to an empty directory.  */
  svn_error_t *(*replace_directory) (svn_string_t *name,
				     void *walk_baton,
                                     void *parent_baton,
				     svn_string_t *ancestor_path,
				     svn_vernum_t ancestor_version,
				     void **child_baton);

  /* Change the value of a directory's property.
     - DIR_BATON specifies the directory whose property should change.
     - NAME is the name of the property to change.
     - VALUE is the new value of the property, or zero if the property
     should be removed altogether.  */
  svn_error_t *(*change_dir_prop) (void *walk_baton,
                                   void *dir_baton,
                                   svn_string_t *name,
                                   svn_string_t *value);

  /* Change the value of a directory entry's property.
     - DIR_BATON specifies the directory.
     - ENTRY is the name of the entry in that directory whose property 
       should be changed.
     - NAME is the name of the property to change.
     - VALUE is the new value of the property, or zero if the property
     should be removed altogether.  */
  svn_error_t *(*change_dirent_prop) (void *walk_baton,
                                      void *dir_baton,
                                      svn_string_t *entry,
                                      svn_string_t *name,
                                      svn_string_t *value);

  /* We are done processing a subdirectory, whose baton is DIR_BATON
     (set by add_directory or replace_directory).  We won't be using
     the baton any more, so whatever resources it refers to may now be
     freed.  */
  svn_error_t *(*finish_directory) (void *dir_baton);


  /* Creating and modifying files.  */

  /* We are going to add a new file named NAME.  The callback can
     store a baton for this new file in **FILE_BATON; whatever value
     it stores there will be passed through to apply_textdelta and/or
     apply_propdelta.  */
  svn_error_t *(*add_file) (svn_string_t *name,
			    void *walk_baton,
                            void *parent_baton,
			    svn_string_t *ancestor_path,
			    svn_vernum_t ancestor_version,
                            void **file_baton);

  /* We are going to change the directory entry named NAME to a file.
     The callback can store a baton for this new file in **FILE_BATON;
     whatever value it stores there will be passed through to
     apply_textdelta and/or apply_propdelta.  */
  svn_error_t *(*replace_file) (svn_string_t *name,
				void *walk_baton,
                                void *parent_baton,
				svn_string_t *ancestor_path,
				svn_vernum_t ancestor_version,
                                void **file_baton);

  /* Apply a text delta, yielding the new version of a file.

     FILE_BATON indicates the file we're creating or updating, and the
     ancestor file on which it is based; it is the baton set by some
     prior `add_file' or `replace_file' callback.

     The callback should set *HANDLER to a text delta window
     handler; we will then call *HANDLER on successive text
     delta windows as we recieve them.  The callback should set
     *HANDLER_BATON to the value we should pass as the BATON
     argument to *HANDLER.  */
  svn_error_t *(*apply_textdelta) (void *walk_baton,
                                   void *parent_baton,
                                   void *file_baton, 
                                   svn_txdelta_window_handler_t **handler,
                                   void **handler_baton);

  /* Change the value of a file's property.
     - FILE_BATON specifies the file whose property should change.
     - NAME is the name of the property to change.
     - VALUE is the new value of the property, or zero if the property
     should be removed altogether.  */
  svn_error_t *(*change_file_prop) (void *walk_baton,
                                    void *parent_baton,
                                    void *file_baton,
                                    svn_string_t *name,
                                    svn_string_t *value);

  /* We are done processing a file, whose baton is FILE_BATON (set by
     `add_file' or `replace_file').  We won't be using the baton any
     more, so whatever resources it refers to may now be freed.  */
  svn_error_t *(*finish_file) (void *file_baton);

} svn_delta_walk_t;

/* Create a delta parser that consumes XML data from SOURCE_FN and
   SOURCE_BATON, and invokes the callback functions in WALKER as
   appropriate.  WALK_BATON is a data passthrough for the entire
   traversal.  DIR_BATON is a data passthrough for the root directory;
   the callbacks can establish new DIR_BATON values for
   subdirectories.  Use POOL for allocations.  */
extern svn_error_t *svn_XML_parse (svn_delta_read_fn_t *source_fn,
                                   void *source_baton,
                                   const svn_delta_walk_t *walker,
                                   void *walk_baton,
                                   void *dir_baton,
                                   apr_pool_t *pool);


#endif  /* SVN_DELTA_H */



/* local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

