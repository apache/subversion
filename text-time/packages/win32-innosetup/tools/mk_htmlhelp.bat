@ECHO OFF
REM #######################################################################
REM FILE       mk_htmlhelp.bat
REM PURPOSE    General Interface for making a Windows distribution
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

SET BOOKNAME=book
cd %BOOKNAME%
SET XLSPARAMS=--param suppress.navigation 0 --param htmlhelp.hhc.binary 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.show.advanced.search 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.chm '%BOOKNAME%.chm'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhp '%BOOKNAME%.hhp'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhk '%BOOKNAME%.hhk'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc '%BOOKNAME%.hhc'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet.type 'text/css'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet '../svn-doc.css'

xsltproc %XLSPARAMS% ..\tools\xsl\htmlhelp\htmlhelp.xsl %BOOKNAME%.xml

SET FOO=name=\"Local\" value=\"
SET BAR=name=\"Local\" value=\"%BOOKNAME%\/
perl  -pi.bak -e "s/%FOO%/%BAR%/g" %BOOKNAME%.hhc
cd ..

SET BOOKNAME=misc-docs
cd %BOOKNAME%
SET XLSPARAMS=--param suppress.navigation 0 --param htmlhelp.hhc.binary 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc.show.root 0
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.show.advanced.search 1
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.chm '%BOOKNAME%.chm'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhp '%BOOKNAME%.hhp'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhk '%BOOKNAME%.hhk'
SET XLSPARAMS=%XLSPARAMS% --param htmlhelp.hhc '%BOOKNAME%.hhc'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet.type 'text/css'
SET XLSPARAMS=%XLSPARAMS% --param html.stylesheet '../svn-doc.css'

xsltproc %XLSPARAMS% ..\tools\xsl\htmlhelp\htmlhelp.xsl %BOOKNAME%.xml

SET FOO=name=\"Local\" value=\"
SET BAR=name=\"Local\" value=\"%BOOKNAME%\/
perl  -pi.bak -e "s/%FOO%/%BAR%/g" %BOOKNAME%.hhc
cd..
