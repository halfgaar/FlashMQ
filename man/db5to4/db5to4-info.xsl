<?xml version="1.0" encoding="UTF-8"?>
<!-- From the DocCookBook: https://doccookbook.sourceforge.net/html/en/dbc.structure.db5-to-db4.html -->
<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:d="http://docbook.org/ns/docbook"
  xmlns:xlink="http://www.w3.org/1999/xlink"
  xmlns:exsl="http://exslt.org/common"
  exclude-result-prefixes="d xlink exsl">

  <!-- Structural elements using info -->
  <xsl:template match="d:appendix[d:info]
                      |d:article[d:info]
                      |d:bibliography[d:info]
                      |d:book[d:info]
                      |d:chapter[d:info]
                      |d:colophon[d:info]
                      |d:equation[d:info]
                      |d:glossary[d:info]
                      |d:index[d:info]
                      |d:legalnotice[d:info]
                      |d:part[d:info]
                      |d:partintro[d:info]
                      |d:preface[d:info]
                      |d:reference[d:info]
                      |d:refsect1[d:info]
                      |d:refsect2[d:info]
                      |d:refsect3[d:info]
                      |d:refsection[d:info]
                      |d:refsynopsisdiv[d:info]
                      |d:sect1[d:info]
                      |d:sect2[d:info]
                      |d:sect3[d:info]
                      |d:sect4[d:info]
                      |d:sect5[d:info]
                      |d:section[d:info]
                      |d:set[d:info]
                      |d:setindex[d:info]">
    <!-- Change order of info and title  -->
    <xsl:element name="{local-name()}">
      <xsl:apply-templates select="@*"/>
      <xsl:apply-templates select="d:title/preceding-sibling::processing-instruction()
                                   |d:title/preceding-sibling::comment()"/>
      <xsl:apply-templates select="d:info"/>
      <xsl:apply-templates select="d:title"/>
      <!-- Process the rest -->
      <xsl:apply-templates select="d:info/following-sibling::node()"/>
    </xsl:element>
  </xsl:template>

  <!-- Block elements using info -->
  <xsl:template match="d:bibliolist[d:info]
                      |d:blockquote[d:info]
                      |d:equation[d:info]
                      |d:example[d:info]
                      |d:figure[d:info]
                      |d:glosslist[d:info]
                      |d:informalequation[d:info]
                      |d:informalexample[d:info]
                      |d:informalfigure[d:info]
                      |d:informaltable[d:info]
                      |d:itemizedlist[d:info]
                      |d:legalnotice[d:info]
                      |d:msgset[d:info]
                      |d:orderedlist[d:info]
                      |d:procedure[d:info]
                      |d:qandadiv[d:info]
                      |d:qandaentry[d:info]
                      |d:qandaset[d:info]
                      |d:table[d:info]
                      |d:task[d:info]
                      |d:taskprerequisites[d:info]
                      |d:taskrelated[d:info]
                      |d:tasksummary[d:info]
                      |d:variablelist[d:info]">
    <xsl:element name="{local-name()}">
      <xsl:apply-templates select="@*"/>
      <xsl:apply-templates select="d:title/preceding-sibling::processing-instruction()
                                   |d:title/preceding-sibling::comment()"/>
      <xsl:apply-templates select="d:info">
        <xsl:with-param name="infoname">block</xsl:with-param>
      </xsl:apply-templates>
      <xsl:apply-templates select="d:title|
                                   d:title/following-sibling::processing-instruction()[1]
                                   |d:title/following-sibling::comment()[1]"/>

      <!-- Process the rest -->
      <xsl:apply-templates select="d:info/following-sibling::node()"/>
    </xsl:element>
  </xsl:template>

  <!-- Suppress other info elements who has no direct mapping -->
  <xsl:template match="d:*[d:info]"/>

  <xsl:template match="d:info">
    <xsl:param name="infoname" select="local-name(..)"/>
    <xsl:variable name="rtf-node">
      <xsl:element name="{$infoname}info">
      <xsl:apply-templates select="@*|node()"/>
    </xsl:element>
    </xsl:variable>
    <xsl:choose>
      <xsl:when test="count(exsl:node-set($rtf-node)/*/*) > 0">
        <xsl:copy-of select="$rtf-node"/>
      </xsl:when>
      <xsl:otherwise><!-- Don't copy, it's empty --></xsl:otherwise>
    </xsl:choose>
  </xsl:template>
</xsl:stylesheet>
