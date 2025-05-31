<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
    <xsl:output method="xml" indent="yes"/>

    <xsl:variable name="mapping" select="document('xmltv-category.xml')/mapping" />
    <xsl:variable name="categorieMapping" select="$mapping/categories/category/contains" />

    <xsl:template match="@* | node()">
        <xsl:copy>
            <xsl:apply-templates select="@* | node()"/>
        </xsl:copy>
    </xsl:template>

    <!-- delete elements which only produces error messages in epgd log -->
    <xsl:template match="image"/>
    <xsl:template match="episode-num"/>
    <xsl:template match="url"/>
    <xsl:template match="icon"/>
    <xsl:template match="star-rating"/>
    <xsl:template match="new"/>
    <xsl:template match="live"/>

    <xsl:template match="programme">
        <event>
            <xsl:attribute name="eventid">
                <xsl:value-of select="substring(attribute::start, 0, 15)"/>
            </xsl:attribute>

            <xsl:apply-templates select="@* | node()"/>

            <xsl:call-template name="mapping">
                <xsl:with-param name="str" select="category" />
            </xsl:call-template>

            <!-- set dummy values, will be replaced in the plugin -->
            <starttime>0</starttime>
            <duration>0</duration>
        </event>
    </xsl:template>

    <xsl:template name="starttime">
        <starttime>TEST</starttime>
        <duration>TEST</duration>
    </xsl:template>

    <xsl:template match="sub-title">
        <shorttext>
            <xsl:apply-templates select="@* | node()"/>
        </shorttext>
    </xsl:template>

    <xsl:template match="desc">
        <longdescription>
            <xsl:apply-templates select="@* | node()"/>
        </longdescription>
    </xsl:template>

    <xsl:template match="date">
        <year>
            <xsl:value-of select="substring(., 0, 5)" />
        </year>
    </xsl:template>

    <xsl:template match="category">
        <genre>
            <xsl:apply-templates select="@* | node()"/>
        </genre>
    </xsl:template>

    <xsl:template match="composer">
        <music>
            <xsl:apply-templates select="@* | node()"/>
        </music>
    </xsl:template>

    <xsl:template match="credits">
        <producer>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'producer'" />
            </xsl:call-template>
        </producer>

        <guest>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'guest'" />
            </xsl:call-template>
        </guest>

        <director>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'director'" />
            </xsl:call-template>
        </director>

        <music>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'composer'" />
            </xsl:call-template>
        </music>

        <screenplay>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'writer'" />
            </xsl:call-template>

            <xsl:value-of select="concat('' , ', ')"/>

            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'adapter'" />
            </xsl:call-template>
        </screenplay>

        <commentator>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'commentator'" />
            </xsl:call-template>

            <xsl:value-of select="concat('' , ', ')"/>

            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'editor'" />
            </xsl:call-template>

            <xsl:value-of select="concat('' , ', ')"/>

            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'presenter'" />
            </xsl:call-template>
        </commentator>

        <actor>
            <xsl:call-template name="concat">
                <xsl:with-param name="name" select = "'actor'" />
            </xsl:call-template>
        </actor>
    </xsl:template>

    <xsl:template name = "concat" >
        <xsl:param name = "name" />

        <xsl:text />
        <xsl:for-each select="*[local-name() = $name]">
            <xsl:variable name="i" select="position()" />

            <xsl:choose>
                <xsl:when test="count(../*[local-name() = $name])-position() >= 1">
                    <xsl:value-of select="../*[local-name() = $name][$i]"/>
                    <xsl:if test="../*[local-name() = $name][$i]/@role != ''"> (<xsl:value-of select="../*[local-name() = $name][$i]/@role" />)</xsl:if>
                    <xsl:value-of select="', '"/>
                </xsl:when>

                <xsl:when test="count(../*[local-name() = $name])-position() = 0">
                    <xsl:value-of select="../*[local-name() = $name][$i]"/>
                    <xsl:if test="../*[local-name() = $name][$i]/@role != ''"> (<xsl:value-of select="../*[local-name() = $name][$i]/@role"/>)</xsl:if>
                </xsl:when>
            </xsl:choose>
        </xsl:for-each>
    </xsl:template>

    <xsl:template name="mapping">
        <xsl:param name="str" select="." />
        <xsl:variable name="value" select="translate($str,'ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖU', 'abcdefghijklmnopqrstuvwxyzäöü')" />
        <xsl:variable name="map" select="$categorieMapping[contains($value, .)]/../@name" />
        <xsl:choose>
            <xsl:when test="string-length($map)">
                <category><xsl:value-of select="$map"/></category>
            </xsl:when>
            <xsl:otherwise>
                <category><xsl:text>Sonstige</xsl:text></category>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>
</xsl:stylesheet>
