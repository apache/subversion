========================================================================
1. Status
========================================================================

Translation in progress. XML files will be uploaded later.


========================================================================
2. Building HTML from DocBook. Russian specific.
========================================================================

You can build *.html files using standard xsl stylesheets
(from http://sourceforge.net/projects/docbook) without any changes.
This is good news.

However, cyrillic characters looks like '&#XXXX;' in source of the
output files.

It's better to make minor changes to 'docbook.xsl' and
'chunker.xsl' files:

------------------------------------------------------------------------

Native 'docbook.xsl':
<xsl:output method="html" encoding="ISO-8859-1" indent="no"/>

Modified 'docbook.xsl':
<xsl:output method="html" encoding="UTF-8" indent="no"/>
or
<xsl:output method="html" encoding="windows-1251" indent="no"/>

------------------------------------------------------------------------

Native 'chunker.xsl':
<xsl:param name="chunker.output.encoding" select="'ISO-8859-1'"/>

Modified 'chunker.xsl':
<xsl:param name="chunker.output.encoding" select="'UTF-8'"/>
or
<xsl:param name="chunker.output.encoding" select="'windows-1251'"/>
------------------------------------------------------------------------

It is decrease *.html output file sizes.
As I mean, using UTF-8 is more preferrable.


--
Dmitriy O Popkov
mailto:dimentiy@dimentiy.info