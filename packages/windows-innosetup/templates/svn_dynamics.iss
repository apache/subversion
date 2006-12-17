; This is where you can let Inno Setup know where to find the files included in
; the setup.
; The paths can be relative to the "packages\win32-innosetup" path in the
; Subversion repository or they can be absolute (full path).
; This file must reside in the "packages\win32-innosetup" directory so copy it
; to there and edit it.
; This file is ignored by the Subversion repository when it resides in
; "packages\win32-innosetup". That's because the many people who want to make
; distros for Subversion have different preferences (compilers, sources, etc.).
;
; #############################################################################
; Edit this variables and make sure that all files are where they should be
; according to this file
;
; Misc files such as svn.chm goes here:
#define path_setup_in "in"
; This is where the final setup ends
#define path_setup_out "out"
; Path to Inno Setup Executable
#define path_is "E:\Inno Setup4"

;Build defines ----------------------------------------------------------------
; File names
#define libdb_dll "libdb43.dll"

; Uncomment the defines if true
#define inc_dbgsyms
#define VC7
#define inc_locale

; Apache Server: The minimum required version
#define apache_ver_min "2.0.52"

;Paths the program locations and the Subversion Book sources ------------------
; Edit the paths below according to your system

; Full path to the binaries root folder
#define path_svn "C:\work\Subversion\binaries_svn\svn-win32"
; Full path to the WC of the svnbook
#define path_svnbook "C:\work\Subversion\svnbook"
; Full path to MS HTML help compiler hhc.exe's parent folder
#define path_hhc "E:\HTMLHelpWorkShop"


; Basic files
#define path_svnclient         (path_svn + "bin")
#define path_svnsync           (path_svn + "bin")
#define path_svnadmin          (path_svn + "bin")
#define path_svnlook           (path_svn + "bin")
#define path_svnserve          (path_svn + "bin")
#define path_svnversion        (path_svn + "bin")
#define path_svndumpfilter     (path_svn + "bin")
#define path_locale            (path_svn + "share\locale")
; APR
#define path_libapr_dll        (path_svn + "bin")
#define path_libaprutil_dll    (path_svn + "bin")
#define path_libapriconv_dll   (path_svn + "bin")
#define path_iconv_dll         (path_svn + "bin")
#define path_intl_dll          (path_svn + "bin")
; httpd
#define path_davsvn            (path_svn + "bin")
#define path_authzsvn          (path_svn + "bin")
; Misc
#define path_msvcr70_dll       (path_svn + "bin")
#define path_brkdb_dll         (path_svn + "bin")
#define path_svnpath           "tools\svnpath"
#define path_iconv             (path_svn + "iconv")
#define path_ssl               (path_svn + "bin")
; Licenses
#define path_licenses          (path_svn + "licenses")

;Debug symbols ----------------------------------------------------------------
; Basic files
#define path_svnclient_pdb     (path_svn + "bin")
#define path_svnsync_pdb       (path_svn + "bin")
#define path_svnadmin_pdb      (path_svn + "bin")
#define path_svnlook_pdb       (path_svn + "bin")
#define path_svnserve_pdb      (path_svn + "bin")
#define path_svnversion_pdb    (path_svn + "bin")
#define path_svndumpfilter_pdb (path_svn + "bin")
; httpd
#define path_davsvn_pdb        (path_svn + "bin")
#define path_authzsvn_pdb      (path_svn + "bin")
; Misc
#define path_iconv_pdb         (path_svn + "iconv")
#define path_libapr_pdb        (path_svn + "lib\apr")
#define path_libapu_pdb        (path_svn + "lib\apr-util")
#define path_py_libsvn_pdb     (path_svn + "python\libsvn")

;Development -------------------------------------------------------------------
#define path_dev_doc           (path_svn + "doc")
#define path_dev_inc           (path_svn + "include")
#define path_dev_inc_apr       (path_svn + "include\apr")
#define path_dev_inc_apu       (path_svn + "include\apr-util")
#define path_dev_lib           (path_svn + "lib")
#define path_dev_lib_apr       (path_svn + "lib\apr")
#define path_dev_lib_apu       (path_svn + "lib\apr-util")

;Python bindings
#define path_py_bind_libsvn    (path_svn + "python\libsvn")
#define path_py_bind_svn       (path_svn + "python\svn")
