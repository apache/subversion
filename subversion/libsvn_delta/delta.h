/*
 * delta.h:  private delta library things
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


#include "apr_pools.h"
#include "apr_hash.h"
#include "svn_xml.h"

#ifndef SVN_LIBSVN_DELTA_H
#define SVN_LIBSVN_DELTA_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Private interface for text deltas. */

/* Context/baton for building an operation sequence. */

typedef struct svn_txdelta__ops_baton_t {
  int num_ops;                  /* current number of ops */
  int ops_size;                 /* number of ops allocated */
  svn_txdelta_op_t *ops;        /* the operations */

  svn_stringbuf_t *new_data;    /* any new data used by the operations */
} svn_txdelta__ops_baton_t;


/* Insert a delta op into the delta window being built via BUILD_BATON. If
   OPCODE is svn_delta_new, bytes from NEW_DATA are copied into the window
   data and OFFSET is ignored.  Otherwise NEW_DATA is ignored. All
   allocations are performed in POOL. */
void svn_txdelta__insert_op (svn_txdelta__ops_baton_t *build_baton,
                             int opcode,
                             apr_off_t offset,
                             apr_off_t length,
                             const char *new_data,
                             apr_pool_t *pool);


/* Allocate a delta window from POOL. */
svn_txdelta_window_t *
svn_txdelta__make_window (svn_txdelta__ops_baton_t *build_baton,
                          apr_pool_t *pool);


/* Create vdelta window data. Allocate temporary data from POOL. */
void svn_txdelta__vdelta (svn_txdelta__ops_baton_t *build_baton,
                          const char *start,
                          apr_size_t source_len,
                          apr_size_t target_len,
                          apr_pool_t *pool);


/* Compose two delta windows, yielding a third, allocated from POOL.
   Return NULL If WINDOW_B doesn't depend on WINDOW_A (i.e., it's
   already a valid composed window. */
svn_txdelta_window_t *
svn_txdelta__compose_windows (const svn_txdelta_window_t *window_A,
                              const svn_txdelta_window_t *window_B,
                              /*FIXME:*/apr_off_t *sview_offset,
                              apr_pool_t *pool);



/* These are the in-memory tree-delta stackframes; they are used to
 * keep track of a delta's state while the XML stream is being parsed.
 * 
 * The XML representation has certain space optimizations.  For
 * example, if an ancestor is omitted, it means the same path at the
 * same revision (taken from the surrounding delta context).  We may
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
  svn_delta__XML_open,
  svn_delta__XML_file,
  svn_delta__XML_dir,
  svn_delta__XML_textdelta,
  svn_delta__XML_textdeltaref,
  svn_delta__XML_propdelta,
  svn_delta__XML_set

} svn_delta__XML_t;

/* Private xml name definitions */
#define SVN_DELTA__XML_TAG_ADD               "add"
#define SVN_DELTA__XML_TAG_DELETE            "delete"
#define SVN_DELTA__XML_TAG_DELTA_PKG         "delta-pkg"
#define SVN_DELTA__XML_TAG_DIR               "dir"
#define SVN_DELTA__XML_TAG_FILE              "file"
#define SVN_DELTA__XML_TAG_PROP_DELTA        "prop-delta"
#define SVN_DELTA__XML_TAG_OPEN              "open"
#define SVN_DELTA__XML_TAG_SET               "set"
#define SVN_DELTA__XML_TAG_TEXT_DELTA        "text-delta"
#define SVN_DELTA__XML_TAG_TEXT_DELTA_REF    "text-delta-ref"
#define SVN_DELTA__XML_TAG_TREE_DELTA        "tree-delta"

/* Private xml attribute definitions */
#define SVN_DELTA__XML_ATTR_ANCESTOR         "ancestor"
#define SVN_DELTA__XML_ATTR_BASE_PATH        "base-path"
#define SVN_DELTA__XML_ATTR_BASE_REV         "base-rev"
#define SVN_DELTA__XML_ATTR_COPYFROM_PATH    "copyfrom-path"
#define SVN_DELTA__XML_ATTR_COPYFROM_REV     "copyfrom-rev"
#define SVN_DELTA__XML_ATTR_DTD_VER          "dtd-ver"
#define SVN_DELTA__XML_ATTR_ENCODING         "encoding"
#define SVN_DELTA__XML_ATTR_ID               "id"
#define SVN_DELTA__XML_ATTR_NAME             "name"
#define SVN_DELTA__XML_ATTR_TARGET_REV       "target-rev"




typedef struct svn_xml__stackframe_t
{
  svn_delta__XML_t tag;  /* this stackframe represents an open <tag> */

  svn_stringbuf_t *name;    /* if the tag had a "name" attribute attached */
  svn_stringbuf_t *ancestor_path;     /* Explicit, else inherited from parent */ 
  svn_revnum_t ancestor_revision;   /* Explicit, else inherited from parent */ 

  void *baton;           /* holds caller data for the _current_ subdirectory */
  void *file_baton;      /* holds caller data for the _current_ file */

  apr_hash_t *namespace; /* if this frame is a tree-delta, use this
                            hash to detect collisions in the
                            dirent-namespace */

  svn_stringbuf_t *ref_id;  /* if this frame is a postfix text-delta,
                            here is its ID string */
  svn_stringbuf_t *encoding; /* if this frame is a text-delta, here is
                             encoding, if it specified one */

  svn_boolean_t hashed;  /* TRUE iff this is a <file> tag whose
                            file_baton has been stored in a postfix
                            hashtable. */
  
  struct svn_xml__stackframe_t *next;
  struct svn_xml__stackframe_t *previous;
  
} svn_xml__stackframe_t;



/***  An in-memory property delta ***/

typedef struct svn_delta__propdelta_t
{
  enum {
    svn_propdelta_file,
    svn_propdelta_dir
  } kind;                    /* what kind of object does this
                                prop-delta affect? */

  svn_stringbuf_t *entity_name; /* The name of the file, dir, or dirent
                                which is being patched. */
  
  svn_stringbuf_t *name;        /* name of property to change */
  svn_stringbuf_t *value;       /* new value of property; if NULL, then
                                this property should be deleted. */

} svn_delta__propdelta_t;




/* An svn_xml__digger_t is passed as *userData to Expat (and from
 * there to registered callback functions).
 *
 * As the callbacks see various XML elements, they construct
 * digger->stack.  This "stack" keeps track of the XML nesting and
 * aids in the validation of the XML.
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
  const char *base_path;
  svn_revnum_t base_revision;

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

  /* An writable generic stream to parse svndiff data, called whenever
     we receive binary data from expat.  Specifically, this is the
     _current_ handler that we're using for the data within the
     _current_ file being added or opened. */
  svn_stream_t *svndiff_parser;

  /* A hashtable: text-delta-ref-IDs ==> file_batons.  
     Used for "postfix" text-deltas. */
  apr_hash_t *postfix_hash;
  
  /* An in-memory prop-delta, possibly in the process of being
     buffered up */
  struct svn_delta__propdelta_t *current_propdelta;

} svn_xml__digger_t;







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


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_DELTA_H */



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
