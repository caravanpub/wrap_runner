VERSION=20210527p0
TARGETS=wrap_runner
RUNNEROBJS=wrap_runner.o sha2.o my_asprintf.o

#since this is developed on OSX, some accomodation is required
UNAME := $(shell uname -s)

#defaults to be overridden
HASHDBDIR = "/var/db/wrappers/"
HASHDBFILE = "/var/db/wrappers/wrapsums"
CFGFILE = "/etc/wrap_runner.cfg"
CFLAGS ?= -g -I. -I/opt/tools/include -I/usr/pkg/include -I/opt/local/include -I/usr/local/include
LDFLAGS ?= -L/opt/tools/lib -L/usr/local/lib


ifeq ($(UNAME), HP-UX)
CFLAGS = -I. -I/opt/tools/include
LDFLAGS = -shared-libgcc -L/opt/tools/lib
endif

ifeq ($(UNAME), NetBSD)
MAKE=gmake
#used for debugging, so add -g
CFLAGS= -g -I. -I/opt/tools/include -I/usr/pkg/include
LDFLAGS = -L/opt/tools/lib -L/usr/pkg/lib -R/opt/tools/lib -R/usr/pkg/lib
endif

.DUMMY:	all

all: $(TARGETS) 

wrap_runner.o:	wrap_runner.c version.h
	$(CC) -c -D'HASHDBFILE=$(HASHDBFILE)' -D'CFGFILE=$(CFGFILE)' $(CFLAGS) wrap_runner.c
wrap_runner: $(RUNNEROBJS)
	$(CC) -o wrap_runner $(RUNNEROBJS) $(LDFLAGS) -lgdbm -lcurl

install:
	if test ! -d /opt/tools/wrappers;then /bin/mkdir -p /opt/tools/wrappers;fi
	for p in wrap_runner;do if test -f /opt/tools/wrappers/$$p;then /bin/mv /opt/tools/wrappers/$$p /opt/tools/wrappers/$$p.old;fi ;/bin/cp -p $$p /opt/tools/wrappers;done
	/bin/cp -p wrap_runner /opt/tools/wrappers
	if test ! -f /etc/wrap_runner.cfg;then cp wrap_runner.cfg /etc/; chmod 600 /etc/wrap_runner.cfg;fi
	if test ! -d $(HASHDBDIR);then mkdir -p $(HASHDBDIR); chmod 700 $(HASHDBDIR);fi


clean:
	rm -rf $(RUNNEROBJS) wrap_runner wrap_runner*.tar.gz wrap_runner-${VERSION} *.i

distclean:
	rm -rf *.o *~ wrap_runner *.tgz wrap_runner-${VERSION} wrap_runner*.tar.gz *.i

release:
	rm -rf wrap_runner-${VERSION} wrap_runner*.tar.gz
	mkdir wrap_runner-${VERSION}
	cp -rp wrap_runner.cfg sha2 *.c *.h Makefile COPYRIGHT wrap_runner-${VERSION}
	tar -cf - wrap_runner-${VERSION} | gzip > wrap_runner-${VERSION}.tar.gz
	rm -rf wrap_runner-${VERSION}

database-release:
	rm -rf wrap_runner_database-${VERSION}.tar.gz
	mkdir wrap_runner_database-${VERSION}
	cp -rp COPYRIGHT database/ControlNode database/SubmissionNode  database/local_hash_importer.sh database/wrapper.sql wrap_runner_database-${VERSION}
	tar -cf - wrap_runner_database-${VERSION} | gzip > wrap_runner_database-${VERSION}.tar.gz
	rm -rf wrap_runner_database-${VERSION}

# DO NOT DELETE
