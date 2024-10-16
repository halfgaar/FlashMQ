<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:exsl="http://exslt.org/common"
  xmlns:dbk="http://docbook.org/ns/docbook"
  xmlns:xlink="http://www.w3.org/1999/xlink"
  xmlns:semver="https://semver.org/spec/v2.0.0.html"
  xmlns:fmq="http://www.flashmq.org/man/"
  xmlns="http://docbook.org/ns/docbook"
  extension-element-prefixes="exsl">

  <!--<xsl:key name="since-flashmq-version" match="@condition" use="@-->

  <xsl:import href="./docbook5-refentry-xslt/docbook5-refentry-to-html5.xsl"/>

  <xsl:variable name="flashmq-version-conditions">
    <xsl:for-each select="//@condition">
      <xsl:if test="not(preceding::*/@condition = current()) and not(../ancestor::*/@condition = current())">
        <xsl:variable name="semver">
          <xsl:apply-templates select="." mode="extract-flashmq-version"/>
        </xsl:variable>
        <fmq:condition>
          <xsl:attribute name="docbook-condition">
            <xsl:value-of select="."/>
          </xsl:attribute>
          <xsl:call-template name="semver-parsed">
            <xsl:with-param name="semver" select="$semver"/>
          </xsl:call-template>
        </fmq:condition>
      </xsl:if>
    </xsl:for-each>
  </xsl:variable>

  <xsl:template match="dbk:refentry" mode="html-refentry-last-child">
      <style type="text/css" media="screen"><![CDATA[
        html[data-color-scheme=light] {
          --compatible-flashmq-version-color: #209020;
          --incompatible-flashmq-version-color: crimson;
          --flashmq-version-weight: bold;
        }
        html[data-color-scheme=dark] {
          --compatible-flashmq-version-color: #5cdd5c;
          --incompatible-flashmq-version-color: #fd5c5c;
          --flashmq-version-weight: normal;
        }

        .flashmq_version_requirement {
          float: right;
          color: var(--main-color);
          font-size: .9rem;
          font-weight: var(--flashmq-version-weight);

          &.compatible-with-selected-flashmq-version {
            color: var(--compatible-flashmq-version-color);
          }
          &.incompatible-with-selected-flashmq-version {
            color: var(--incompatible-flashmq-version-color);
          }
        }

        #flashmq-version-nav {
          position: sticky;
          top: 0;
        }

        #select-flashmq-version {
          float: right;
          margin-top: 4px;
          padding: 4px;
          font-size: .8rem;
        }
      ]]></style>
  </xsl:template>

  <xsl:template match="dbk:refentry" mode="html-article-first-child">
    <nav id="flashmq-version-nav">
      <select id="select-flashmq-version">
        <option value="" selected="selected">latest</option>
        <xsl:for-each select="exsl:node-set($flashmq-version-conditions)/fmq:condition">
          <xsl:sort select="semver:parsed/@major" data-type="number" order="descending"/>
          <xsl:sort select="semver:parsed/@minor" data-type="number" order="descending"/>
          <xsl:sort select="semver:parsed/@patch" data-type="number" order="descending"/>
          <option>
            <xsl:attribute name="value">
              <xsl:apply-templates select="@docbook-condition" mode="extract-flashmq-version"/>
            </xsl:attribute>
            <xsl:text>≥ v</xsl:text>
            <xsl:apply-templates select="@docbook-condition" mode="extract-flashmq-version"/>
          </option>
        </xsl:for-each>
      </select>
    </nav>
  </xsl:template>

  <xsl:template match="dbk:refentry" mode="html-body-last-child">
    <script><![CDATA[
function parseSemver(semver) {
  const re = /^(?<major>0|[1-9]\d*)\.(?<minor>0|[1-9]\d*)\.(?<patch>0|[1-9]\d*)(?:-(?<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$/;
  const { major, minor, patch, prerelease, buildmetadata } = re.exec(semver).groups;
  return {
    major: parseInt(major),
    minor: parseInt(minor),
    patch: parseInt(patch),
    prerelease: prerelease,
    buildmetadata: buildmetadata,
  };
}

function compareSemver(a, b) {
  const a_obj = parseSemver(a);
  const b_obj = parseSemver(b);

  if (a_obj.major < b_obj.major) return -1;
  if (a_obj.major > b_obj.major) return 1;
  if (a_obj.minor < b_obj.minor) return -1;
  if (a_obj.minor > b_obj.minor) return 1;
  if (a_obj.patch < b_obj.patch) return -1;
  if (a_obj.patch > b_obj.patch) return 1;
  if (a_obj.prerelease !== null && b_obj.prerelease === null) return -1;
  if (a_obj.prerelease === null && b_obj.prerelease !== null) return 1;
  // NOTE: prereleases are not taken into account and probably will never need to, because, though Wiebe
  //       may start to do prereleases, new options and option values will then almost certainly be bound
  //       to the main (major + minor + patch) version to which the prerelease identifiers are appended.

  return 0;
}

function applySelectedFlashmqVersion(selectElement) {
  console.assert(selectElement instanceof HTMLSelectElement);

  const selectedVersion = selectElement.selectedOptions[0].value;

  for (const el of document.querySelectorAll('[data-since-flashmq-version]')) {
    const contentAppliesSinceVersion = el.dataset.sinceFlashmqVersion;
    if (selectedVersion !== '' && compareSemver(contentAppliesSinceVersion, selectedVersion) > 0) {
      el.classList.add('incompatible-with-selected-flashmq-version');
      el.classList.remove('compatible-with-selected-flashmq-version');
    }
    else {
      el.classList.add('compatible-with-selected-flashmq-version');
      el.classList.remove('incompatible-with-selected-flashmq-version');
    }
  }

  const url = new URL(window.location.href);
  const searchParams = new URLSearchParams(url.search);
  if (selectedVersion === '') {
    searchParams.delete('v');
  }
  else {
    searchParams.set('v', selectedVersion);
  }
  url.search = searchParams.toString();
  history.pushState({}, '', url);
}

const selectFlashmqVersionElement = document.getElementById('select-flashmq-version');
selectFlashmqVersionElement.addEventListener('change', (e) => {
  applySelectedFlashmqVersion(e.target);
});

const url = new URL(window.location.href);
if (url.search) {
  if (url.searchParams.has('v')) {
    const versionFromUrl = url.searchParams.get('v');
    for (const opt of selectFlashmqVersionElement.options) {
      if (opt.value === versionFromUrl) {
        opt.selected = true;
      }
    }
  }
}

applySelectedFlashmqVersion(selectFlashmqVersionElement);
    ]]></script>
  </xsl:template>

  <xsl:template name="semver-prerelease-identifiers">
    <xsl:param name="prerelease"/>
    <xsl:variable name="identifier">
      <xsl:choose>
        <xsl:when test="contains($prerelease, '.')">
          <xsl:value-of select="substring-before($prerelease, '.')"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="$prerelease"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>

    <xsl:if test="$identifier">
      <semver:identifier>
        <xsl:attribute name="type">
          <xsl:choose>
            <xsl:when test="string(number($identifier)) = $identifier">
              <xsl:text>numeric</xsl:text>
            </xsl:when>
            <xsl:otherwise>
              <xsl:text>alphanumeric</xsl:text>
            </xsl:otherwise>
          </xsl:choose>
        </xsl:attribute>
        <xsl:value-of select="$identifier"/>
      </semver:identifier>
    </xsl:if>

    <xsl:if test="contains($prerelease, '.')">
      <xsl:call-template name="semver-prerelease-identifiers">
        <xsl:with-param name="prerelease" select="substring-after($prerelease, '.')"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>

  <xsl:template match="text()" mode="semver-parsed" name="semver-parsed">
    <xsl:param name="semver" select="."/>

    <xsl:variable name="buildmetadata" select="substring-after($semver, '+')"/>
    <xsl:variable name="prerelease">
      <xsl:choose>
        <xsl:when test="contains($semver, '+')">
          <xsl:value-of select="substring-after(substring-before($semver, '+'), '-')"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="substring-after($semver, '-')"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="main">
      <xsl:choose>
        <xsl:when test="contains($semver, '-')">
          <xsl:value-of select="substring-before($semver, '-')"/>
        </xsl:when>
        <xsl:when test="contains($semver, '+')">
          <xsl:value-of select="substring-before($semver, '+')"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of select="$semver"/>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="major" select="substring-before($main, '.')"/>
    <xsl:variable name="minor" select="substring-before(substring-after($main, '.'), '.')"/>
    <xsl:variable name="patch" select="substring-after($main, concat($minor, '.'))"/>
    <semver:parsed major="{$major}" minor="{$minor}" patch="{$patch}">
      <xsl:if test="$prerelease">
        <semver:prerelease>
          <xsl:call-template name="semver-prerelease-identifiers">
            <xsl:with-param name="prerelease" select="$prerelease"/>
          </xsl:call-template>
        </semver:prerelease>
      </xsl:if>
      <xsl:if test="$buildmetadata">
        <semver:buildmetadata>
          <xsl:value-of select="$buildmetadata"/>
        </semver:buildmetadata>
      </xsl:if>
    </semver:parsed>
  </xsl:template>

  <!-- This overrides a template from the `<xsl:import>`ed generic stylesheet. -->
  <xsl:template match="@condition">
    <xsl:attribute name="data-since-flashmq-version">
      <xsl:apply-templates select="." mode="extract-flashmq-version"/>
    </xsl:attribute>
  </xsl:template>

  <xsl:template match="dbk:varlistentry/dbk:term" mode="html-dt-first-child">
    <xsl:if test="../@condition">
      <!-- Add `<div>` to show since which FlashMQ version the containing block applies. -->
      <div class="flashmq_version_requirement">
        <xsl:attribute name="data-since-flashmq-version">
          <xsl:apply-templates select="../@condition" mode="extract-flashmq-version"/>
        </xsl:attribute>
        <xsl:text>≥ v</xsl:text>
        <xsl:apply-templates select="../@condition" mode="extract-flashmq-version"/>
      </div>
    </xsl:if>
  </xsl:template>

  <xsl:template match="dbk:*/@condition | fmq:condition/@docbook-condition" mode="extract-flashmq-version">
    <xsl:value-of select="normalize-space(substring-after(., 'flashmq ≥'))"/>
  </xsl:template>
</xsl:stylesheet>
<!-- vim: set expandtab ts=2 sw=2 sts=2: -->
