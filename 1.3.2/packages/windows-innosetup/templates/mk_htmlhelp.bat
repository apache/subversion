@ECHO OFF
REM #######################################################################
REM FILE       mk_htmlhelp.bat
REM PURPOSE    Making MS HTML help file out of the svn-book sources
REM ====================================================================
REM Copyright (c) 2000-2005 CollabNet.  All rights reserved.
REM
REM This software is licensed as described in the file COPYING, which
REM you should have received as part of this distribution.  The terms
REM are also available at http://subversion.tigris.org/license-1.html.
REM If newer versions of this license are posted there, you may use a
REM newer version instead, at your option.
REM
REM This software consists of voluntary contributions made by many
REM individuals.  For exact contribution history, see the revision
REM history and logs, available at http://subversion.tigris.org/.
REM ====================================================================

SET BOOKNAME=svn-book
SET HHC=<%tv_path_hhc%>
SET BOOKDEST=<%tv_bookdest%>

mkdir images
copy ..\en\book\images\*.* images

SET XLSPARAMS=--param suppress.navigation 0 --param htmlhelp.hhc.binary 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.show.advanced.search 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.chm '%BOOKNAME%.chm'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhp '%BOOKNAME%.hhp'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc '%BOOKNAME%.hhc'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet.type 'text/css'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet 'svn-doc.css'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhp.tail 'svn_bck.png'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.use.hhk 0
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc.show.root 0
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.autolabel 1

xsltproc %XLSPARAMS% ..\tools\xsl\htmlhelp\htmlhelp.xsl ..\en\book\book.xml

%HHC% %BOOKNAME%.hhp
move %BOOKNAME%.chm %BOOKDEST%

del /Q %BOOKNAME%.*
del /Q *.html
rmdir /S /Q images
