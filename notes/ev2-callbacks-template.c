/* Use this file to quickly copy/paste handlers for all Ev2 callbacks.

   Please prefix each function to distinguish them from the template,
   for improved ctags behavior.  */

#define UNUSED(x) ((void)(x))


/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_cb(void *baton,
                 const char *relpath,
                 const apr_array_header_t *children,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_cb(void *baton,
            const char *relpath,
            const svn_checksum_t *checksum,
            svn_stream_t *contents,
            apr_hash_t *props,
            svn_revnum_t replaces_rev,
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_symlink_t */
static svn_error_t *
add_symlink_cb(void *baton,
               const char *relpath,
               const char *target,
               apr_hash_t *props,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_cb(void *baton,
              const char *relpath,
              svn_node_kind_t kind,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_alter_directory_t */
static svn_error_t *
alter_directory_cb(void *baton,
                   const char *relpath,
                   svn_revnum_t revision,
                   const apr_array_header_t *children,
                   apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_alter_file_t */
static svn_error_t *
alter_file_cb(void *baton,
              const char *relpath,
              svn_revnum_t revision,
              apr_hash_t *props,
              const svn_checksum_t *checksum,
              svn_stream_t *contents,
              apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_alter_symlink_t */
static svn_error_t *
alter_symlink_cb(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 apr_hash_t *props,
                 const char *target,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_copy_t */
static svn_error_t *
copy_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_move_t */
static svn_error_t *
move_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


static svn_error_t *
make_editor(svn_editor_t **editor,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  static const svn_editor_cb_many_t editor_cbs = {
    add_directory_cb,
    add_file_cb,
    add_symlink_cb,
    add_absent_cb,
    alter_directory_cb,
    alter_file_cb,
    alter_symlink_cb,
    delete_cb,
    copy_cb,
    move_cb,
    complete_cb,
    abort_cb
  };
  struct edit_baton *eb = apr_palloc(result_pool, sizeof(*eb));

  /* ### assign EB members  */

  SVN_ERR(svn_editor_create(editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(*editor, &editor_cbs, scratch_pool));

  return SVN_NO_ERROR;
}
