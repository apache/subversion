/*
 * svn_delta.h :  structures related to delta-parsing
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



#ifndef SVN_DELTA_H
#define SVN_DELTA_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



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
   contiguous section of the target string (the "target view") using a
   specified contiguous region of the source string (the "source
   view").  It contains a series of instructions which assemble the
   new target string text by pulling together substrings from:
     - the source view,
     - the previously constructed portion of the target view,
     - a string of new data contained within the window structure

   The source view must always slide forward from one window to the
   next; that is, neither the beginning nor the end of the source view
   may move to the left as we read from a window stream.  This
   property allows us to apply deltas to non-seekable source streams
   without making a full copy of the source stream.  */

enum svn_delta_action {
    /* Append the LEN bytes at OFFSET in the source view to the
       target.  It must be the case that 0 <= OFFSET < OFFSET + LEN <=
       size of source view.  */
    svn_txdelta_source,

    /* Append the LEN bytes at OFFSET in the target view, to the
       target.  It must be the case that 0 <= OFFSET < current
       position in the target view.

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
       LEN <= length of NEW.  Windows MUST use new data in ascending
       order with no overlap at the moment; svn_txdelta_to_svndiff
       depends on this.  */
    svn_txdelta_new
};

/* A single text delta instruction.  */
typedef struct svn_txdelta_op_t {
  enum svn_delta_action action_code;
  apr_size_t offset;
  apr_size_t length;
} svn_txdelta_op_t;


/* How to produce the next stretch of the target string.  */
typedef struct svn_txdelta_window_t {

  /* The offset and length of the source view for this window.  */
  apr_off_t sview_offset;
  apr_size_t sview_len;

  /* The length of the target view for this window, i.e. the number of
   * bytes which will be reconstructed by the instruction stream.  */
  apr_size_t tview_len;

  /* The number of instructions in this window.  */
  int num_ops;

  /* The instructions for this window.  */
  const svn_txdelta_op_t *ops;

  /* New data, for use by any `svn_delta_new' instructions.  */
  const svn_string_t *new_data;

} svn_txdelta_window_t;


/* A typedef for functions that consume a series of delta windows, for
   use in caller-pushes interfaces.  Such functions will typically
   apply the delta windows to produce some file, or save the windows
   somewhere.  At the end of the delta window stream, you must call
   this function passing zero for the WINDOW argument.  */
typedef svn_error_t * (*svn_txdelta_window_handler_t)
                      (svn_txdelta_window_t *window, void *baton);


/* A delta stream --- this is the hat from which we pull a series of
   svn_txdelta_window_t objects, which, taken in order, describe the
   entire target string.  This type is defined within libsvn_delta, and
   opaque outside that library.  */
typedef struct svn_txdelta_stream_t svn_txdelta_stream_t;


/* Set *WINDOW to a pointer to the next window from the delta stream
   STREAM.  When we have completely reconstructed the target string,
   set *WINDOW to zero.

   The window will be allocated in POOL.  */
svn_error_t *svn_txdelta_next_window (svn_txdelta_window_t **window,
                                      svn_txdelta_stream_t *stream,
                                      apr_pool_t *pool);


/* Return the MD5 digest for the complete fulltext deltified by
   STREAM, or NULL if STREAM has not yet returned its final NULL
   window.  The digest is allocated in the same memory as STREAM.  */
const unsigned char *svn_txdelta_md5_digest (svn_txdelta_stream_t *stream);

/* Set *STREAM to a pointer to a delta stream that will turn the byte
   string from SOURCE into the byte stream from TARGET.

   SOURCE and TARGET are both readable generic streams.  When we call
   `svn_txdelta_next_window' on *STREAM, it will read from SOURCE and
   TARGET to gather as much data as it needs.

   Do any necessary allocation in a sub-pool of POOL.  */
void svn_txdelta (svn_txdelta_stream_t **stream,
                  svn_stream_t *source,
                  svn_stream_t *target,
                  apr_pool_t *pool);


/* Send the contents of STRING to window-handler HANDLER/BATON. This is
   effectively a 'copy' operation, resulting in delta windows that make
   the target equivalent to the value of STRING.

   All temporary allocation is performed in POOL.
*/
svn_error_t *svn_txdelta_send_string (const svn_string_t *string,
                                      svn_txdelta_window_handler_t handler,
                                      void *handler_baton,
                                      apr_pool_t *pool);

/* Send the contents of STREAM to window-handler HANDLER/BATON. This is
   effectively a 'copy' operation, resulting in delta windows that make
   the target equivalent to the stream.

   All temporary allocation is performed in POOL.
*/
svn_error_t *svn_txdelta_send_stream (svn_stream_t *stream,
                                      svn_txdelta_window_handler_t handler,
                                      void *handler_baton,
                                      apr_pool_t *pool);

/* Send the contents of TXSTREAM to window-handler HANDLER/BATON. Windows
   will be extracted from the stream and delivered to the handler.

   All temporary allocation is performed in POOL.
*/
svn_error_t *svn_txdelta_send_txstream (svn_txdelta_stream_t *txstream,
                                        svn_txdelta_window_handler_t handler,
                                        void *handler_baton,
                                        apr_pool_t *pool);


/* Prepare to apply a text delta.  SOURCE is a readable generic stream
   yielding the source data, TARGET is a writable generic stream to
   write target data to, and allocation takes place in a sub-pool of
   POOL.  On return, *HANDLER is set to a window handler function and
   *HANDLER_BATON is set to the value to pass as the BATON argument to
   *HANDLER.  */
void svn_txdelta_apply (svn_stream_t *source,
                        svn_stream_t *target,
                        apr_pool_t *pool,
                        svn_txdelta_window_handler_t *handler,
                        void **handler_baton);



/*** Producing and consuming svndiff-format text deltas.  ***/

/* Prepare to produce an svndiff-format diff from text delta windows.
   OUTPUT is a writable generic stream to write the svndiff data to.
   Allocation takes place in a sub-pool of POOL.  On return, *HANDLER
   is set to a window handler function and *HANDLER_BATON is set to
   the value to pass as the BATON argument to *HANDLER.  */
void svn_txdelta_to_svndiff (svn_stream_t *output,
                             apr_pool_t *pool,
                             svn_txdelta_window_handler_t *handler,
                             void **handler_baton);

/* Return a writable generic stream which will parse svndiff-format
   data into a text delta, invoking HANDLER with HANDLER_BATON
   whenever a new window is ready.  If ERROR_ON_EARLY_CLOSE is TRUE,
   attempting to close this stream before it has handled the entire
   svndiff data set will result in SVN_ERR_SVNDIFF_UNEXPECTED_END,
   else this error condition will be ignored.  */
svn_stream_t *svn_txdelta_parse_svndiff (svn_txdelta_window_handler_t handler,
                                         void *handler_baton,
                                         svn_boolean_t error_on_early_close,
                                         apr_pool_t *pool);



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

   The simplest approach would be to represent tree deltas using the
   obvious data structure.  To do an update, the server would
   construct a delta structure, and the working copy library would
   apply that structure to the working copy; WebDAV's job would simply
   be to get the structure across the net intact.

   However, we expect that these deltas will occasionally be too large
   to fit in a typical workstation's swap area.  For example, in
   checking out a 200Mb source tree, the entire source tree is
   represented by a single tree delta.  So it's important to handle
   deltas that are too large to fit in swap all at once.

   So instead of representing the tree delta explicitly, we define a
   standard way for a consumer to process each piece of a tree delta
   as soon as the producer creates it.  The `svn_delta_edit_fns_t'
   structure is a set of callback functions to be defined by a delta
   consumer, and invoked by a delta producer.  Each invocation of a
   callback function describes a piece of the delta --- a file's
   contents changing, something being renamed, etc.  */

/* A structure full of callback functions the delta source will invoke
   as it produces the delta.  */
typedef struct
{
  /*
     FUNCTION USAGE

     Here's how to use these functions to express a tree delta.

     The delta consumer implements the callback functions described in
     this structure, and the delta producer invokes them.  So the
     caller (producer) is pushing tree delta data at the callee
     (consumer).

     At the start of traversal, the consumer provides EDIT_BATON, a
     baton global to the entire delta edit.  In the case of
     `svn_xml_parse', this would be the EDIT_BATON argument; other
     producers will work differently.  If there is a target revision
     that needs to be set for this operation, the producer should
     called the 'set_target_revision' function at this point.  Next,
     the producer should pass this EDIT_BATON to the `open_root'
     function, to get a baton representing root of the tree being
     edited.

     Most of the callbacks work in the obvious way:

         delete_entry
         add_file           add_directory    
         open_file          open_directory

     Each of these takes a directory baton, indicating the directory
     in which the change takes place, and a PATH argument, giving the
     path (relative to the root of the edit) of the file,
     subdirectory, or directory entry to change. Editors will usually
     want to join this relative path with some base stored in the edit
     baton (e.g. a URL, a location in the OS filesystem).

     Since every call requires a parent directory baton, including
     add_directory and open_directory, where do we ever get our
     initial directory baton, to get things started?  The `open_root'
     function returns a baton for the top directory of the change.  In
     general, the producer needs to invoke the editor's `open_root'
     function before it can get anything of interest done.

     While `open_root' provides a directory baton for the root of
     the tree being changed, the `add_directory' and `open_directory'
     callbacks provide batons for other directories.  Like the
     callbacks above, they take a PARENT_BATON and a relative path
     PATH, and then return a new baton for the subdirectory being
     created / modified --- CHILD_BATON.  The producer can then use
     CHILD_BATON to make further changes in that subdirectory.

     So, if we already have subdirectories named `foo' and `foo/bar',
     then the producer can create a new file named `foo/bar/baz.c' by
     calling:
        open_root () --- yielding a baton ROOT for the top directory
        open_directory (ROOT, "foo") --- yielding a baton F for `foo'
        open_directory (F, "foo/bar") --- yielding a baton B for `foo/bar'
        add_file (B, "foo/bar/baz.c")
     
     When the producer is finished making changes to a directory, it
     should call `close_directory'.  This lets the consumer do any
     necessary cleanup, and free the baton's storage.

     The `add_file' and `open_file' callbacks each return a baton
     for the file being created or changed.  This baton can then be
     passed to `apply_textdelta' to change the file's contents, or
     `change_file_prop' to change the file's properties.  When the
     producer is finished making changes to a file, it should call
     `close_file', to let the consumer clean up and free the baton.

     The `add_file' and `add_directory' functions each take arguments
     COPYFROM_PATH and COPYFROM_REVISION.  If COPYFROM_PATH is
     non-NULL, then COPYFROM_PATH and COPYFROM_REVISION indicate where
     the file or directory should be copied from (to create the file
     or directory being added).


     FUNCTION CALL ORDERING

     There are six restrictions on the order in which the producer
     may use the batons:

     1. The producer may call `open_directory', `add_directory',
        `open_file', `add_file', or `delete_entry' at most once on
        any given directory entry.

     2. The producer may not close a directory baton until it has
        closed all batons for its subdirectories.

     3. When a producer calls `open_directory' or `add_directory',
        it must specify the most recently opened of the currently open
        directory batons.  Put another way, the producer cannot have
        two sibling directory batons open at the same time.

     4. A producer must call `change_dir_prop' on a directory either
        before opening any of the directory's subdirs or after closing
        them, but not in the middle.

     5. When the producer calls `open_file' or `add_file', either:

        (a) The producer must follow with the changes to the file
        (`change_file_prop' and/or `apply_textdelta', as applicable)
        followed by a `close_file' call, before issuing any other file
        or directory calls, or

        (b) The producer must follow with a `change_file_prop' call if
        it is applicable, before issuing any other file or directory
        calls; later, after all directory batons including the root
        have been closed, the producer must issue `apply_textdelta'
        and `close_file' calls.

     6. When the producer calls `apply_textdelta', it must make all of
        the window handler calls (including the NULL window at the
        end) before issuing any other svn_delta_editor_t calls.

     So, the producer needs to use directory and file batons as if it
     is doing a single depth-first traversal of the tree, with the
     exception that the producer may keep file batons open in order to
     make apply_textdelta calls at the end.

     These restrictions make it easier to write a consumer that
     generates an XML-style tree delta.  An XML tree delta mentions
     each directory once, and includes all the changes to that
     directory within the <directory> element.  However, it does allow
     text deltas to appear at the end.


     POOL USAGE

     Many editor functions are invoked multiple times, in a sequence
     determined by the editor "driver". The driver is responsible for
     creating a pool for use on each iteration of the editor function,
     and clearing that pool between each iteration. The driver passes
     the appropriate pool on each function invocation. These "iterative"
     functions are:

         open_directory   open_file
         add_directory    add_file
         change_dir_prop  change_file_prop
         delete_entry

     Based on the requirement of calling the editor functions in a
     depth-first style, it is usually customary for the driver to similar
     nest the pools. However, this is only a safety feature to ensure
     that pools associated with deeper items are always cleared when the
     top-level items are also cleared. The interface does not assume, nor
     require, any particular organization of the pools passed to these
     functions. In fact, if "postfix deltas" are used for files, the file
     pools definitely need to live outside the scope of their parent
     directories' pools.

     Some of the editor functions are called just once, so this interface
     simplifies their signatures by removing a pool argument. It is
     assumed that a pool is reachable through a baton for these functions
     to perform their work. These functions, and pools they will
     typically use are:

         set_target_revision    EDIT_BATON holds a pool
         close_directory        DIR_BATON holds a pool, and should be
                                the DIR_POOL passed to the function
                                which created DIR_BATON (open_root,
                                open_directory, or add_directory)
         apply_textdelta        FILE_BATON holds a pool, and should be
                                the FILE_POOL passed to the function
                                which created FILE_BATON (open_file
                                or add_File)
         close_file             FILE_BATON holds a pool, and should be
                                the FILE_POOL passed to the function
                                which created FILE_BATON (open_file
                                or add_File)
         close_edit             EDIT_BATON holds a pool
         abort_edit             EDIT_BATON holds a pool

     Note that close_directory can be called *before* a file in that
     directory has been closed. That is, the directory's baton is
     closed before the file's baton. The implication is that
     apply_textdelta() and close_file() should not refer to a parent
     directory baton UNLESS the editor has taken precautions to
     allocate it in a pool of the appropriate lifetime (the DIR_POOL
     passed to open_directory and add_directory definitely does not
     have the proper lifetime). In general, it is recommended to simply
     avoid keeping a parent directory baton in a file baton.
  */

  /* Set the target revision for this edit to TARGET_REVISION.  This
     call, if used, should precede all other editor calls. */
  svn_error_t *(*set_target_revision) (void *edit_baton,
                                       svn_revnum_t target_revision);

  /* Set *ROOT_BATON to a baton for the top directory of the change.
     (This is the top of the subtree being changed, not necessarily
     the root of the filesystem.)  Like any other directory baton, the
     producer should call `close_directory' on ROOT_BATON when they're
     done.  And like other open_* calls, the BASE_REVISION here is
     the current revision of the directory (before getting bumped up
     to the new target revision set with set_target_revision).

     Allocations for the returned ROOT_BATON should be performed in
     DIR_POOL. It is also typical to (possibly) save this pool for later
     usage by close_directory. */
  svn_error_t *(*open_root) (void *edit_baton,
                             svn_revnum_t base_revision,
                             apr_pool_t *dir_pool,
                             void **root_baton);


  /* Deleting things.  */
       
  /* Remove the directory entry named PATH, a child of the directory
     represented by PARENT_BATON.  REVISION is used as a sanity check
     to ensure that you are removing the revision of PATH that you
     really think you are.

     All allocations should be performed in POOL. */
  svn_error_t *(*delete_entry) (const char *path,
                                svn_revnum_t revision,
                                void *parent_baton,
                                apr_pool_t *pool);


  /* Creating and modifying directories.  */
  
  /* We are going to add a new subdirectory named PATH.  We will use
     the value this callback stores in *CHILD_BATON as the
     PARENT_BATON for further changes in the new subdirectory.  

     If COPYFROM_PATH is non-NULL, this add has history (i.e., is a
     copy), and the origin of the copy may be recorded as
     COPYFROM_PATH under COPYFROM_REVISION.

     Allocations for the returned CHILD_BATON should be performed in
     DIR_POOL. It is also typical to (possibly) save this pool for later
     usage by close_directory. */
  svn_error_t *(*add_directory) (const char *path,
                                 void *parent_baton,
                                 const char *copyfrom_path,
                                 svn_revnum_t copyfrom_revision,
                                 apr_pool_t *dir_pool,
                                 void **child_baton);

  /* We are going to make changes in a subdirectory (of the directory
     identified by PARENT_BATON). The subdirectory is specified by
     PATH. The callback must store a value in *CHILD_BATON that should
     be used as the PARENT_BATON for subsequent changes in this
     subdirectory.  BASE_REVISION is the current revision of the
     subdirectory.

     Allocations for the returned CHILD_BATON should be performed in
     DIR_POOL. It is also typical to (possibly) save this pool for later
     usage by close_directory. */
  svn_error_t *(*open_directory) (const char *path,
                                  void *parent_baton,
                                  svn_revnum_t base_revision,
                                  apr_pool_t *dir_pool,
                                  void **child_baton);

  /* Change the value of a directory's property.
     - DIR_BATON specifies the directory whose property should change.
     - NAME is the name of the property to change.
     - VALUE is the new value of the property, or NULL if the property
       should be removed altogether.  

     All allocations should be performed in POOL. */
  svn_error_t *(*change_dir_prop) (void *dir_baton,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);

  /* We are done processing a subdirectory, whose baton is DIR_BATON
     (set by add_directory or open_directory).  We won't be using
     the baton any more, so whatever resources it refers to may now be
     freed.  */
  svn_error_t *(*close_directory) (void *dir_baton);


  /* Creating and modifying files.  */

  /* We are going to add a new file named PATH.  The callback can
     store a baton for this new file in **FILE_BATON; whatever value
     it stores there should be passed through to apply_textdelta
     and/or apply_propdelta.

     If COPYFROM_PATH is non-NULL, this add has history (i.e., is a
     copy), and the origin of the copy may be recorded as
     COPYFROM_PATH under COPYFROM_REVISION.

     Allocations for the returned FILE_BATON should be performed in
     FILE_POOL. It is also typical to save this pool for later usage
     by apply_textdelta and possibly close_file. */
  svn_error_t *(*add_file) (const char *path,
                            void *parent_baton,
                            const char *copy_path,
                            svn_revnum_t copy_revision,
                            apr_pool_t *file_pool,
                            void **file_baton);

  /* We are going to make change to a file named PATH, which resides
     in the directory identified by PARENT_BATON.

     The callback can store a baton for this new file in **FILE_BATON;
     whatever value it stores there should be passed through to
     apply_textdelta and/or apply_propdelta.  This file has a current
     revision of BASE_REVISION.

     Allocations for the returned FILE_BATON should be performed in
     FILE_POOL. It is also typical to save this pool for later usage
     by apply_textdelta and possibly close_file. */
  svn_error_t *(*open_file) (const char *path,
                             void *parent_baton,
                             svn_revnum_t base_revision,
                             apr_pool_t *file_pool,
                             void **file_baton);

  /* Apply a text delta, yielding the new revision of a file.

     FILE_BATON indicates the file we're creating or updating, and the
     ancestor file on which it is based; it is the baton set by some
     prior `add_file' or `open_file' callback.

     The callback should set *HANDLER to a text delta window
     handler; we will then call *HANDLER on successive text
     delta windows as we receive them.  The callback should set
     *HANDLER_BATON to the value we should pass as the BATON
     argument to *HANDLER.

     If *HANDLER is set to NULL, then the editor is indicating to the
     driver that it is not interested in receiving information about
     the changes in this file. The driver can use this information to
     avoid computing changes. Note that the editor knows the change
     has occurred (by virtue of this function being invoked), but is
     simply indicating that it doesn't want the details.  */
  svn_error_t *(*apply_textdelta) (void *file_baton, 
                                   svn_txdelta_window_handler_t *handler,
                                   void **handler_baton);

  /* Change the value of a file's property.
     - FILE_BATON specifies the file whose property should change.
     - NAME is the name of the property to change.
     - VALUE is the new value of the property, or NULL if the property
       should be removed altogether.

     All allocations should be performed in POOL. */
  svn_error_t *(*change_file_prop) (void *file_baton,
                                    const char *name,
                                    const svn_string_t *value,
                                    apr_pool_t *pool);

  /* We are done processing a file, whose baton is FILE_BATON (set by
     `add_file' or `open_file').  We won't be using the baton any
     more, so whatever resources it refers to may now be freed.  */
  svn_error_t *(*close_file) (void *file_baton);

  /* All delta processing is done.  Call this, with the EDIT_BATON for
     the entire edit. */
  svn_error_t *(*close_edit) (void *edit_baton);

  /* The editor-driver has decided to bail out.  Allow the editor to
     gracefully clean up things if it needs to. */
  svn_error_t *(*abort_edit) (void *edit_baton);

} svn_delta_editor_t;  



/* ### This structure is deprecated. It is the old format of the
   ### svn_delta_editor_t interface. */
typedef struct svn_delta_edit_fns_t
{
  svn_error_t *(*set_target_revision) (void *edit_baton,
                                       svn_revnum_t target_revision);
  svn_error_t *(*open_root) (void *edit_baton,
                             svn_revnum_t base_revision,
                             void **root_baton);
  svn_error_t *(*delete_entry) (svn_stringbuf_t *name,
                                svn_revnum_t revision,
                                void *parent_baton);
  svn_error_t *(*add_directory) (svn_stringbuf_t *name,
                                 void *parent_baton,
                                 svn_stringbuf_t *copyfrom_path,
                                 svn_revnum_t copyfrom_revision,
                                 void **child_baton);
  svn_error_t *(*open_directory) (svn_stringbuf_t *name,
                                  void *parent_baton,
                                  svn_revnum_t base_revision,
                                  void **child_baton);
  svn_error_t *(*change_dir_prop) (void *dir_baton,
                                   svn_stringbuf_t *name,
                                   svn_stringbuf_t *value);
  svn_error_t *(*close_directory) (void *dir_baton);
  svn_error_t *(*add_file) (svn_stringbuf_t *name,
                            void *parent_baton,
                            svn_stringbuf_t *copy_path,
                            svn_revnum_t copy_revision,
                            void **file_baton);
  svn_error_t *(*open_file) (svn_stringbuf_t *name,
                             void *parent_baton,
                             svn_revnum_t base_revision,
                             void **file_baton);
  svn_error_t *(*apply_textdelta) (void *file_baton, 
                                   svn_txdelta_window_handler_t *handler,
                                   void **handler_baton);
  svn_error_t *(*change_file_prop) (void *file_baton,
                                    svn_stringbuf_t *name,
                                    svn_stringbuf_t *value);
  svn_error_t *(*close_file) (void *file_baton);
  svn_error_t *(*close_edit) (void *edit_baton);
  svn_error_t *(*abort_edit) (void *edit_baton);

} svn_delta_edit_fns_t;

/* ### temporary function for wrapping an svn_delta_editor_t interface
   ### into the old-form svn_delta_edit_fns_t interface. this wrapping
   ### function enables an old-style editor driver to drive a new-style
   ### editor. */
void svn_delta_compat_wrap (const svn_delta_edit_fns_t **wrapper_editor,
                            void **wrapper_baton,
                            const svn_delta_editor_t *editor,
                            void *edit_baton,
                            apr_pool_t *pool);


/* Return a default delta editor template, allocated in POOL.
 *
 * The editor functions in the template do only the most basic
 * baton-swapping: each editor function that produces a baton does so
 * by copying its incoming baton into the outgoing baton reference.
 *
 * This editor is not intended to be useful by itself, but is meant to
 * be the basis for a useful editor.  After getting a default editor,
 * you substitute in your own implementations for the editor functions
 * you care about.  The ones you don't care about, you don't have to
 * implement -- you can rely on the template's implementation to
 * safely do nothing of consequence.
 */
svn_delta_editor_t *svn_delta_default_editor (apr_pool_t *pool);

/* ### create a default editor for the old-style editor */
svn_delta_edit_fns_t *svn_delta_old_default_editor (apr_pool_t *pool);



/* Compose EDITOR_1 and its baton with EDITOR_2 and its baton.
 *
 * Returns a new editor in E which each function FUN calls
 * EDITOR_1->FUN and then EDITOR_2->FUN, with the corresponding batons.
 * 
 * If EDITOR_1->FUN returns error, that error is returned from E->FUN
 * and EDITOR_2->FUN is never called; otherwise E->FUN's return value
 * is the same as EDITOR_2->FUN's.
 *
 * If an editor function is null, it is simply never called, and this
 * is not an error.
 */
void
svn_delta_compose_editors (const svn_delta_editor_t **new_editor,
                           void **new_edit_baton,
                           const svn_delta_editor_t *editor_1,
                           void *edit_baton_1,
                           const svn_delta_editor_t *editor_2,
                           void *edit_baton_2,
                           apr_pool_t *pool);


/* Compose BEFORE_EDITOR, BEFORE_EDIT_BATON with MIDDLE_EDITOR,
 * MIDDLE_EDIT_BATON, then compose the result with AFTER_EDITOR,
 * AFTER_EDIT_BATON, all according to the conventions of
 * svn_delta_compose_old_editors().  Return the resulting editor in
 * *NEW_EDITOR, *NEW_EDIT_BATON.
 *
 * If either BEFORE_EDITOR or AFTER_EDITOR is null, that editor will
 * simply not be included in the composition.  It is advised, though
 * not required, that a null editor pair with a null baton, and a
 * non-null editor with a non-null baton.
 *
 * MIDDLE_EDITOR must not be null.  I'm not going to tell you what
 * happens if it is.
 */
void svn_delta_wrap_editor (const svn_delta_editor_t **new_editor,
                            void **new_edit_baton,
                            const svn_delta_editor_t *before_editor,
                            void *before_edit_baton,
                            const svn_delta_editor_t *middle_editor,
                            void *middle_edit_baton,
                            const svn_delta_editor_t *after_editor,
                            void *after_edit_baton,
                            apr_pool_t *pool);





/* Creates an editor which outputs XML delta streams to OUTPUT.  On
   return, *EDITOR and *EDITOR_BATON will be set to the editor and its
   associate baton.  The editor's memory will live in a sub-pool of
   POOL. */
svn_error_t *
svn_delta_get_xml_editor (svn_stream_t *output,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          apr_pool_t *pool);



/* An opaque object that represents a Subversion Delta XML parser. */

typedef struct svn_delta_xml_parser_t svn_delta_xml_parser_t;

/* Given a precreated svn_delta_edit_fns_t EDITOR, return a custom xml
   PARSER that will call into it (and feed EDIT_BATON to its
   callbacks.)  Additionally, this XML parser will use BASE_PATH and
   BASE_REVISION as default "context variables" when computing ancestry
   within a tree-delta. */
svn_error_t *svn_delta_make_xml_parser (svn_delta_xml_parser_t **parser,
                                        const svn_delta_edit_fns_t *editor,
                                        void *edit_baton,
                                        const char *base_path, 
                                        svn_revnum_t base_revision,
                                        apr_pool_t *pool);


/* Destroy an svn_delta_xml_parser_t when finished with it. */
void svn_delta_free_xml_parser (svn_delta_xml_parser_t *parser);


/* Push LEN bytes of xml data in BUFFER at SVN_XML_PARSER.  As xml is
   parsed, EDITOR callbacks will be executed (using context variables
   and batons that were used to create the parser.)  If this is the
   final parser "push", ISFINAL must be set to true (so that both
   expat and local cleanup can occur). */
svn_error_t *
svn_delta_xml_parsebytes (const char *buffer, apr_size_t len, int isFinal, 
                          svn_delta_xml_parser_t *svn_xml_parser);


/* Reads an XML stream from SOURCE using expat internally, validating
   the XML as it goes (according to Subversion's own tree-delta DTD).
   Whenever an interesting event happens, it calls a caller-specified
   callback routine from EDITOR.  
   
   Once called, it retains control and "pulls" data from SOURCE
   until either the stream runs out or an error occurs. */
svn_error_t *svn_delta_xml_auto_parse (svn_stream_t *source,
                                       const svn_delta_edit_fns_t *editor,
                                       void *edit_baton,
                                       const char *base_path,
                                       svn_revnum_t base_revision,
                                       apr_pool_t *pool);





/* A callback vtable invoked by our diff-editors, as they receive
   diffs from the server.  'svn diff' and 'svn merge' both implement
   their own versions of this table. */
typedef struct svn_diff_callbacks_t
{
  /* A file PATH has changed.  The changes can be seen by comparing
     TMPFILE1 and TMPFILE2, which represent REV1 and REV2 of the file,
     respectively. */
  svn_error_t *(*file_changed) (const char *path,
                                const char *tmpfile1,
                                const char *tmpfile2,
                                svn_revnum_t rev1,
                                svn_revnum_t rev2,
                                void *diff_baton);

  /* A file PATH was added.  The contents can be seen by comparing
     TMPFILE1 and TMPFILE2. */
  svn_error_t *(*file_added) (const char *path,
                              const char *tmpfile1,
                              const char *tmpfile2,
                              void *diff_baton);
  
  /* A file PATH was deleted.  The [loss of] contents can be seen by
     comparing TMPFILE1 and TMPFILE2. */
  svn_error_t *(*file_deleted) (const char *path,
                                const char *tmpfile1,
                                const char *tmpfile2,
                                void *diff_baton);
  
  /* A directory PATH was added. */
  svn_error_t *(*dir_added) (const char *path,
                             void *diff_baton);
  
  /* A directory PATH was deleted. */
  svn_error_t *(*dir_deleted) (const char *path,
                               void *diff_baton);
  
  /* A list of property changes (PROPCHANGES) was applied to PATH.
     The array is a list of (svn_prop_t) structures. 
     The original list of properties is provided in ORIGINAL_PROPS. */
  svn_error_t *(*props_changed) (const char *path,
                                 const apr_array_header_t *propchanges,
                                 apr_hash_t *original_props,
                                 void *diff_baton);

} svn_diff_callbacks_t;



#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DELTA_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

