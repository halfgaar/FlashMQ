<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:xi="http://www.w3.org/2001/XInclude"
  xmlns:xlink="http://www.w3.org/1999/xlink"
  xmlns:exsl="http://exslt.org/common"
  xmlns:dbk="http://docbook.org/ns/docbook"
  xmlns:html="http://www.w3.org/1999/xhtml"
  xmlns="http://www.w3.org/1999/xhtml"
  exclude-result-prefixes="xi xlink exsl html dbk">

<xsl:output
  method="html"
  version="5.0"
  doctype-system="about:legacy-compat"
  encoding="UTF-8"
  indent="yes"/>

  <xsl:param name="dbk5.reference"/>

  <xsl:variable name="reference" select="document($dbk5.reference)/dbk:reference"/>

  <xsl:template match="/">
    <html>
      <xsl:apply-templates select="/dbk:*/@xml:lang"/>
      <xsl:apply-templates select="node()"/>
    </html>
  </xsl:template>

  <xsl:template match="@xml:lang">
    <!-- Contrary to XHTML 1.1, XHTML 5 uses `lang` instead of `xml:lang` -->
    <xsl:attribute name="lang">
      <xsl:value-of select="."/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template match="@xml:id">
    <!-- Contrary to XHTML 1.1, XHTML 5 uses `id` instead of `xml:id` -->
    <xsl:attribute name="id">
      <xsl:value-of select="."/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template match="@xml:id" mode="anchor">
    <a class="hash-anchor">
      <xsl:attribute name="href">
        <xsl:text>#</xsl:text>
        <xsl:value-of select="."/>
      </xsl:attribute>
      <xsl:text>#</xsl:text>
    </a>
  </xsl:template>

  <xsl:template match="/dbk:refentry">
    <head>
      <title>
        <xsl:choose>
          <xsl:when test="dbk:refnamediv/dbk:refdescriptor">
            <!-- “When none of the `refname`s is appropriate, [the optional] `refdescriptor` is used to
                 specify the topic name.” -->
            <xsl:value-of select="dbk:refnamediv/dbk:refdescriptor"/>
          </xsl:when>
          <xsl:when test="dbk:refmeta">
            <!-- To give this precedence over `dbk:refname` slightly violates std. “processing expectations”:
                 https://tdg.docbook.org/tdg/5.0/refnamediv.html -->
            <xsl:value-of select="dbk:refmeta/dbk:refentrytitle"/>
            <xsl:if test="dbk:refmeta/dbk:manvolnum">
              <xsl:text> (</xsl:text>
              <xsl:value-of select="dbk:refmeta/dbk:manvolnum"/>
              <xsl:text>)</xsl:text>
            </xsl:if>
          </xsl:when>
          <xsl:otherwise>
            <xsl:value-of select="dbk:refnamediv/dbk:refname[1]"/>
          </xsl:otherwise>
        </xsl:choose>
        <xsl:text> – </xsl:text>
        <xsl:value-of select="dbk:refnamediv/dbk:refpurpose"/>
      </title>

      <style type="text/css" media="screen"><![CDATA[
        html {
          margin: 0;
          padding: 0;
          font-size: 18px;  /* Set rem */
          font-family: sans-serif;
        }
        body {
          padding: 1rem 2rem;
          line-height: 1.5em;
        }
        article {
          box-sizing: border-box;
          max-width: 50em;
          padding-left: 3rem;
        }
        header {
          margin-top: 2.5rem;
          margin-bottom: -.25rem;
          margin-left: -3rem;
        }
        h1 {
          margin: 0;
          line-height: 1.2em;
          font-size: 2rem;
          font-weight: bold;
          color: #111;
        }
        h1 code.manvolnum {
          font-size: 70%;
        }
        h2.refpurpose {
          margin-top: -.25em;
          font-size: 1em;
          text-transform: none;
        }
        h2 {
          margin: 0;
          line-height: 1.2em;
          font-size: .9rem;
          font-weight: 600;
          color: #111;
          text-transform: uppercase;
        }
        dt {
          margin-bottom: -.5rem;
          font-weight: bold;
          color: #111;
        }
        dt .replaceable {
          text-decoration: underline;
          font-weight: normal;
        }
        dd {
          margin-bottom: 2em;
        }
        a.hash-anchor {
          margin-left: .5em;
          color: #aaa;
          text-decoration: none;
          font-weight: normal;
        }
        a.hash-anchor:hover {
          text-decoration: underline;
          color: #111;
        }
        code {
          font-family: monospace;
          font-weight: bold;
        }
        pre.monospaced, pre.cmdsynopsis {
          background-color: #ddd;
          font-family: monospace;
          padding: 2em 2em;
        }
      ]]></style>
    </head>
    <body>
      <article>
        <xsl:apply-templates select="@xml:id"/>
        <header>
          <h1>
            <xsl:choose>
              <xsl:when test="dbk:refnamediv/dbk:refdescriptor">
                <!-- “When none of the `refname`s is appropriate, [the optional] `refdescriptor` is used to
                     specify the topic name.” -->
                <code class="refdescriptor">
                  <xsl:value-of select="dbk:refnamediv/dbk:refdescriptor"/>
                </code>
              </xsl:when>
              <xsl:when test="dbk:refmeta">
                <!-- To give this precedence over `dbk:refname` slightly violates std. “processing expectations”:
                     https://tdg.docbook.org/tdg/5.0/refnamediv.html -->
                <xsl:value-of select="dbk:refmeta/dbk:refentrytitle"/>
                <xsl:if test="dbk:refmeta/dbk:manvolnum">
                  <code class="manvolnum">
                    <xsl:text> (</xsl:text>
                    <xsl:value-of select="dbk:refmeta/dbk:manvolnum"/>
                    <xsl:text>)</xsl:text>
                  </code>
                </xsl:if>
              </xsl:when>
              <xsl:otherwise>
                <code class="refname">
                  <xsl:value-of select="dbk:refnamediv/dbk:refname[1]"/>
                </code>
              </xsl:otherwise>
            </xsl:choose>
          </h1>
          <h2 class="refpurpose">
            <xsl:value-of select="dbk:refnamediv/dbk:refpurpose"/>
          </h2>
        </header>

        <xsl:apply-templates select="dbk:refsynopsisdiv | dbk:refsect1 | dbk:refsection"/>
      </article>
    </body>
  </xsl:template>

  <xsl:template match="dbk:info"/>

  <xsl:template match="dbk:refsynopsisdiv">
    <section class="refsynopsisdiv">
      <xsl:apply-templates select="@*"/>
      <xsl:if test="not(dbk:info) and not(dbk:title)">
        <header>
          <h2>
            <xsl:text>Synopsis</xsl:text>
          </h2>
        </header>
      </xsl:if>
      <xsl:apply-templates select="node()"/>
    </section>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis">
    <pre>
      <xsl:attribute name="class">
        <xsl:value-of select="local-name(.)"/>
        <xsl:if test="@class">
          <xsl:text> </xsl:text>
          <xsl:value-of select="@class"/>
        </xsl:if>
        <xsl:if test="@role">
          <xsl:text> </xsl:text>
          <xsl:value-of select="@role"/>
        </xsl:if>
      </xsl:attribute>
      <xsl:apply-templates select="@*[local-name() != 'class'] | node()"/>
    </pre>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//text()[normalize-space(.) = '']"/><!-- Ignore space -->

  <xsl:template match="dbk:cmdsynopsis//dbk:group | dbk:cmdsynopsis//dbk:arg">
    <xsl:if test="preceding-sibling::dbk:*">
      <xsl:text> </xsl:text>
    </xsl:if>

    <xsl:choose>
      <xsl:when test="@choice = 'plain'"/>
      <xsl:when test="@choice = 'req'">
        <xsl:text>{</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>[</xsl:text>
      </xsl:otherwise>
    </xsl:choose>

    <xsl:apply-templates select="node()"/>

    <xsl:choose>
      <xsl:when test="@choice = 'plain'"/>
      <xsl:when test="@choice = 'req'">
        <xsl:text>}</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text>]</xsl:text>
      </xsl:otherwise>
    </xsl:choose>

    <xsl:if test="@rep = 'repeat'">
      <xsl:text>...</xsl:text>
    </xsl:if>

    <xsl:if test="parent::dbk:group and following-sibling::dbk:*">
      <xsl:text> |</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:replaceable">
    <xsl:if test="preceding-sibling::dbk:*">
      <xsl:text> </xsl:text>
    </xsl:if>

    <xsl:call-template name="monospace"/>
  </xsl:template>

  <xsl:template match="dbk:cmdsynopsis//dbk:sbr">
    <xsl:text>&#xa;    </xsl:text>
  </xsl:template>

  <xsl:template match="dbk:refsect1 | dbk:refsection">
    <section class="refsect1">
      <xsl:apply-templates select="@*"/>
      <xsl:apply-templates select="node()"/>
    </section>
  </xsl:template>

  <xsl:template match="dbk:refsect1/dbk:title | dbk:refsection/dbk:title | dbk:refsection/dbk:info/dbk:title">
    <header>
      <h2>
        <xsl:apply-templates select="@* | node()"/>
        <xsl:apply-templates select="../@xml:id | parent::dbk:info/dbk:refsection/@xml:id" mode="anchor"/>
      </h2>
    </header>
  </xsl:template>

  <xsl:template match="dbk:literallayout">
    <pre>
      <xsl:attribute name="class">
        <xsl:value-of select="local-name(.)"/>
        <xsl:if test="@class">
          <xsl:text> </xsl:text>
          <xsl:value-of select="@class"/>
        </xsl:if>
        <xsl:if test="@role">
          <xsl:text> </xsl:text>
          <xsl:value-of select="@role"/>
        </xsl:if>
      </xsl:attribute>
      <xsl:apply-templates select="text()"/>
    </pre>
  </xsl:template>

  <xsl:template match="dbk:variablelist">
    <dl>
      <xsl:apply-templates select="@* | node()"/>
    </dl>
  </xsl:template>

  <xsl:template match="dbk:varlistentry">
    <xsl:apply-templates select="node()"/>
  </xsl:template>

  <xsl:template match="dbk:varlistentry/dbk:term">
    <dt>
      <xsl:choose>
        <xsl:when test="not(preceding-sibling::dbk:term)">
          <xsl:apply-templates select="../@xml:id"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:apply-templates select="@xml:id"/>
        </xsl:otherwise>
      </xsl:choose>
      <xsl:apply-templates select="@* | node()"/>
      <xsl:choose>
        <xsl:when test="not(preceding-sibling::dbk:term)">
          <xsl:apply-templates select="../@xml:id" mode="anchor"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:apply-templates select="@xml:id" mode="anchor"/>
        </xsl:otherwise>
      </xsl:choose>
    </dt>
  </xsl:template>

  <xsl:template match="dbk:varlistentry/dbk:listitem">
    <dd>
      <xsl:apply-templates select="@* | node()"/>
    </dd>
  </xsl:template>

  <xsl:template match="dbk:para | dbk:simplepara">
    <p>
      <xsl:apply-templates select="@* | node()"/>
    </p>
  </xsl:template>

  <xsl:template match="dbk:emphasis">
    <em>
      <xsl:apply-templates select="@* | node()"/>
    </em>
  </xsl:template>

  <xsl:template match="dbk:emphasis[role='strong']">
    <strong>
      <xsl:apply-templates select="@* | node()"/>
    </strong>
  </xsl:template>

  <xsl:template match="dbk:citetitle">
    <cite>
      <xsl:apply-templates select="@* | node()"/>
    </cite>
  </xsl:template>

  <xsl:template match="dbk:citetitle/@pubwork"/>

  <xsl:template match="dbk:filename | dbk:command | dbk:literal | dbk:systemitem | dbk:property | dbk:function | dbk:userinput | dbk:code | dbk:replaceable | dbk:option | dbk:symbol | dbk:envvar" name="monospace">
    <code>
      <xsl:attribute name="class">
        <xsl:value-of select="local-name(.)"/>
        <xsl:if test="@class">
          <xsl:text> </xsl:text>
          <xsl:value-of select="@class"/>
        </xsl:if>
        <xsl:if test="@role">
          <xsl:text> </xsl:text>
          <xsl:value-of select="@role"/>
        </xsl:if>
      </xsl:attribute>
      <xsl:apply-templates select="node()"/>
    </code>
  </xsl:template>

  <xsl:template match="dbk:email">
    <a href="mailto:{.}">
      <xsl:value-of select="."/>
    </a>
  </xsl:template>

  <xsl:template match="dbk:link[@xlink:href]">
    <a href="{@xlink:href}">
      <xsl:apply-templates select="node()"/>
    </a>
  </xsl:template>

  <xsl:template match="dbk:citerefentry">
    <xsl:variable name="refentry" select="$reference/dbk:refentry[dbk:refmeta/dbk:refentrytitle/text() = current()/dbk:refentrytitle/text() and dbk:refmeta/dbk:manvolnum/text()=current()/dbk:manvolnum/text()]"/>
    <!--<xsl:variable name="refentry" select="$reference/dbk:refentry[dbk:refmeta/dbk:refname/text()=current()/dbk:refentrytitle/text()]"/>-->
    <xsl:choose>
      <xsl:when test="$refentry">
        <xsl:if test="not($refentry/dbk:info/dbk:biblioid[@class='uri'])">
          <xsl:message terminate="yes">
            <xsl:text>refentry/info/biblioid[@class='uri'] missing in source DocBook for man:</xsl:text>
            <xsl:value-of select="concat(dbk:refentrytitle, '(', dbk:manvolnum, ')')"/>
          </xsl:message>
        </xsl:if>
        <a>
          <xsl:attribute name="href">
            <xsl:value-of select="$refentry/dbk:info/dbk:biblioid[@class='uri']"/>
          </xsl:attribute>
          <xsl:apply-templates select="." mode="content"/>
        </a>
      </xsl:when>
      <xsl:otherwise>
        <xsl:apply-templates select="." mode="content"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="dbk:citerefentry" mode="content">
    <cite class="citerefentry">
      <span class="refentrytitle">
        <xsl:value-of select="dbk:refentrytitle"/>
      </span>
      <span class="manvolnum">
        <xsl:text>(</xsl:text>
        <xsl:value-of select="dbk:manvolnum"/>
        <xsl:text>)</xsl:text>
      </span>
    </cite>
  </xsl:template>

  <xsl:template match="dbk:itemizedlist | dbk:simplelist[@type='vert' or not(@type)]">
    <ul>
      <xsl:apply-templates select="node()"/>
    </ul>
  </xsl:template>

  <xsl:template match="dbk:itemizedlist/dbk:listitem | dbk:simplelist[@type='vert' or not(@type)]/dbk:member">
    <li>
      <xsl:apply-templates select="node()"/>
    </li>
  </xsl:template>

  <xsl:template match="dbk:simplelist[@type='inline' or @type='horiz']">
    <span class="simplelist inline">
      <xsl:apply-templates select="@*[local-name != 'inline']"/>
      <xsl:apply-templates select="node()"/>
    </span>
  </xsl:template>

  <xsl:template match="dbk:simplelist/dbk:member">
    <xsl:if test="preceding-sibling::dbk:member">
      <span class="member-sep">
        <xsl:text>, </xsl:text>
      </span>
    </xsl:if>
    <span class="member">
      <xsl:apply-templates select="node()"/>
    </span>
  </xsl:template>

  <xsl:template match="dbk:*[@outputformat][not(@outputformat='html')]"/>

  <xsl:template match="dbk:*">
    <xsl:message terminate="no">
      <xsl:text>DocBook element unrecognized by XSLT: &lt;</xsl:text>
      <xsl:value-of select="name(.)"/>
      <xsl:text>&gt;</xsl:text>
    </xsl:message>
  </xsl:template>
</xsl:stylesheet>
