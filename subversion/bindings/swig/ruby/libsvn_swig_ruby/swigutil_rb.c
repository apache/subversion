#include "swigutil_rb.h"
#include <st.h>

#if SVN_SWIG_VERSION >= 103025
#  include <swiglabels.swg>
#endif
#include <ruby/rubyhead.swg>
#include <swigrun.swg>
#if SVN_SWIG_VERSION < 103025
#  include <common.swg>
#endif
#include <ruby/rubydef.swg>
#if SVN_SWIG_VERSION >= 103025
#  include <runtime.swg>
#endif

#define POOL_P(obj) (RTEST(rb_obj_is_kind_of(obj, rb_svn_core_pool())))
#define CONTEXT_P(obj) (RTEST(rb_obj_is_kind_of(obj, rb_svn_client_context())))

static VALUE mSvn = Qnil;
static VALUE mSvnCore = Qnil;
static VALUE cSvnError = Qnil;
static VALUE cSvnCoreStream = Qnil;
static VALUE cSvnCorePool = Qnil;
static VALUE mSvnClient = Qnil;
static VALUE cSvnClientContext = Qnil;

#define DEFINE_ID(key, name)                    \
static ID id_ ## key = 0;                       \
static ID                                       \
rb_id_ ## key (void)                            \
{                                               \
  if (!id_ ## key) {                            \
    id_ ## key = rb_intern(name);               \
  }                                             \
  return id_ ## key;                            \
}

DEFINE_ID(code, "code")
DEFINE_ID(message, "message")
DEFINE_ID(call, "call")
DEFINE_ID(read, "read")
DEFINE_ID(write, "write")
DEFINE_ID(eqq, "===")
DEFINE_ID(baton, "baton")
DEFINE_ID(new, "new")
DEFINE_ID(new_corresponding_error, "new_corresponding_error")
DEFINE_ID(set_target_revision, "set_target_revision")
DEFINE_ID(open_root, "open_root")
DEFINE_ID(delete_entry, "delete_entry")
DEFINE_ID(add_directory, "add_directory")
DEFINE_ID(open_directory, "open_directory")
DEFINE_ID(change_dir_prop, "change_dir_prop")
DEFINE_ID(close_directory, "close_directory")
DEFINE_ID(absent_directory, "absent_directory")
DEFINE_ID(add_file, "add_file")
DEFINE_ID(open_file, "open_file")
DEFINE_ID(apply_textdelta, "apply_textdelta")
DEFINE_ID(change_file_prop, "change_file_prop")
DEFINE_ID(absent_file, "absent_file")
DEFINE_ID(close_file, "close_file")
DEFINE_ID(close_edit, "close_edit")
DEFINE_ID(abort_edit, "abort_edit")
DEFINE_ID(__pool__, "__pool__")
DEFINE_ID(__pools__, "__pools__")
DEFINE_ID(name, "name")
DEFINE_ID(swig_type_regex, "swig_type_regex")

typedef void *(*r2c_func)(VALUE value, void *ctx, apr_pool_t *pool);
typedef VALUE (*c2r_func)(void *value, void *ctx);
typedef struct hash_to_apr_hash_data_t
{
  apr_hash_t *apr_hash;
  r2c_func func;
  void *ctx;
  apr_pool_t *pool;
} hash_to_apr_hash_data_t;


static VALUE
rb_ary_aref1(VALUE ary, VALUE arg)
{
  VALUE args[1] = {arg};
  return rb_ary_aref(1, args, ary);
}

static VALUE
rb_ary_aref_n(VALUE ary, int n)
{
  return rb_ary_aref1(ary, INT2NUM(n));
}


static VALUE
rb_svn(void)
{
  if (NIL_P(mSvn)) {
    mSvn = rb_const_get(rb_cObject, rb_intern("Svn"));
  }
  return mSvn;
}

static VALUE
rb_svn_core(void)
{
  if (NIL_P(mSvnCore)) {
    mSvnCore = rb_const_get(rb_svn(), rb_intern("Core"));
  }
  return mSvnCore;
}

static VALUE
rb_svn_error(void)
{
  if (NIL_P(cSvnError)) {
    cSvnError = rb_const_get(rb_svn(), rb_intern("Error"));
  }
  return cSvnError;
}

static VALUE
rb_svn_core_stream(void)
{
  if (NIL_P(cSvnCoreStream)) {
    cSvnCoreStream = rb_const_get(rb_svn_core(), rb_intern("Stream"));
  }
  return cSvnCoreStream;
}

static VALUE
rb_svn_core_pool(void)
{
  if (NIL_P(cSvnCorePool)) {
    cSvnCorePool = rb_const_get(rb_svn_core(), rb_intern("Pool"));
    rb_ivar_set(cSvnCorePool, rb_id___pools__(), rb_hash_new());
  }
  return cSvnCorePool;
}

static VALUE
rb_svn_pool_holder(void)
{
  return rb_ivar_get(rb_svn_core_pool(), rb_id___pools__());
}

VALUE
svn_swig_rb_svn_error_new(VALUE code, VALUE message, VALUE file, VALUE line)
{
  return rb_funcall(rb_svn_error(),
                    rb_id_new_corresponding_error(),
                    4, code, message, file, line);
}

void
svn_swig_rb_handle_svn_error(svn_error_t *error)
{
  VALUE error_code = INT2NUM(error->apr_err);
  VALUE message;
  VALUE file = Qnil;
  VALUE line = Qnil;

  if (error->file)
    file = rb_str_new2(error->file);
  if (error->line)
    line = LONG2NUM(error->line);
  
  message = rb_str_new2(error->message ? error->message : "");
  
  while (error->child) {
    error = error->child;
    if (error->message) {
      rb_str_concat(message, rb_str_new2("\n"));
      rb_str_concat(message, rb_str_new2(error->message));
    }
  }
  svn_error_clear(error);
  
  rb_exc_raise(svn_swig_rb_svn_error_new(error_code, message, file, line));
}

static VALUE
rb_svn_client(void)
{
  if (NIL_P(mSvnClient)) {
    mSvnClient = rb_const_get(rb_svn(), rb_intern("Client"));
  }
  return mSvnClient;
}

static VALUE
rb_svn_client_context(void)
{
  if (NIL_P(cSvnClientContext)) {
    cSvnClientContext = rb_const_get(rb_svn_client(), rb_intern("Context"));
  }
  return cSvnClientContext;
}


static VALUE inited = Qnil;
/* C -> Ruby */
static VALUE
c2r_swig_type(void *value, void *ctx)
{
  swig_type_info *info;

  if (NIL_P(inited)) {
    SWIG_InitRuntime();
    inited = Qtrue;
  }
  
  info = SWIG_TypeQuery((char *)ctx);
  if (info) {
    return SWIG_NewPointerObj(value, info, 0);
  } else {
    rb_raise(rb_eArgError, "invalid SWIG type: %s", (char *)ctx);
  }
}

static VALUE
c2r_string(void *value, void *ctx)
{
  if (value) {
    return rb_str_new2((const char *)value);
  } else {
    return Qnil;
  }
}

static VALUE
c2r_string2(const char *cstr)
{
  return c2r_string((void *)cstr, NULL);
}

static VALUE
c2r_long(void *value, void *ctx)
{
  return INT2NUM(*(long *)value);
}

static VALUE
c2r_svn_string(void *value, void *ctx)
{
  const svn_string_t *s = (svn_string_t *)value;

  return c2r_string2(s->data);
}


/* Ruby -> C */
static void *
r2c_string(VALUE value, void *ctx, apr_pool_t *pool)
{
  return (void *)apr_pstrdup(pool, StringValuePtr(value));
}

static void *
r2c_svn_string(VALUE value, void *ctx, apr_pool_t *pool)
{
  return (void *)svn_string_create(StringValuePtr(value), pool);
}

static void *
r2c_swig_type(VALUE value, void *ctx, apr_pool_t *pool)
{
  void **result = NULL;
  result = apr_palloc(pool, sizeof(void *));
  SWIG_ConvertPtr(value, result, SWIG_TypeQuery((char *)ctx), 1);
  return *result;
}



/* apr_array_t -> Ruby Array */
#define DEFINE_APR_ARRAY_TO_ARRAY(return_type, name, conv, amp, type, ctx)  \
return_type                                                                 \
name(const apr_array_header_t *apr_ary)                                     \
{                                                                           \
  VALUE ary = rb_ary_new();                                                 \
  int i;                                                                    \
                                                                            \
  for (i = 0; i < apr_ary->nelts; i++) {                                    \
    rb_ary_push(ary, conv((void *)amp(APR_ARRAY_IDX(apr_ary, i, type)),     \
                          ctx));                                            \
  }                                                                         \
                                                                            \
  return ary;                                                               \
}

/*
DEFINE_APR_ARRAY_TO_ARRAY(VALUE, svn_swig_rb_apr_array_to_rb_array_rev,
                          INT2NUM, svn_revnum_t)
*/
DEFINE_APR_ARRAY_TO_ARRAY(VALUE, svn_swig_rb_apr_array_to_array_string,
                          c2r_string, , const char *, NULL)
DEFINE_APR_ARRAY_TO_ARRAY(VALUE, svn_swig_rb_apr_array_to_array_svn_string,
                          c2r_svn_string, &, svn_string_t, NULL)
DEFINE_APR_ARRAY_TO_ARRAY(static VALUE, c2r_commit_item_array,
                          c2r_swig_type, &, svn_client_commit_item_t,
                          (void *)"svn_client_commit_item_t *")
DEFINE_APR_ARRAY_TO_ARRAY(VALUE, svn_swig_rb_apr_array_to_array_prop,
                          c2r_swig_type, &, svn_prop_t,
                          (void *)"svn_prop_t *")
DEFINE_APR_ARRAY_TO_ARRAY(VALUE, svn_swig_rb_apr_array_to_array_svn_rev,
                          c2r_long, &, svn_revnum_t, NULL)
     


/* Ruby Array -> apr_array_t */
#define DEFINE_ARRAY_TO_APR_ARRAY(type, name, converter, context) \
apr_array_header_t *                                              \
name(VALUE array, apr_pool_t *pool)                               \
{                                                                 \
  int i, len;                                                     \
  apr_array_header_t *apr_ary;                                    \
                                                                  \
  Check_Type(array, T_ARRAY);                                     \
  len = RARRAY(array)->len;                                       \
  apr_ary = apr_array_make(pool, len, sizeof(type));              \
  apr_ary->nelts = len;                                           \
  for (i = 0; i < len; i++) {                                     \
    VALUE value;                                                  \
    type val;                                                     \
    value = rb_ary_aref_n(array, i);                              \
    val = converter(value, context, pool);                        \
    APR_ARRAY_IDX(apr_ary, i, type) = val;                        \
  }                                                               \
  return apr_ary;                                                 \
}

DEFINE_ARRAY_TO_APR_ARRAY(const char *, svn_swig_rb_strings_to_apr_array,
                          r2c_string, NULL)
DEFINE_ARRAY_TO_APR_ARRAY(svn_auth_provider_object_t *,
                          svn_swig_rb_array_to_auth_provider_object_apr_array,
                          r2c_swig_type, (void *)"svn_auth_provider_object_t *")
DEFINE_ARRAY_TO_APR_ARRAY(svn_prop_t *,
                          svn_swig_rb_array_to_apr_array_prop,
                          r2c_swig_type, (void *)"svn_prop_t *")



/* apr_hash_t -> Ruby Hash */
static VALUE
c2r_hash(apr_hash_t *hash,
         c2r_func func,
         void *ctx)
{
  apr_hash_index_t *hi;
  VALUE r_hash = rb_hash_new();

  for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi)) {
    const void *key;
    void *val;
    VALUE v = Qnil;
      
    apr_hash_this(hi, &key, NULL, &val);
    if (val) {
      v = (*func)(val, ctx);
    }
    rb_hash_aset(r_hash, c2r_string2(key), v);
  }
    
  return r_hash;
}

VALUE
svn_swig_rb_apr_hash_to_hash_string(apr_hash_t *hash)
{
  return c2r_hash(hash, c2r_string, NULL);
}

VALUE
svn_swig_rb_apr_hash_to_hash_svn_string(apr_hash_t *hash)
{
  return c2r_hash(hash, c2r_svn_string, NULL);
}

VALUE
svn_swig_rb_apr_hash_to_hash_swig_type(apr_hash_t *hash, const char *type_name)
{
  return c2r_hash(hash, c2r_swig_type, (void *)type_name);
}



/* Ruby Hash -> apr_hash_t */
static int
r2c_hash_i(VALUE key, VALUE value, hash_to_apr_hash_data_t *data)
{
  if (key != Qundef) {
    void *val = data->func(value, data->ctx, data->pool);
    apr_hash_set(data->apr_hash,
                 apr_pstrdup(data->pool, StringValuePtr(key)),
                 APR_HASH_KEY_STRING,
                 val);
  }
  return ST_CONTINUE;
}

static apr_hash_t *
r2c_hash(VALUE hash, r2c_func func, void *ctx, apr_pool_t *pool)
{
  if (NIL_P(hash)) {
    return NULL;
  } else {
    apr_hash_t *apr_hash;
    hash_to_apr_hash_data_t data = {
      NULL,
      func,
      ctx,
      pool
    };

    apr_hash = apr_hash_make(pool);
    data.apr_hash = apr_hash;
    rb_hash_foreach(hash, r2c_hash_i, (VALUE)&data);
    
    return apr_hash;
  }
}


apr_hash_t *
svn_swig_rb_hash_to_apr_hash_string(VALUE hash, apr_pool_t *pool)
{
  return r2c_hash(hash, r2c_string, NULL, pool);
}

apr_hash_t *
svn_swig_rb_hash_to_apr_hash_svn_string(VALUE hash, apr_pool_t *pool)
{
  return r2c_hash(hash, r2c_svn_string, NULL, pool);
}

apr_hash_t *
svn_swig_rb_hash_to_apr_hash_swig_type(VALUE hash, const char *typename, apr_pool_t *pool)
{
  return r2c_hash(hash, r2c_swig_type, (void *)typename, pool);
}


/*
static VALUE
convert_svn_client_commit_item_t(void *value, void *ctx)
{
  VALUE ary;
  VALUE path, kind, url, rev, cf_url, state;
  svn_client_commit_item_t *item = value;

  ary = rb_ary_new2(6);
  
  path = c2r_string2(item->path);
  url = c2r_string2(item->url);
  cf_url = c2r_string2(item->copyfrom_url);
        
  kind = INT2NUM(item->kind);
  rev = INT2NUM(item->revision);
  state = INT2NUM(item->state_flags);

  if (path && kind && url && rev && cf_url && state) {
    rb_ary_push(ary, path);
    rb_ary_push(ary, kind);
    rb_ary_push(ary, url);
    rb_ary_push(ary, rev);
    rb_ary_push(ary, cf_url);
    rb_ary_push(ary, state);
    return ary;
  } else {
    return Qnil;
  }
}
*/


static VALUE
rb_get_pool(VALUE self)
{
  return rb_ivar_get(self, rb_id___pool__());
}

static VALUE
rb_pools(VALUE self)
{
  VALUE pools = rb_ivar_get(self, rb_id___pools__());

  if (NIL_P(pools)) {
    pools = rb_hash_new();
    rb_ivar_set(self, rb_id___pools__(), pools);
  }
  
  return pools;
}

static void
rb_set_pool(VALUE self, VALUE pool)
{
  if (NIL_P(pool)) {
    VALUE old_pool = rb_ivar_get(self, rb_id___pool__());
    rb_hash_aset(rb_pools(self), rb_obj_id(old_pool), old_pool);
    rb_ivar_set(self, rb_id___pool__(), Qnil);
  } else {
    if (NIL_P(rb_ivar_get(self, rb_id___pool__()))) {
      rb_ivar_set(self, rb_id___pool__(), pool);
    } else {
      rb_hash_aset(rb_pools(self), rb_obj_id(pool), pool);
    }
  }
}

static VALUE
rb_pool_new(void)
{
  return rb_funcall(rb_svn_core_pool(), rb_id_new(), 0);
}

static VALUE swig_type_re = Qnil;

static VALUE
swig_type_regex(void)
{
  if (NIL_P(swig_type_re)) {
    char reg_str[] = "\\A(?:SWIG|Svn::Ext)::";
    swig_type_re = rb_reg_new(reg_str, strlen(reg_str), 0);
    rb_ivar_set(rb_svn(), rb_id_swig_type_regex(), swig_type_re);
  }
  return swig_type_re;
}

static VALUE
find_swig_type_object(int num, VALUE *objects)
{
  VALUE re = swig_type_regex();
  int i;

  for (i = 0; i < num; i++) {
    if (RTEST(rb_reg_match(re,
                           rb_funcall(rb_obj_class(objects[i]),
                                      rb_id_name(),
                                      0)))) {
      return objects[i];
    }
  }
  
  return Qnil;
}

void
svn_swig_rb_get_pool(int argc, VALUE *argv, VALUE self,
                     VALUE *rb_pool, apr_pool_t **pool)
{
  *rb_pool = Qnil;
  
  if (argc > 0) {
    if (POOL_P(argv[argc - 1])) {
      *rb_pool = argv[argc - 1];
    }
    argc -= 1;
  }

  if (NIL_P(rb_pool)) {
    VALUE target = find_swig_type_object(argc, argv);
    *rb_pool = rb_get_pool(target);
    if (!POOL_P(*rb_pool)) {
      *rb_pool = Qnil;
    }
  }
  
  if (NIL_P(*rb_pool)) {
    *rb_pool = rb_pool_new();
    {
      VALUE target = find_swig_type_object(argc, argv);
      if (!NIL_P(target)) {
        rb_set_pool(target, *rb_pool);
      }
    }
  }
  
  SWIG_ConvertPtr(*rb_pool, (void **)pool, SWIG_TypeQuery("apr_pool_t *"), 1);
}

static VALUE
rb_set_pool_if_swig_type_object(VALUE target, VALUE pool)
{
  VALUE targets[1] = {target};
  
  if (!NIL_P(find_swig_type_object(1, targets))) {
    rb_set_pool(target, pool);
  }

  return Qnil;
}

void
svn_swig_rb_set_pool(VALUE target, VALUE pool)
{
  if (NIL_P(target)) {
    return;
  }
    
  if (!RTEST(rb_obj_is_kind_of(target, rb_cArray))) {
    target = rb_ary_new3(1, target);
  }

  rb_iterate(rb_each, target, rb_set_pool_if_swig_type_object, pool);
}

void
svn_swig_rb_push_pool(VALUE pool)
{
  rb_hash_aset(rb_svn_pool_holder(), rb_obj_id(pool), pool);
}

void
svn_swig_rb_pop_pool(VALUE pool)
{
  rb_hash_delete(rb_svn_pool_holder(), rb_obj_id(pool));
}


static VALUE
callback(VALUE info)
{
  if (RTEST(rb_equal((ID)rb_ary_aref_n(info, 1), rb_id_call()))) {
    /* rb_p(rb_ary_aref_n(info, 0)); */
    if (TYPE(rb_ary_aref_n(info, 0)) == T_STRING) {
      rb_p(rb_funcall(rb_mKernel, rb_intern("caller"), 0));
    }
  }
  return rb_apply(rb_ary_aref_n(info, 0),
                  (ID)rb_ary_aref_n(info, 1),
                  rb_ary_aref1(info,
                               rb_range_new(INT2NUM(2),
                                            INT2NUM(-1),
                                            FALSE)));
}


static VALUE
callback_rescue(VALUE error)
{
  svn_error_t **err = (svn_error_t **)error;
  VALUE message;
  
  message = rb_funcall(ruby_errinfo, rb_id_message(), 0);
  *err = svn_error_create(NUM2INT(rb_funcall(ruby_errinfo, rb_id_code(), 0)),
                          NULL,
                          StringValuePtr(message));
  return Qnil;
}


typedef struct {
  VALUE editor;
  VALUE baton;
} item_baton;

static item_baton *
make_baton(apr_pool_t *pool, VALUE editor, VALUE baton)
{
  item_baton *newb = apr_palloc(pool, sizeof(*newb));

  newb->editor = editor;
  newb->baton = baton;
  rb_ary_push(rb_ivar_get(editor, rb_id_baton()), baton);

  return newb;
}


static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  item_baton *ib = edit_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(3,
                     ib->editor,
                     rb_id_set_target_revision(),
                     INT2NUM(target_revision));
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);
  return err;
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  item_baton *ib = edit_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;
  VALUE result;

  args = rb_ary_new3(3,
                     ib->editor,
                     rb_id_open_root(),
                     INT2NUM(base_revision));
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  *root_baton = make_baton(dir_pool, ib->editor, result);
  return err;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(5,
                     ib->editor,
                     rb_id_delete_entry(),
                     c2r_string2(path),
                     INT2NUM(revision),
                     ib->baton);
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);
  return err;
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *dir_pool,
              void **child_baton)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;
  VALUE result;

  args = rb_ary_new3(6,
                     ib->editor,
                     rb_id_add_directory(),
                     c2r_string2(path),
                     ib->baton,
                     c2r_string2(copyfrom_path),
                     INT2NUM(copyfrom_revision));
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  *child_baton = make_baton(dir_pool, ib->editor, result);
  return err;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;
  VALUE result;

  args = rb_ary_new3(5,
                     ib->editor,
                     rb_id_open_directory(),
                     c2r_string2(path),
                     ib->baton,
                     INT2NUM(base_revision));
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  *child_baton = make_baton(dir_pool, ib->editor, result);
  return err;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  item_baton *ib = dir_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(5,
                     ib->editor,
                     rb_id_change_dir_prop(),
                     ib->baton,
                     c2r_string2(name),
                     value ? rb_str_new(value->data, value->len) : Qnil);
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);
  return err;
}

static svn_error_t *
close_baton(void *baton, ID method_id)
{
  item_baton *ib = baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(3,
                     ib->editor,
                     method_id,
                     ib->baton);
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);
  return err;
}

static svn_error_t *
close_directory(void *dir_baton, apr_pool_t *pool)
{
  return close_baton(dir_baton, rb_id_close_directory());
}

static svn_error_t *
absent_directory(const char *path, void *parent_baton, apr_pool_t *pool)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(4,
                     ib->editor,
                     rb_id_absent_directory(),
                     c2r_string2(path),
                     ib->baton);
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);
  return err;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *file_pool,
         void **file_baton)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;
  VALUE result;

  args = rb_ary_new3(6,
                     ib->editor,
                     rb_id_add_file(),
                     c2r_string2(path),
                     ib->baton,
                     c2r_string2(copyfrom_path),
                     INT2NUM(copyfrom_revision));
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  *file_baton = make_baton(file_pool, ib->editor, result);
  return err;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;
  VALUE result;

  args = rb_ary_new3(5,
                     ib->editor,
                     rb_id_open_file(),
                     c2r_string2(path),
                     ib->baton,
                     INT2NUM(base_revision));
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  *file_baton = make_baton(file_pool, ib->editor, result);
  return err;
}

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  VALUE handler = (VALUE)baton;
  VALUE args;
  VALUE result;
  svn_error_t *err = SVN_NO_ERROR;

  args = rb_ary_new3(3,
                     handler,
                     rb_id_call(),
                     window ?
                     c2r_swig_type((void *)window, (void *)"svn_txdelta_window_t *") :
                     Qnil);
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  return err;
}

static svn_error_t *
apply_textdelta(void *file_baton, 
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **h_baton)
{
  item_baton *ib = file_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;
  VALUE result;

  args = rb_ary_new3(4,
                     ib->editor,
                     rb_id_apply_textdelta(),
                     ib->baton,
                     c2r_string2(base_checksum));
  result = rb_rescue2(callback, args,
                      callback_rescue, (VALUE)&err,
                      rb_svn_error(), (VALUE)0);
  if (NIL_P(result)) {
    *handler = svn_delta_noop_window_handler;
    *h_baton = NULL;
  } else {
    *handler = window_handler;
    *h_baton = (void *)result;
  }

  return err;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  item_baton *ib = file_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(5,
                     ib->editor,
                     rb_id_change_file_prop(),
                     ib->baton,
                     c2r_string2(name),
                     value ? rb_str_new(value->data, value->len) : Qnil);
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);

  return err;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  item_baton *ib = file_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(4,
                     ib->editor,
                     rb_id_close_file(),
                     ib->baton,
                     c2r_string2(text_checksum));
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);

  return err;
}

static svn_error_t *
absent_file(const char *path,
            void *parent_baton,
            apr_pool_t *pool)
{
  item_baton *ib = parent_baton;
  svn_error_t *err = SVN_NO_ERROR;
  VALUE args;

  args = rb_ary_new3(4,
                     ib->editor,
                     rb_id_absent_file(),
                     c2r_string2(path),
                     ib->baton);
  rb_rescue2(callback, args,
             callback_rescue, (VALUE)&err,
             rb_svn_error(), (VALUE)0);

  return err;
}

static svn_error_t *
close_edit(void *edit_baton, apr_pool_t *pool)
{
  item_baton *ib = edit_baton;
  svn_error_t *err = close_baton(edit_baton, rb_id_close_edit());
  rb_ary_clear(rb_ivar_get(ib->editor, rb_id_baton()));
  return err;
}

static svn_error_t *
abort_edit(void *edit_baton, apr_pool_t *pool)
{
  item_baton *ib = edit_baton;
  svn_error_t *err = close_baton(edit_baton, rb_id_abort_edit());
  rb_ary_clear(rb_ivar_get(ib->editor, rb_id_baton()));
  return err;
}

void
svn_swig_rb_make_editor(const svn_delta_editor_t **editor,
                        void **edit_baton,
                        VALUE rb_editor,
                        apr_pool_t *pool)
{
  svn_delta_editor_t *thunk_editor = svn_delta_default_editor(pool);
  
  thunk_editor->set_target_revision = set_target_revision;
  thunk_editor->open_root = open_root;
  thunk_editor->delete_entry = delete_entry;
  thunk_editor->add_directory = add_directory;
  thunk_editor->open_directory = open_directory;
  thunk_editor->change_dir_prop = change_dir_prop;
  thunk_editor->close_directory = close_directory;
  thunk_editor->absent_directory = absent_directory;
  thunk_editor->add_file = add_file;
  thunk_editor->open_file = open_file;
  thunk_editor->apply_textdelta = apply_textdelta;
  thunk_editor->change_file_prop = change_file_prop;
  thunk_editor->close_file = close_file;
  thunk_editor->absent_file = absent_file;
  thunk_editor->close_edit = close_edit;
  thunk_editor->abort_edit = abort_edit;

  *editor = thunk_editor;
  rb_ivar_set(rb_editor, rb_id_baton(), rb_ary_new());
  *edit_baton = make_baton(pool, rb_editor, Qnil);
}

svn_error_t *
svn_swig_rb_log_receiver(void *baton,
                         apr_hash_t *changed_paths,
                         svn_revnum_t revision,
                         const char *author,
                         const char *date,
                         const char *message,
                         apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;

    args = rb_ary_new3(7,
                       proc,
                       rb_id_call(),
                       changed_paths ? 
                       svn_swig_rb_apr_hash_to_hash_string(changed_paths) :
                       Qnil,
                       c2r_long(&revision, NULL),
                       c2r_string2(author),
                       c2r_string2(date),
                       c2r_string2(message));
    rb_rescue2(callback, args,
               callback_rescue, (VALUE)&err,
               rb_svn_error(), (VALUE)0);
  }
  return err;
}


svn_error_t *
svn_swig_rb_repos_authz_func(svn_boolean_t *allowed,
                             svn_fs_root_t *root,
                             const char *path,
                             void *baton,
                             apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_error_t *err = SVN_NO_ERROR;

  *allowed = TRUE;
  
  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;

    args = rb_ary_new3(4,
                       proc,
                       rb_id_call(),
                       c2r_swig_type((void *)root, (void *)"svn_fs_root_t *"),
                       c2r_string2(path));
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    *allowed = RTEST(result);
  }
  return err;
}

svn_error_t *
svn_swig_rb_get_commit_log_func(const char **log_msg,
                                const char **tmp_file,
                                apr_array_header_t *commit_items,
                                void *baton,
                                apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;
    VALUE is_message;
    VALUE value;
    char *ret;

    args = rb_ary_new3(3,
                       proc,
                       rb_id_call(),
                       c2r_commit_item_array(commit_items));
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    is_message = rb_ary_aref_n(result, 0);
    value = rb_ary_aref_n(result, 1);

    Check_Type(value, T_STRING);
    ret = (char *)r2c_string(value, NULL, pool);
    if (RTEST(is_message)) {
      *log_msg = ret;
      *tmp_file = NULL;
    } else {
      *log_msg = NULL;
      *tmp_file = ret;
    }
  }
  return err;
}


void
svn_swig_rb_notify_func2(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  
  if (!NIL_P(proc)) {
    VALUE args;
    apr_pool_t *notify_pool;
    VALUE rb_notify_pool;
    svn_wc_notify_t *copied_notify;
    VALUE rb_copied_notify;

    svn_swig_rb_get_pool(0, NULL, 0, &rb_notify_pool, &notify_pool);
    copied_notify = svn_wc_dup_notify(notify, notify_pool);
    rb_copied_notify = c2r_swig_type((void *)copied_notify,
                                     (void *)"svn_wc_notify_t *");
    rb_set_pool(rb_copied_notify, rb_notify_pool);
    
    args = rb_ary_new3(3,
                       proc,
                       rb_id_call(),
                       rb_copied_notify);
    callback(args);
  }
}

svn_error_t *
svn_swig_rb_cancel_func(void *cancel_baton)
{
  VALUE proc = (VALUE)cancel_baton;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;

    args = rb_ary_new3(2,
                       proc,
                       rb_id_call());
    rb_rescue2(callback, args,
               callback_rescue, (VALUE)&err,
               rb_svn_error(), (VALUE)0);
  }
  
  return err;
}



/* auth provider callbacks */
svn_error_t *
svn_swig_rb_auth_simple_prompt_func(svn_auth_cred_simple_t **cred,
                                    void *baton,
                                    const char *realm,
                                    const char *username,
                                    svn_boolean_t may_save,
                                    apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_auth_cred_simple_t *new_cred = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;
    
    args = rb_ary_new3(5,
                       proc,
                       rb_id_call(),
                       rb_str_new2(realm),
                       rb_str_new2(username),
                       RTEST(may_save) ? Qtrue : Qfalse);
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    if (!NIL_P(result)) {
      void *result_cred = NULL;
      svn_auth_cred_simple_t *tmp_cred = NULL;
      
      SWIG_ConvertPtr(result, &result_cred,
                      SWIG_TypeQuery("svn_auth_cred_simple_t *"), 1);
      tmp_cred = (svn_auth_cred_simple_t *)result_cred;
      new_cred = apr_pcalloc(pool, sizeof (*new_cred));
      new_cred->username = tmp_cred->username ? \
        apr_pstrdup(pool, tmp_cred->username) : NULL;
      new_cred->password = tmp_cred->password ? \
        apr_pstrdup(pool, tmp_cred->password) : NULL;
      new_cred->may_save = tmp_cred->may_save;
    }
  }
  
  *cred = new_cred;
  return err;
}

svn_error_t *
svn_swig_rb_auth_username_prompt_func(svn_auth_cred_username_t **cred,
                                      void *baton,
                                      const char *realm,
                                      svn_boolean_t may_save,
                                      apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_auth_cred_username_t *new_cred = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;

    args = rb_ary_new3(4,
                       proc,
                       rb_id_call(),
                       rb_str_new2(realm),
                       RTEST(may_save) ? Qtrue : Qfalse);
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    if (!NIL_P(result)) {
      void *result_cred = NULL;
      svn_auth_cred_username_t *tmp_cred = NULL;
      
      SWIG_ConvertPtr(result, &result_cred,
                      SWIG_TypeQuery("svn_auth_cred_username_t *"), 1);
      tmp_cred = (svn_auth_cred_username_t *)result_cred;
      new_cred = apr_pcalloc(pool, sizeof (*new_cred));
      new_cred->username = tmp_cred->username ? \
        apr_pstrdup(pool, tmp_cred->username) : NULL;
      new_cred->may_save = tmp_cred->may_save;
    }
  }
  
  *cred = new_cred;
  return err;
}

svn_error_t *
svn_swig_rb_auth_ssl_server_trust_prompt_func(
  svn_auth_cred_ssl_server_trust_t **cred,
  void *baton,
  const char *realm,
  apr_uint32_t failures,
  const svn_auth_ssl_server_cert_info_t *cert_info,
  svn_boolean_t may_save,
  apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_auth_cred_ssl_server_trust_t *new_cred = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;

    args = rb_ary_new3(6,
                       proc,
                       rb_id_call(),
                       rb_str_new2(realm),
                       UINT2NUM(failures),
                       c2r_swig_type((void *)cert_info,
                                     (void *)"svn_auth_ssl_server_cert_info_t *"),
                       RTEST(may_save) ? Qtrue : Qfalse);
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    if (!NIL_P(result)) {
      void *result_cred;
      svn_auth_cred_ssl_server_trust_t *tmp_cred = NULL;
      
      SWIG_ConvertPtr(result, &result_cred,
                      SWIG_TypeQuery("svn_auth_cred_ssl_server_trust_t *"), 1);
      tmp_cred = (svn_auth_cred_ssl_server_trust_t *)result_cred;
      new_cred = apr_pcalloc(pool, sizeof (*new_cred));
      *new_cred = *tmp_cred;
    }
  }
  
  *cred = new_cred;
  return err;
}

svn_error_t *
svn_swig_rb_auth_ssl_client_cert_prompt_func(
  svn_auth_cred_ssl_client_cert_t **cred,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_auth_cred_ssl_client_cert_t *new_cred = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;

    args = rb_ary_new3(4,
                       proc,
                       rb_id_call(),
                       rb_str_new2(realm),
                       RTEST(may_save) ? Qtrue : Qfalse);
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    if (!NIL_P(result)) {
      void *result_cred = NULL;
      svn_auth_cred_ssl_client_cert_t *tmp_cred = NULL;
      
      SWIG_ConvertPtr(result, &result_cred,
                      SWIG_TypeQuery("svn_auth_cred_ssl_client_cert_t *"), 1);
      tmp_cred = (svn_auth_cred_ssl_client_cert_t *)result_cred;
      new_cred = apr_pcalloc(pool, sizeof (*new_cred));
      new_cred->cert_file = tmp_cred->cert_file ? \
        apr_pstrdup(pool, tmp_cred->cert_file) : NULL;
      new_cred->may_save = tmp_cred->may_save;
    }
  }
  
  *cred = new_cred;
  return err;
}

svn_error_t *
svn_swig_rb_auth_ssl_client_cert_pw_prompt_func(
  svn_auth_cred_ssl_client_cert_pw_t **cred,
  void *baton,
  const char *realm,
  svn_boolean_t may_save,
  apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_auth_cred_ssl_client_cert_pw_t *new_cred = NULL;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;
    VALUE result;

    args = rb_ary_new3(4,
                       proc,
                       rb_id_call(),
                       rb_str_new2(realm),
                       RTEST(may_save) ? Qtrue : Qfalse);
    result = rb_rescue2(callback, args,
                        callback_rescue, (VALUE)&err,
                        rb_svn_error(), (VALUE)0);

    if (!NIL_P(result)) {
      void *result_cred = NULL;
      svn_auth_cred_ssl_client_cert_pw_t *tmp_cred = NULL;
      
      SWIG_ConvertPtr(result, &result_cred,
                      SWIG_TypeQuery("svn_auth_cred_ssl_client_cert_pw_t *"), 1);
      tmp_cred = (svn_auth_cred_ssl_client_cert_pw_t *)result_cred;
      new_cred = apr_pcalloc(pool, sizeof (*new_cred));
      new_cred->password = tmp_cred->password ? \
        apr_pstrdup(pool, tmp_cred->password) : NULL;
      new_cred->may_save = tmp_cred->may_save;
    }
  }
  
  *cred = new_cred;
  return err;
}


apr_file_t *
svn_swig_rb_make_file(VALUE file, apr_pool_t *pool)
{
  apr_file_t *apr_file = NULL;
  
  apr_file_open(&apr_file, StringValuePtr(file),
                APR_CREATE | APR_READ | APR_WRITE,
                APR_OS_DEFAULT,
                pool);
  
  return apr_file;
}


static svn_error_t *
read_handler_rbio (void *baton, char *buffer, apr_size_t *len)
{
  VALUE result;
  VALUE io = (VALUE)baton;
  svn_error_t *err = SVN_NO_ERROR;

  result = rb_funcall(io, rb_id_read(), 1, INT2NUM(*len));
  memcpy(buffer, StringValuePtr(result), RSTRING(result)->len);
  *len = RSTRING(result)->len;

  return err;
}

static svn_error_t *
write_handler_rbio (void *baton, const char *data, apr_size_t *len)
{
  VALUE io = (VALUE)baton;
  svn_error_t *err = SVN_NO_ERROR;

  rb_funcall(io, rb_id_write(), 1, rb_str_new(data, *len));

  return err;
}

svn_stream_t *
svn_swig_rb_make_stream(VALUE io)
{
  svn_stream_t *stream;
  
  if (RTEST(rb_funcall(rb_svn_core_stream(), rb_id_eqq(), 1, io))) {
    SWIG_ConvertPtr(io, (void **)&stream, SWIG_TypeQuery("svn_stream_t *"), 1);
  } else {
    VALUE rb_pool = rb_pool_new();
    apr_pool_t *pool;
    
    rb_set_pool(io, rb_pool);
    SWIG_ConvertPtr(rb_pool, (void **)&pool, SWIG_TypeQuery("apr_pool_t *"), 1);
    stream = svn_stream_create((void *)io, pool);
    svn_stream_set_read(stream, read_handler_rbio);
    svn_stream_set_write(stream, write_handler_rbio);
  }
  
  return stream;
}

void
svn_swig_rb_set_revision(svn_opt_revision_t *rev, VALUE value)
{
  switch (TYPE(value)) {
  case T_NIL:
    rev->kind = svn_opt_revision_unspecified;
    break;
  case T_FIXNUM:
    rev->kind = svn_opt_revision_number;
    rev->value.number = NUM2LONG(value);
    break;
  case T_STRING:
    if (RTEST(rb_reg_match(rb_reg_new("^BASE$",
                                      strlen("^BASE$"),
                                      RE_OPTION_IGNORECASE),
                           value)))
      rev->kind = svn_opt_revision_base;
    else if (RTEST(rb_reg_match(rb_reg_new("^HEAD$",
                                           strlen("^HEAD$"),
                                           RE_OPTION_IGNORECASE),
                                value)))
      rev->kind = svn_opt_revision_head;
    else if (RTEST(rb_reg_match(rb_reg_new("^WORKING$",
                                           strlen("^WORKING$"),
                                           RE_OPTION_IGNORECASE),
                                value)))
      rev->kind = svn_opt_revision_working;
    else if (RTEST(rb_reg_match(rb_reg_new("^COMMITTED$",
                                           strlen("^COMMITTED$"),
                                           RE_OPTION_IGNORECASE),
                                value))) 
      rev->kind = svn_opt_revision_committed;
    else if (RTEST(rb_reg_match(rb_reg_new("^PREV$",
                                           strlen("^PREV$"),
                                           RE_OPTION_IGNORECASE),
                                value))) 
      rev->kind = svn_opt_revision_previous;
    else
      rb_raise(rb_eArgError,
               "invalid value: %s",
               StringValuePtr(value));
    break;
  default:
    if (rb_obj_is_kind_of(value,
                          rb_const_get(rb_cObject, rb_intern("Time")))) {
      rev->kind = svn_opt_revision_date;
      rev->value.date = NUM2LONG(rb_funcall(value, rb_intern("to_i"), 0));
    } else {
      rb_raise(rb_eArgError,
               "invalid type: %s",
               rb_class2name(CLASS_OF(value)));
    }
    break;
  }
}

void
svn_swig_rb_adjust_arg_for_client_ctx_and_pool(int *argc, VALUE **argv)
{
  if (*argc > 1) {
    VALUE last_arg = (*argv)[*argc - 1];
    if (NIL_P(last_arg) || POOL_P(last_arg)) {
      *argv += *argc - 2;
      *argc = 2;
    } else {
      if (CONTEXT_P(last_arg)) {
        *argv += *argc - 1;
        *argc = 1;
      } else {
        *argv += *argc - 2;
        *argc = 2;
      }
    }
  }
}

void
svn_swig_rb_wc_status_func(void *baton,
                           const char *path,
                           svn_wc_status2_t *status)
{
  VALUE proc = (VALUE)baton;

  if (!NIL_P(proc)) {
    VALUE args;
    apr_pool_t *status_pool;
    VALUE rb_status_pool;
    svn_wc_status2_t *copied_status;
    VALUE rb_copied_status;

    svn_swig_rb_get_pool(0, NULL, 0, &rb_status_pool, &status_pool);
    copied_status = svn_wc_dup_status2(status, status_pool);
    rb_copied_status = c2r_swig_type((void *)copied_status,
                                     (void *)"svn_wc_status2_t *");
    rb_set_pool(rb_copied_status, rb_status_pool);
    
    args = rb_ary_new3(4,
                       proc,
                       rb_id_call(),
                       rb_str_new2(path),
                       rb_copied_status);
    callback(args);
  }
}

svn_error_t *
svn_swig_rb_client_blame_receiver_func(void *baton,
                                       apr_int64_t line_no,
                                       svn_revnum_t revision,
                                       const char *author,
                                       const char *date,
                                       const char *line,
                                       apr_pool_t *pool)
{
  VALUE proc = (VALUE)baton;
  svn_error_t *err = SVN_NO_ERROR;

  if (!NIL_P(proc)) {
    VALUE args;

    args = rb_ary_new3(7,
                       proc,
                       rb_id_call(),
                       sizeof(apr_int64_t) == sizeof(long long) ?
                         LONG2NUM(line_no) :
                         LL2NUM(line_no),
                       INT2NUM(revision),
                       rb_str_new2(author),
                       rb_str_new2(date),
                       rb_str_new2(line));
    
    rb_rescue2(callback, args,
               callback_rescue, (VALUE)&err,
               rb_svn_error(), (VALUE)0);
  }
  
  return err;
}

