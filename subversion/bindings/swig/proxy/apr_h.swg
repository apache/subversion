# Proxy classes for apr_*

%{
#include "apr.h"
%}

#ifdef WIN32
/* Fool SWIG into understanding the __int64 datatype */
#define __int64 long long
#endif

%include apr.h

#ifdef WIN32
#undef __int64
#endif
