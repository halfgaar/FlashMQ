<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:dbk="http://docbook.org/ns/docbook"
  xmlns:exsl="http://exslt.org/common"
  xmlns:func="http://exslt.org/functions"
  xmlns:date="http://exslt.org/dates-and-times"
  xmlns:str="http://exslt.org/strings"
  xmlns:db2m="https://www.bigsmoke.us/docbook5-to-man.xsl/"
  xmlns:xlink="http://www.w3.org/1999/xlink"
  xmlns="http://docbook.org/ns/docbook"
  extension-element-prefixes="exsl func date str">

  <xsl:import href="./docbook5-refentry-xslt/docbook5-refentry-to-manpage.xsl"/>

  <xsl:template match="dbk:literallayout[@language='flashmq.conf']/text()">
    <xsl:variable name="lines" select="str:tokenize(self::text(), '&#xa;&#xd;')"/>
    <xsl:for-each select="$lines"><!-- Traverse each `<token>` in the `$lines` nodeset. -->
      <xsl:apply-templates select="text()" mode="flashmq.conf-line"/>
    </xsl:for-each>
  </xsl:template>

  <xsl:template match="text()" mode="flashmq.conf-line">
    <xsl:variable name="words" select="str:tokenize(self::text(), '&#x9;&#x20;')"/>
    <xsl:choose>
      <xsl:when test="$words[1] = '#'">
        <xsl:text>\m[blue]</xsl:text>
        <xsl:value-of select="self::text()"/>
        <xsl:text>\m[]</xsl:text>
      </xsl:when>
      <xsl:when test="$words[2] = '{'">
        <xsl:text>\m[yellow]</xsl:text>
        <xsl:value-of select="substring-before(self::text(), '{')"/>
        <xsl:text>\m[]</xsl:text>
        <xsl:text>{</xsl:text>
        <xsl:value-of select="substring-after(self::text(), '{')"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>\m[green]</xsl:text>
        <xsl:value-of select="substring-before(self::text(), $words[2])"/>
        <xsl:text>\m[]</xsl:text>
        <xsl:apply-templates select="exsl:node-set(concat($words[2], substring-after(self::text(), $words[2])))" mode="flashmq.conf-values"/>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:text>&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="text()" mode="flashmq.conf-values">
    <xsl:param name="char-pos" select="number('1')"/>
    <xsl:param name="in-quotes" select="''"/>
    <xsl:variable name="char" select="substring(self::text(), $char-pos, 1)"/>
    <xsl:variable name="open-quote" select="not($in-quotes) and ($char = '&quot;' or $char = &quot;'&quot;)"/>
    <xsl:variable name="close-quote" select="boolean($in-quotes) and $char = $in-quotes and substring(self::text(), $char-pos - 1, 1) != '\'"/>
    <xsl:variable name="next-in-quotes">
      <xsl:choose>
        <xsl:when test="$open-quote">
          <xsl:value-of select="$char"/>
        </xsl:when>
        <xsl:when test="$in-quotes and not($close-quote)">
          <xsl:value-of select="$in-quotes"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="''"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="is-start-of-unquoted-value" select="not($in-quotes) and not(string($next-in-quotes)) and ($char-pos = 1 or string-length(translate(substring(self::text(), $char-pos - 1, 1), ' &#x9;', '')) = 0) and boolean(translate($char, ' &#x9;{}', ''))"/>
    <xsl:variable name="is-end-of-unquoted-value" select="not($in-quotes) and not(string($next-in-quotes)) and ($char-pos = string-length(self::text()) or not(translate(substring(self::text(), $char-pos + 1, 1), ' &#x9;{}', ''))) and boolean(translate($char, ' &#x9;{}', ''))"/>
    <xsl:if test="$is-start-of-unquoted-value">
      <xsl:text>\m[cyan]</xsl:text>
    </xsl:if>
    <xsl:if test="$open-quote">
      <xsl:text>\m[magenta]</xsl:text>
    </xsl:if>
    <xsl:if test="$char = '\' and substring(self::text(), $char-pos - 1, 1) != '\'">
      <xsl:text>\m[blue]</xsl:text>
    </xsl:if>
    <xsl:value-of select="db2m:escape-char($char)"/>
    <xsl:if test="$is-end-of-unquoted-value">
      <xsl:text>\m[]</xsl:text>
    </xsl:if>
    <xsl:if test="$close-quote">
      <xsl:text>\m[default]</xsl:text>
    </xsl:if>
    <xsl:if test="substring(self::text(), $char-pos - 1, 1) = '\'">
      <xsl:text>\m[]</xsl:text>
    </xsl:if>
    <xsl:if test="$char-pos &lt; string-length(self::text())">
      <xsl:apply-templates select="self::text()" mode="flashmq.conf-values">
        <xsl:with-param name="char-pos" select="$char-pos + 1"/>
        <xsl:with-param name="in-quotes" select="string($next-in-quotes)"/>
      </xsl:apply-templates>
    </xsl:if>
  </xsl:template>
</xsl:stylesheet>
<!-- vim: set expandtab ts=2 sw=2 sts=2: -->
