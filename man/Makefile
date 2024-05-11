TARGETS := flashmq.1 flashmq.conf.5 flashmq.1.html flashmq.conf.5.html

all: $(TARGETS)

.PHONY: clean
clean:
	rm *.dbk4

# Why `docbook2x-man` from the `docbook2x` Debian package, and _not_
# `docbook-to-man` from the `docbook-to-man` Debian package?  Because
# the former seems to only eat SGML (not XML) as the input format, or
# something…  Whatever the cause, it's crashing like crazy and
# `docbook2x` seems to work find (with DocBook 4; neither package has
# DocBook 5 support).
#
# See the `docbook2x-man(1)` page for XSLT parameters.
#
%: %.dbk4
	docbook2x-man $<

# As far as I am aware, there are no tools (yet) that directly convert a
# DocBook 5 `<refentry>`  to a man page.  Therefore, we convert DocBook 5 to
# DocBook 4 first.  Why not “just” directly author in DocBook 4?  DocBook 5 has
# more powerful schemas (and thus validation).
#
#.SECONDARY: %.dbk4  # Uncomment to keep intermediate `*.dbk4` files for debugging.
%.dbk4: %.dbk5
	xsltproc db5to4/db5to4-withinfo.xsl $< > $@

%.html: docbook5-to-html5/docbook5-to-html5.xsl %.dbk5
	xsltproc --xinclude --stringparam dbk5.reference $(CURDIR)/reference.dbk5 $^ > $@
