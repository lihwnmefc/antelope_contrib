
all Include install installMAN pf relink tags test :: FORCED
	@-if localmake_config python python_amqplib ; then \
	    $(MAKE) -f Makefile2 $@ ; \
	fi

clean uninstall :: FORCED
	$(MAKE) -f Makefile2 $@

FORCED:

