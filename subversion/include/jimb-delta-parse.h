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
