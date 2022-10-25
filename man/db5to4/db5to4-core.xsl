<?xml version="1.0" encoding="UTF-8"?>
<!-- From the DocCookBook: https://doccookbook.sourceforge.net/html/en/dbc.structure.db5-to-db4.html -->
<xsl:stylesheet version="1.0"
  xmlns:d="http://docbook.org/ns/docbook"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:xi="http://www.w3.org/2001/XInclude"
  xmlns:xlink="http://www.w3.org/1999/xlink"
  xmlns:html="http://www.w3.org/1999/xhtml"
  xmlns:exsl="http://exslt.org/common"
  exclude-result-prefixes="d xi xlink exsl html">

  <xsl:import href="copy.xsl"/>

  <xsl:output method="xml" indent="yes"
    doctype-public="-//OASIS//DTD DocBook XML V4.5//EN"
    doctype-system="http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd"/>
  <xsl:strip-space elements="*"/>
  <xsl:preserve-space
    elements="d:screen d:programlisting d:literallayout xi:*"/>
  <xsl:variable name="inlines">abbrev accel acronym alt anchor
    annotation application author biblioref citation citebiblioid
    citerefentry citetitle classname code command computeroutput
    constant coref database date editor email emphasis envar errorcode
    errorname errortext errortype exceptionname filename firstterm
    footnote footnoteref foreignphrase function glossterm guibutton
    guiicon guilabel guimenu guimenuitem guisubmenu hardware indexterm
    initializer inlineequation inlinemediaobject interfacename jobtitle
    keycap keycode keycombo keysym link literal markup menuchoice
    methodname modifier mousebutton nonterminal olink ooclass
    ooexception oointerface option optional org orgname package
    parameter person personname phrase productname productnumber prompt
    property quote remark replaceable returnvalue shortcut subscript
    superscript symbol systemitem tag termdef token trademark type uri
    userinput varname wordasword xref</xsl:variable>

  <!-- Overwrite standard template and create elements without
       a namespace node
  -->
  <xsl:template match="d:*">
    <xsl:element name="{local-name()}">
      <xsl:apply-templates select="@*|node()"/>
    </xsl:element>
  </xsl:template>

  <xsl:template match="@xml:id|@xml:lang">
    <xsl:attribute name="{local-name()}">
      <xsl:apply-templates/>
    </xsl:attribute>
  </xsl:template>

  <!-- Suppress the following attributes: -->
  <xsl:template match="@annotations|@version"/>
  <xsl:template match="@xlink:*"/>

  <xsl:template match="@xlink:href">
    <xsl:choose>
      <xsl:when test="contains($inlines, local-name(..))">
        <ulink url="{.}" remap="{local-name(..)}">
          <xsl:value-of select=".."/>
        </ulink>
      </xsl:when>
      <xsl:otherwise>
        <xsl:message>@xlink:href could not be processed!
  parent element: <xsl:value-of select="local-name(..)"/>
        </xsl:message>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="d:*[@xlink:href]">
    <xsl:choose>
      <xsl:when test="contains($inlines, local-name())">
        <ulink url="{@xlink:href}" remap="{local-name(.)}">
          <xsl:element name="{local-name()}">
            <xsl:apply-templates
              select="@*[local-name() != 'href' and
                         namespace-uri() != 'http://www.w3.org/1999/xlink']
                      |node()"/>
          </xsl:element>
        </ulink>
      </xsl:when>
      <xsl:otherwise>
        <xsl:element name="{local-name()}">
          <xsl:apply-templates
            select="@*[local-name() != 'href' and
                       namespace-uri() != 'http://www.w3.org/1999/xlink']
                    |node()"/>
        </xsl:element>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="d:link/@xlink:href">
    <xsl:attribute name="url">
      <xsl:value-of select="."/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template match="d:link[@xlink:href]">
    <ulink>
      <xsl:apply-templates select="@*|node()"/>
    </ulink>
  </xsl:template>

  <xsl:template match="d:link[@linkend]">
    <link>
      <xsl:apply-templates select="@*|node()"/>
    </link>
  </xsl:template>

  <!-- Renamed DocBook elements -->
  <xsl:template match="d:personblurb">
    <authorblurb>
      <xsl:apply-templates select="@*|node()"/>
    </authorblurb>
  </xsl:template>
  <xsl:template match="d:tag">
    <sgmltag>
      <xsl:apply-templates select="@*|node()"/>
    </sgmltag>
  </xsl:template>

  <!-- New DocBook v5.1 and HTML elements, no mapping available -->
  <xsl:template match="d:acknowledgements|d:annotation|d:arc
                       |d:cover
                       |d:definitions
                       |d:extendedlink
                       |d:givenname
                       |d:locator
                       |d:org|d:tocdiv
                       |html:*">
    <xsl:message>Don't know how to transfer "<xsl:value-of
      select="local-name()"/>" element into DocBook 4</xsl:message>
  </xsl:template>

  <xsl:template match="d:orgname">
     <othername>
       <xsl:apply-templates select="@*|node()"/>
     </othername>
   </xsl:template>

</xsl:stylesheet>
