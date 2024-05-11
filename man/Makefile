GROFF_TARGETS := flashmq.1 flashmq.conf.5
HTML5_TARGETS := flashmq.1.html flashmq.conf.5.html

all: $(GROFF_TARGETS) $(HTML5_TARGETS)

$(GROFF_TARGETS): %: flashmq-docbook5-refentry-to-manpage.xsl %.dbk5
	xsltproc --xinclude --stringparam creation-date "$(shell date +'%b %d %Y')" $^ > $@

%.html: docbook5-refentry-xslt/docbook5-refentry-to-html5.xsl %.dbk5
	xsltproc --xinclude --stringparam dbk5.reference $(CURDIR)/reference.dbk5 $^ > $@
