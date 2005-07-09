#include "svn_private_config.h"

void
svn_swig_rb_nls_initialize(void)
{
#ifdef ENABLE_NLS
#  ifdef WIN32
#  else
  bindtextdomain(PACKAGE_NAME, SVN_LOCALE_DIR);
#    ifdef HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset(PACKAGE_NAME, "UTF-8");
#    endif
#  endif
#endif
}
