<?xml version="1.0"?>
<x:transform
  xmlns:x="http://www.w3.org/1999/XSL/Transform"
  version="1.0">

  <x:output method="xml" cdata-section-elements=""
    doctype-public="-//W3C//DTD XHTML 1.0 Strict//EN"
    doctype-system="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"
    omit-xml-declaration="yes"
    />

  <!-- Simple pass-thru for most leaf-type nodes -->
  <x:template match="comment()|processing-instruction()|text()|@*">
    <x:copy-of select="."/>
  </x:template>

  <!-- By default, pass-thru elements, including their attributes -->
  <x:template match="*">
    <x:copy>
      <x:apply-templates select="@*"/>
      <x:apply-templates/>
    </x:copy>
    <x:message>
      <x:text>Warning: unspecialized tag: &lt;</x:text>
      <x:value-of select="name()"/>
      <x:for-each select="@*">
        <x:text> </x:text>
        <x:value-of select="name()"/>="<x:value-of select="."/>"</x:for-each>
      <x:text>&gt;</x:text>
    </x:message>
  </x:template>

  <!-- Drop attributes which we do not have in the source document, but
       which are created by default values in the DTD. -->
  <x:template match="@inheritnum[parent::orderedlist] |
    @continuation[parent::orderedlist] | @moreinfo[parent::filename] |
    @moreinfo[parent::literal]" />

  <!-- Main HTML document enclosure -->
  <x:template match="/">
    <html>
      <head>
        <style type="text/css"><![CDATA[
          @import "tigris-branding/css/tigris.css";
          @import "tigris-branding/css/inst.css";
          ]]></style>
        <link rel="stylesheet" type="text/css" media="print"
          href="tigris-branding/css/print.css"/>
        <script type="text/javascript"
          src="tigris-branding/scripts/tigris.js"></script>
        <title>Subversion Design</title>
      </head>

      <x:text>&#10;</x:text>
      <body>
        <x:text>&#10;</x:text>
        <div class="app">
          <x:text>&#10;</x:text>
          <x:apply-templates/>
        </div>
      </body>
    </html>
  </x:template>

  <!-- <book> is a redundant container -->
  <x:template match="book">
    <x:apply-templates/>
  </x:template>

  <!-- <bookinfo> is various metadata which can be removed -->
  <x:template match="bookinfo" />

  <x:template match="book/title | book/subtitle">
    <p class="warningmark"><em>
        <x:apply-templates/>
    </em></p>
  </x:template>

  <x:template match="para">
    <p><x:apply-templates/></p>
  </x:template>

  <x:template match="screen | programlisting">
    <pre>
      <x:apply-templates/>
    </pre>
  </x:template>

  <x:template match="filename | literal | email">
    <tt class="{name()}"><x:apply-templates/></tt>
  </x:template>

  <x:template match="emphasis">
    <em><x:apply-templates/></em>
  </x:template>

  <x:template match="emphasis[@role='bold']">
    <strong><x:apply-templates/></strong>
  </x:template>

  <x:template match="firstterm">
    <strong class="{name()}"><x:apply-templates/></strong>
  </x:template>

  <x:template match="citetitle | email | replaceable">
    <em class="{name()}"><x:apply-templates/></em>
  </x:template>

  <x:template match="chapter | appendix">
    <div class="h2" id="{@id}" title="#{@id}">
      <x:text>&#10;  </x:text>
      <h2><x:value-of select="title"/></h2>
      <x:apply-templates select="child::node()[name() != 'title']"/>
    </div><x:text> </x:text>
    <x:comment><x:text> </x:text><x:value-of select="@id"/> (h2) </x:comment>
  </x:template>

  <x:template match="sect1">
    <div class="h3" id="{@id}" title="#{@id}">
      <x:text>&#10;    </x:text>
      <h3><x:value-of select="title"/></h3>
      <x:apply-templates select="child::node()[name() != 'title']"/>
    </div><x:text> </x:text>
    <x:comment><x:text> </x:text><x:value-of select="@id"/> (h3) </x:comment>
  </x:template>

  <x:template match="sect2">
    <div class="h4" id="{@id}" title="#{@id}">
      <x:text>&#10;      </x:text>
      <h4><x:value-of select="title"/></h4>
      <x:apply-templates select="child::node()[name() != 'title']"/>
    </div><x:text> </x:text>
    <x:comment><x:text> </x:text><x:value-of select="@id"/> (h4) </x:comment>
  </x:template>

  <x:template match="sect3" title="#{@id}">
    <div class="h5" id="{@id}">
      <x:text>&#10;        </x:text>
      <h5><x:value-of select="title"/></h5>
      <x:apply-templates select="child::node()[name() != 'title']"/>
    </div><x:text> </x:text>
    <x:comment><x:text> </x:text><x:value-of select="@id"/> (h5) </x:comment>
  </x:template>

  <x:template match="simplesect">
    <x:apply-templates/>
  </x:template>

  <x:template match="ulink">
    <a href="{@url}"><x:value-of select="@url"/></a>
  </x:template>

  <x:template match="xref">
    <a href="#{@linkend}"><x:value-of select="id(@linkend)/title"/></a>
  </x:template>

  <x:template match="footnote">
    (<x:apply-templates/>)
  </x:template>

  <x:template match="footnote/para">
    <x:apply-templates/>
  </x:template>

  <x:template match="para/itemizedlist">
    <x:text disable-output-escaping="yes">&lt;/p&gt;</x:text>
    <ul>
      <x:apply-templates/>
    </ul>
    <x:text disable-output-escaping="yes">&lt;p&gt;</x:text>
  </x:template>

  <x:template match="itemizedlist">
    <ul>
      <x:apply-templates/>
    </ul>
  </x:template>

  <x:template match="orderedlist">
    <ol>
      <x:apply-templates/>
    </ol>
  </x:template>

  <x:template match="itemizedlist/listitem | orderedlist/listitem">
    <li>
      <x:apply-templates/>
    </li>
  </x:template>

  <x:template match="variablelist">
    <dl>
      <x:apply-templates/>
    </dl>
  </x:template>

  <x:template match="varlistentry">
    <x:apply-templates/>
  </x:template>

  <x:template match="varlistentry/term">
    <dt><x:apply-templates/></dt>
  </x:template>

  <x:template match="varlistentry/listitem">
    <dd>
      <x:apply-templates/>
    </dd>
  </x:template>
</x:transform>
