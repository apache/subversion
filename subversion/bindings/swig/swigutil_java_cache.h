/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file swigutil_java_cache.h
 * @brief Caching of Java class references and method IDs.
 */

#if defined(SVN_SWIG_JAVA_DEFINE_CACHE) || defined(SVN_SWIG_JAVA_INIT_CACHE) || defined(SVN_SWIG_JAVA_TERM_CACHE) || !defined(SVN_SWIG_JAVACACHE_INCLUDED)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Define methodID/class references */
#if defined(SVN_SWIG_JAVA_DEFINE_CACHE)

#define SVN_SWIG_JAVA_CACHE_START

#define SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,name,clazz,method,signature) \
    jmethodID name;

#define SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,name,clazzname) \
    jclass name;

#define SVN_SWIG_JAVA_CACHE_END

/* Initialize methodID/class references */
#elif defined(SVN_SWIG_JAVA_INIT_CACHE)

#define SVN_SWIG_JAVA_CACHE_START \
    { jclass _clazz;

#define SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,name,clazz,method,signature) \
    name = JCALL3(GetMethodID, jenv, clazz, method, signature);          \
    if (name == NULL) { return JNI_ERR; }

#define SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,name,clazzname) \
    _clazz = JCALL1(FindClass, jenv, clazzname);           \
    if (_clazz == NULL) { return JNI_ERR; }                \
    name = JCALL1(NewGlobalRef, jenv, _clazz);             \
    if (name == NULL) { return JNI_ERR; }

#define SVN_SWIG_JAVA_CACHE_END \
    }


/* Clear methodID/class references */
#elif defined(SVN_SWIG_JAVA_TERM_CACHE)

#define SVN_SWIG_JAVA_CACHE_START

#define SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,name,clazz,method,signature)

#define SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,name,clazzname) \
    JCALL1(DeleteGlobalRef, jenv, name);

#define SVN_SWIG_JAVA_CACHE_END


/* Simple declaration */
#elif !defined(SVN_SWIG_JAVACACHE_INCLUDED)

#define SVN_SWIG_JAVACACHE_INCLUDED

#define SVN_SWIG_JAVA_CACHE_START

#define SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,name,clazz,method,signature) \
    extern jmethodID name;

#define SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,name,clazzname) \
    extern jclass name;

#define SVN_SWIG_JAVA_CACHE_END

#endif


SVN_SWIG_JAVA_CACHE_START

SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_long,"java/lang/Long")
SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_string,"java/lang/String")
SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_outputstream,"java/io/OutputStream")
SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_inputstream,"java/io/InputStream")
SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_list,"java/util/ArrayList")
SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_list,"java/util/List")
SVN_SWIG_JAVA_CACHE_CLASS_DEF(jenv,svn_swig_java_cls_list,"java/util/Map")

SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,svn_swig_java_mid_long_longvalue,svn_swig_java_cls_long,"longValue","()J")
SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,svn_swig_java_mid_outputstream_write,svn_swig_java_cls_outputstream,"write","([B)V")
SVN_SWIG_JAVA_CACHE_METHOD_DEF(jenv,svn_swig_java_mid_inputstream_read,svn_swig_java_cls_inputstream,"read","([B)I")

SVN_SWIG_JAVA_CACHE_END


#undef SVN_SWIG_JAVA_CACHE_START
#undef SVN_SWIG_JAVA_CACHE_CLASS_DEF
#undef SVN_SWIG_JAVA_CACHE_METHOD_DEF
#undef SVN_SWIG_JAVA_CACHE_END

#undef SVN_SWIG_JAVA_DEFINE_CACHE
#undef SVN_SWIG_JAVA_INIT_CACHE
#undef SVN_SWIG_JAVA_TERM_CACHE

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
