<?xml version="1.0" encoding="UTF-8"?>
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
  extension-element-prefixes="exsl func date str"
>
  <xsl:output method="text" indent="no" encoding="UTF-8"/>

  <refentry xml:id="docbook5-to-man.xsl.1" xml:lang="en" version="5.2">
    <refmeta>
      <refentrytitle>docbook5-to-man.xsl</refentrytitle>
      <manvolnum>7</manvolnum>
    </refmeta>

    <refnamediv>
      <refname>docbook5-to-man.xsl</refname>
      <refpurpose>XSLT sheet to transform a DocBook 5.x document to a groff man page</refpurpose>
    </refnamediv>

    <refsynopsisdiv>
      <cmdsynopsis>
        <command>xsltproc</command> <filename>path/to/docbook5-to-man.xsl</filename> <filename>path/to/source-doc.dbk</filename> > <filename>man-page.1</filename>
      </cmdsynopsis>
    </refsynopsisdiv>

    <refsect1 xml:id="dependencies">
      <title>Dependencies</title>

      <para>
        <filename>docbook5-to-man.xsl</filename> requires a XSLT 1.0 processor that understands the following EXSLT extensions:
      </para>

      <itemizedlist>
        <listitem>
        </listitem>
        <listitem>
        </listitem>
      </itemizedlist>
    </refsect1>

    <refsect1 xml:id="params">
      <title>XSLT parameters</title>

      <variablelist>
        <varlistentry xml:id="current-date">
          <term><markup role="xslt">&lt;xsl:param name="current-date" select="date:date-time()"/&gt;</markup></term>
          <listitem>
            <para>
              Any string that's understood by https://exslt.github.io/date/functions/format-date/index.html
            </para>
          </listitem>
        </varlistentry>

        <varlistentry xml:id="date-format">
          <term><markup role="xslt">&lt;xsl:param name="date-format" select="yyyy-MM-dd"/&gt;</markup></term>
          <listitem>
            <para>
https://exslt.github.io/date/functions/format-date/index.html
https://docs.oracle.com/javase/8/docs/api/java/text/SimpleDateFormat.html
            </para>
          </listitem>
        </varlistentry>
      </variablelist>
    </refsect1>
  </refentry>

  <xsl:param name="creation-date" select="date:date()"/>

  <!-- Whitespace characters -->
  <xsl:variable name="ws-chars" select="' &#x9;&#x0a;&#x0d;'"/>

  <xsl:template match="/">
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:refentry">
    <xsl:text><![CDATA[.if \n(.g .ds T< \\FC]]>&#x0a;</xsl:text>
    <xsl:text>.if \n(.g .ds T> \\F[\n[.fam]]&#x0a;</xsl:text>

    <xsl:text>.color&#x0a;</xsl:text>

    <xsl:text>.de URL&#x0a;</xsl:text>
    <xsl:text>\\$2 \(la\\$1\(ra\\$3&#x0a;</xsl:text>
    <xsl:text>..&#x0a;</xsl:text>

    <xsl:text>.if \n(.g .mso www.tmac&#x0a;</xsl:text>

    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:info"/>

  <xsl:template match="dbk:refmeta">
    <xsl:text>.TH</xsl:text>
    <xsl:text> </xsl:text>
    <xsl:value-of select="dbk:refentrytitle"/>
    <xsl:text> </xsl:text>
    <xsl:value-of select="dbk:manvolnum"/>
    <xsl:text> </xsl:text>

    <xsl:text>"</xsl:text>
    <xsl:value-of select="$creation-date"/>
    <xsl:text>"</xsl:text>

    <xsl:text> "" ""</xsl:text>

    <xsl:text>&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:refnamediv">
    <xsl:text>.SH NAME&#x0a;</xsl:text>
    <xsl:value-of select="dbk:refname"/>
    <xsl:text> \- </xsl:text>
    <xsl:value-of select="dbk:refpurpose"/>
    <xsl:text>&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:refsynopsisdiv">
    <xsl:text>.SH SYNOPSIS&#x0a;</xsl:text>
    <xsl:text>'nh&#x0a;</xsl:text>
    <xsl:text>.fi&#x0a;</xsl:text>
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis">
    <xsl:text>.ad l&#x0a;</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:command">
    <xsl:if test="preceding-sibling::dbk:*[1][local-name(.) = 'sbr']">
      <xsl:text>'in&#x0a;</xsl:text>
    </xsl:if>
    <xsl:text>\fB\m[green]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]\fR</xsl:text>
    <xsl:text> \kx</xsl:text><!-- Remember horizontal position, to align other lines on right of `<command>` -->
    <xsl:text>&#x0a;</xsl:text>
    <xsl:text>.if (\nx>(\n(.l/2)) .nr x (\n(.l/5)&#x0a;</xsl:text>
    <xsl:text>'in \n(.iu+\nxu&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:sbr">
    <xsl:text>&#x0a;.br&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:group">
    <xsl:apply-templates select="self::*" mode="open"/>
    <xsl:apply-templates select="node()"/>
    <xsl:if test="@rep = 'repeat'">
        <xsl:text>...&#x0a;</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="self::*" mode="close"/>
    <xsl:apply-templates select="self::*" mode="separation-after"/>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:arg">
    <xsl:apply-templates select="self::*" mode="open"/>
    <xsl:if test="not(child::*)">
      <xsl:text>\m[green]</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="node()"/>
    <xsl:if test="not(*)">
      <xsl:text>\m[]</xsl:text>
    </xsl:if>
    <xsl:if test="@rep = 'repeat'">
        <xsl:text>...&#x0a;</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="self::*" mode="close"/>
    <xsl:apply-templates select="self::*" mode="separation-after"/>
  </xsl:template>

  <xsl:template match="dbk:group | dbk:arg | dbk:replaceable" mode="separation-after"/>

  <xsl:template match="dbk:*[following-sibling::dbk:*]" mode="separation-after">
    <xsl:text> </xsl:text>
  </xsl:template>

  <xsl:template match="dbk:group[following-sibling::dbk:group or following-sibling::dbk:arg] | dbk:arg[following-sibling::dbk:group or following-sibling::dbk:arg]" mode="separation-after">
    <xsl:text> | </xsl:text>
  </xsl:template>

  <xsl:template match="dbk:group[@choice='plain']" mode="open"/>

  <xsl:template match="dbk:group[@choice='plain']" mode="close"/>

  <xsl:template match="dbk:arg[@choice='plain']" mode="open"/>

  <xsl:template match="dbk:arg[@choice='plain']" mode="close"/>

  <xsl:template match="dbk:group[@choice='opt' or not(@choice)] | dbk:arg[@choice='opt' or not(@choice)]" mode="open">
    <xsl:text>[</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:group[@choice='opt' or not(@choice)] | dbk:arg[@choice='opt' or not(@choice)]" mode="close">
    <xsl:text>]</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:group[@choice='req'] | dbk:arg[@choice='req']" mode="open">
    <xsl:text>{</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:group[@choice='req'] | dbk:arg[@choice='req']" mode="close">
    <xsl:text>}</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:link">
    <xsl:text>\m[green]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:replaceable">
    <xsl:text>\fI\m[green]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]\fR</xsl:text>
    <xsl:apply-templates select="self::*" mode="separation-after"/>
  </xsl:template>

  <xsl:template match="dbk:refsect1">
    <xsl:text>.SH </xsl:text>
    <xsl:value-of select="db2m:upper-case(dbk:title)"/>
    <xsl:text>&#x0a;</xsl:text>
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:title"/>

  <xsl:template match="dbk:variablelist">
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:varlistentry">
    <xsl:text>.TP&#x0a;</xsl:text>
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:term">
    <xsl:text>\*(T&lt;\fB</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR\*(T&gt;</xsl:text>
    <xsl:text>&#x0a;</xsl:text>
    <xsl:if test="following-sibling::dbk:term">
      <xsl:text>.TQ&#x0a;</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="dbk:term/dbk:option">
    <xsl:text>\m[green]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:varlistentry/dbk:listitem">
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:itemizedlist | dbk:simplelist[@type='vert' or not(@type)]">
    <xsl:text>.RS&#xA;</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>.RE&#xA;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:listitem">
    <xsl:text>.TP 0.2i&#xA;</xsl:text>
    <xsl:text>\(bu&#xA;</xsl:text>
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:simplelist[@type='inline' or @type='horiz']">
    <xsl:for-each select="dbk:member">
      <xsl:apply-templates select="node()"/>
      <xsl:if test="following-sibling::dbk:member">
        <xsl:text>, </xsl:text>
      </xsl:if>
    </xsl:for-each>
  </xsl:template>

  <xsl:template match="dbk:para">
    <xsl:if test="preceding-sibling::dbk:para">
      <xsl:text>&#x0a;</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="node()"/>
    <xsl:text>&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:literallayout[@class='monospaced']">
    <xsl:text>.PP&#x0a;</xsl:text>
    <xsl:text>.nf&#x0a;</xsl:text>
    <xsl:text>.in +7&#x0a;</xsl:text> <!-- It'd be better to make this extra indent dynamic. -->
    <xsl:apply-templates select="node()"/>
    <xsl:text>&#x0a;.in&#x0a;</xsl:text>
    <xsl:text>.fi&#x0a;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:literallayout/text()">
    <xsl:value-of select="self::text()"/>
  </xsl:template>

  <xsl:template match="dbk:link[starts-with(@xlink:href, 'https://') or starts-with(@xlink:href, 'http://')]">
    <xsl:text>\m[blue]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]</xsl:text>
    <xsl:if test="string(.) != @xlink:href">
      <!-- Only when the link text and link URL are different, do we need to make `@xlink:href` visible. -->
      <xsl:text> \(lB\fI\m[blue]</xsl:text>
      <xsl:value-of select="@xlink:href"/>
      <xsl:text>\m[]\fR\(rB</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="dbk:link[starts-with(@xlink:href, '#')]">
    <xsl:text>\fB</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:link/dbk:citetitle">
    <xsl:text>\[lq]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\[rq]</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:citerefentry">
    <xsl:text>\m[blue]</xsl:text>
    <xsl:choose>
      <xsl:when test="preceding-sibling::* or preceding-sibling::text()[not(db2m:has-only-whitespace(self::text()))]">
        <!-- Make the man page title bold when part of the regular flow of text. -->
        <xsl:text>\fB</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <!-- Add a `man:` URI prefix when the man page reference is set apart (likely in a list). -->
        <xsl:text>man:</xsl:text>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:value-of select="dbk:refentrytitle"/>
    <xsl:if test="preceding-sibling::node()">
      <!-- End the bold when part of the regular flow of text. -->
      <xsl:text>\fR</xsl:text>
    </xsl:if>
    <xsl:apply-templates select="dbk:manvolnum"/>
    <xsl:text>\m[]</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:manvolnum">
    <xsl:text>(</xsl:text>
    <xsl:apply-templates select="text()"/>
    <xsl:text>)</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:literal">
    <xsl:text>\fI</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:filename">
    <xsl:text>\fI\m[blue]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:replaceable">
    <xsl:text>\fI\m[cyan]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:option">
    <xsl:text>\fB\m[green]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR\m[]</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:command">
    <xsl:text>\fB\m[green]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:function">
    <xsl:text>\fB</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:systemitem">
    <xsl:text>\fI</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:envvar">
    <xsl:text>\fB</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:symbol">
    <xsl:text>\fB\m[red]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:email">
    <xsl:text>&lt;\m[blue]</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\m[]&gt;</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:emphasis">
    <xsl:text>\fB</xsl:text>
    <xsl:apply-templates select="node()"/>
    <xsl:text>\fR</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:*[@outputformat][not(@outputformat='manpage')]"/>

  <xsl:template match="dbk:*">
    <xsl:message terminate="no">
      <xsl:text>DocBook element unrecognized by XSLT: &lt;</xsl:text>
      <xsl:value-of select="name(.)"/>
      <xsl:text>&gt;</xsl:text>
    </xsl:message>
  </xsl:template>

  <xsl:template match="text()">
    <xsl:variable name="ws-only" select="db2m:has-only-whitespace(self::text())"/>
    <!-- Whitespace in text nodes is a bit tricky to deal with correctly while processing DocBook (very similar to HTML, in fact). -->
    <xsl:choose>
      <xsl:when test="$ws-only">
        <xsl:if test="preceding-sibling::* and following-sibling::* and self::text() = ' '">
          <xsl:text> </xsl:text>
        </xsl:if>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates select="self::text()" mode="handle-whitespace">
          <xsl:with-param name="ws-left">
            <xsl:choose>
              <xsl:when test="preceding-sibling::node()">
                <xsl:text>collapse</xsl:text>
              </xsl:when>
              <xsl:otherwise>
                <xsl:text>trim</xsl:text>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:with-param>
          <xsl:with-param name="ws-right">
            <xsl:choose>
              <xsl:when test="following-sibling::node()">
                <xsl:text>collapse</xsl:text>
              </xsl:when>
              <xsl:otherwise>
                <xsl:text>trim</xsl:text>
              </xsl:otherwise>
            </xsl:choose>
          </xsl:with-param>
        </xsl:apply-templates>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="text()" mode="handle-whitespace" name="handle-whitespace">
    <xsl:param name="ws-left"/>
    <xsl:param name="ws-right"/>
    <xsl:param name="text" select="self::text()"/>
    <xsl:variable name="first-non-ws-char-pos" select="db2m:first-non-ws-char-pos($text)"/>
    <xsl:variable name="last-non-ws-char-pos" select="db2m:last-non-ws-char-pos($text)"/>
    <xsl:choose>
      <xsl:when test="$ws-left = 'collapse' and $first-non-ws-char-pos &gt; 1">
        <xsl:text> </xsl:text>
      </xsl:when>
      <xsl:otherwise/>
    </xsl:choose>
    <xsl:choose>
      <xsl:when test="$first-non-ws-char-pos = -1 and $last-non-ws-char-pos = -1">
        <xsl:value-of select="$text"/>
      </xsl:when>
      <xsl:when test="$first-non-ws-char-pos = -1 and $last-non-ws-char-pos">
        <xsl:value-of select="substring($text, 1, $last-non-ws-char-pos)"/>
      </xsl:when>
      <xsl:when test="$first-non-ws-char-pos and $last-non-ws-char-pos = -1">
        <xsl:value-of select="substring($text, $first-non-ws-char-pos)"/>
      </xsl:when>
      <xsl:when test="$first-non-ws-char-pos and $last-non-ws-char-pos">
        <xsl:value-of select="substring($text, $first-non-ws-char-pos, $last-non-ws-char-pos - $first-non-ws-char-pos + 1)"/>
      </xsl:when>
    </xsl:choose>
    <xsl:choose>
      <xsl:when test="$ws-right = 'collapse' and $last-non-ws-char-pos &lt; string-length($text)">
        <xsl:text> </xsl:text>
      </xsl:when>
      <xsl:otherwise/>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="text()" mode="escape">
    <xsl:param name="char-pos" select="number('1')"/>
    <xsl:variable name="char" select="substring(self::text(), $char-pos, 1)"/>
    <xsl:value-of select="db2m:escape-char($char)"/>
    <xsl:if test="$char-pos &lt; string-length(self::text())">
      <xsl:apply-templates select="self::text()" mode="escape">
        <xsl:with-param name="char-pos" select="$char-pos + 1"/>
      </xsl:apply-templates>
    </xsl:if>
  </xsl:template>

  <func:function name="db2m:escape-char">
    <xsl:param name="char"/>
    <xsl:if test="string-length($char) &gt; 1">
      <xsl:message terminate="yes">
        <xsl:text>db2m:escape-car() function called with more than 1 character.</xsl:text>
      </xsl:message>
    </xsl:if>
    <func:result>
      <xsl:choose>
        <xsl:when test="$char = '\'">
          <xsl:text>\[rs]</xsl:text>
        </xsl:when>
        <xsl:when test="$char = '&quot;'">
          <xsl:text>\[dq]</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="$char"/>
        </xsl:otherwise>
      </xsl:choose>
    </func:result>
  </func:function>

  <func:function name="db2m:has-only-whitespace">
    <xsl:param name="text"/>
    <xsl:param name="ws-chars" select="$ws-chars"/>
    <xsl:if test="string-length($text) > 0 and not($text)">
      <xsl:message terminate="yes">
        <xsl:text>db2m:has-only-whitespace() function called with non-text node.</xsl:text>
      </xsl:message>
    </xsl:if>
    <xsl:choose>
      <xsl:when test="boolean(translate(substring($text, 1, 1), $ws-chars, ''))">
        <func:result select="false()"/>
      </xsl:when>
      <xsl:when test="string-length($text) > 1">
        <func:result select="db2m:has-only-whitespace(substring($text, 2))"/>
      </xsl:when>
      <xsl:otherwise>
        <func:result select="true()"/>
      </xsl:otherwise>
    </xsl:choose>
  </func:function>

  <xsl:template mode="first-non-ws-char-pos" name="first-non-ws-char-pos" match="text()">
    <xsl:param name="haystack" select="self::text()"/>
    <xsl:param name="start" select="number('1')"/>
    <xsl:param name="ws-chars" select="$ws-chars"/>
    <xsl:choose>
      <xsl:when test="$start &gt; string-length($haystack)">
        <xsl:value-of select="number('-1')"/>
      </xsl:when>
      <xsl:when test="boolean(translate(substring($haystack, $start, 1), $ws-chars, ''))">
        <xsl:value-of select="$start"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:call-template name="first-non-ws-char-pos">
          <xsl:with-param name="haystack" select="$haystack"/>
          <xsl:with-param name="start" select="$start + 1"/>
          <xsl:with-param name="ws-chars" select="$ws-chars"/>
        </xsl:call-template>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <func:function name="db2m:first-non-ws-char-pos">
    <xsl:param name="haystack"/>
    <xsl:param name="start" select="number('1')"/>
    <xsl:variable name="pos">
      <xsl:call-template name="first-non-ws-char-pos">
        <xsl:with-param name="haystack" select="$haystack"/>
        <xsl:with-param name="start" select="$start"/>
      </xsl:call-template>
    </xsl:variable>
    <func:result select="number($pos)"/>
  </func:function>

  <xsl:template mode="last-non-ws-char-pos" name="last-non-ws-char-pos" match="text()">
    <xsl:param name="haystack" select="self::text()"/>
    <xsl:param name="start" select="string-length($haystack)"/>
    <xsl:param name="ws-chars" select="$ws-chars"/>
    <xsl:choose>
      <xsl:when test="$start &lt; 1">
        <xsl:value-of select="number('-1')"/>
      </xsl:when>
      <xsl:when test="boolean(translate(substring($haystack, $start, 1), $ws-chars, ''))">
        <xsl:value-of select="$start"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:call-template name="last-non-ws-char-pos">
          <xsl:with-param name="haystack" select="$haystack"/>
          <xsl:with-param name="start" select="$start - 1"/>
          <xsl:with-param name="ws-chars" select="$ws-chars"/>
        </xsl:call-template>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <func:function name="db2m:last-non-ws-char-pos">
    <xsl:param name="haystack"/>
    <xsl:param name="start" select="string-length($haystack)"/>
    <xsl:variable name="pos">
      <xsl:call-template name="last-non-ws-char-pos">
        <xsl:with-param name="haystack" select="$haystack"/>
        <xsl:with-param name="start" select="$start"/>
      </xsl:call-template>
    </xsl:variable>
    <func:result select="number($pos)"/>
  </func:function>

  <func:function name="db2m:upper-case">
    <xsl:param name="input-string"/>
    <func:result select="translate($input-string, 'abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')"/>
  </func:function>

  <!--
  <xsl:template match="dbk:set" mode="dbk:element-category">
    <xsl:text>set</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:book" mode="dbk:element-category">
    <xsl:text>book</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:dedication" mode="dbk:element-category">
    <xsl:text>dedication</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:part | dbk:reference" mode="dbk:element-category">
    <xsl:text>division</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:toc | dbk:index" mode="dbk:element-category">
    <xsl:text>navigational-component</xsl:text>
  </xsl:template>

  <xsl:template match="dbk:preface | dbk:chapter | dbk:appendix | dbk:glossary | dbk:bibliography | dbk:article" mode="dbk:element-category">
    <xsl:text>component</xsl:text>
  </xsl:template>
  -->

  <refentry xml:id="test-entry">
  </refentry>

  <test-result xmlns="https://www.bigsmoke.us/docbook5-to-man.xsl/test-result"><![CDATA[
]]></test-result>
</xsl:stylesheet>
<!-- vim: set expandtab ts=2 sw=2 sts=2: -->
