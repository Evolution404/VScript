# Makefile for building VS

CC= gcc -std=gnu99
CFLAGS= -O2 -Wall -Wextra -g
LIBS= -lm -lreadline

RM= rm -f


CORE_O=	vapi.o vcode.o vdebug.o vdo.o vdump.o vfunc.o vgc.o vlex.o \
	vmem.o vobject.o vopcodes.o vparser.o vstate.o vstring.o vtable.o \
	vundump.o vvm.o vzio.o
#LIB_O=	vauxlib.o vbaselib.o vbitlib.o vcorolib.o vdblib.o violib.o \
	vmathlib.o voslib.o vstrlib.o vtablib.o vutf8lib.o voadlib.o vinit.o
LIB_O=	vauxlib.o vbaselib.o vinit.o vtablib.o
BASE_O= $(CORE_O) $(LIB_O)

VS_T=	vs
VS_O=	vs.o

VSC_T=	vsc
VSC_O=	vsc.o

ALL_O= $(BASE_O) $(VS_O) $(VSC_O)
ALL_T= $(VS_T) $(VSC_T)

all:	$(ALL_T)


$(VS_T): $(VS_O) $(BASE_O)
	$(CC) -o $@ $(VS_O) $(BASE_O) $(LIBS)

$(VSC_T): $(VSC_O) $(BASE_O)
	$(CC) -o $@ $(VSC_O) $(BASE_O) $(LIBS)

clean:
	$(RM) $(ALL_T) $(ALL_O)




vapi.o: vapi.c vs.h vsconf.h vapi.h vlimits.h vstate.h \
 vobject.h vzio.h vmem.h vdebug.h vdo.h vfunc.h vgc.h vstring.h \
 vtable.h vundump.h vvm.h
vauxlib.o: vauxlib.c vs.h vsconf.h vauxlib.h
vbaselib.o: vbaselib.c vs.h vsconf.h vauxlib.h vslib.h
vtablib.o: vtablib.c vs.h vsconf.h vauxlib.h vslib.h
#violib.o: violib.c vs.h vsconf.h vauxlib.h vslib.h
#voslib.o: voslib.c vs.h vsconf.h vauxlib.h vslib.h
#vstrlib.o: vstrlib.c vs.h vsconf.h vauxlib.h vslib.h
#vmathlib.o: vmathlib.c vs.h vsconf.h vauxlib.h vslib.h
vcode.o: vcode.c vs.h vsconf.h vcode.h vlex.h vobject.h \
 vlimits.h vzio.h vmem.h vopcodes.h vparser.h vdebug.h vstate.h \
 vdo.h vgc.h vstring.h vtable.h vvm.h
vdebug.o: vdebug.c vs.h vsconf.h vapi.h vlimits.h vstate.h \
 vobject.h vzio.h vmem.h vcode.h vlex.h vopcodes.h vparser.h \
 vdebug.h vdo.h vfunc.h vstring.h vgc.h vtable.h vvm.h
vdo.o: vdo.c vs.h vsconf.h vapi.h vlimits.h vstate.h \
 vobject.h vzio.h vmem.h vdebug.h vdo.h vfunc.h vgc.h vopcodes.h \
 vparser.h vstring.h vtable.h vundump.h vvm.h
vdump.o: vdump.c vs.h vsconf.h vobject.h vlimits.h vstate.h \
 vzio.h vmem.h vundump.h
vfunc.o: vfunc.c vs.h vsconf.h vfunc.h vobject.h vlimits.h \
 vgc.h vstate.h vzio.h vmem.h
vgc.o: vgc.c vs.h vsconf.h vdebug.h vstate.h vobject.h \
 vlimits.h vzio.h vmem.h vdo.h vfunc.h vgc.h vstring.h vtable.h
vinit.o: vinit.c vs.h vsconf.h vslib.h vauxlib.h
vlex.o: vlex.c vs.h vsconf.h vlimits.h vdebug.h \
 vstate.h vobject.h vzio.h vmem.h vdo.h vgc.h vlex.h vparser.h \
 vstring.h vtable.h
vmem.o: vmem.c vs.h vsconf.h vdebug.h vstate.h vobject.h \
 vlimits.h vzio.h vmem.h vdo.h vgc.h
vobject.o: vobject.c vs.h vsconf.h vlimits.h \
 vdebug.h vstate.h vobject.h vzio.h vmem.h vdo.h vstring.h vgc.h \
 vvm.h
vopcodes.o: vopcodes.c vopcodes.h vlimits.h vs.h vsconf.h
vparser.o: vparser.c vs.h vsconf.h vcode.h vlex.h vobject.h \
 vlimits.h vzio.h vmem.h vopcodes.h vparser.h vdebug.h vstate.h \
 vdo.h vfunc.h vstring.h vgc.h vtable.h
vstate.o: vstate.c vs.h vsconf.h vapi.h vlimits.h vstate.h \
 vobject.h vzio.h vmem.h vdebug.h vdo.h vfunc.h vgc.h vlex.h \
 vstring.h vtable.h
vstring.o: vstring.c vs.h vsconf.h vdebug.h vstate.h \
 vobject.h vlimits.h vzio.h vmem.h vdo.h vstring.h vgc.h
vtable.o: vtable.c vs.h vsconf.h vdebug.h vstate.h vobject.h \
 vlimits.h vzio.h vmem.h vdo.h vgc.h vstring.h vtable.h vvm.h
vs.o: vs.c vs.h vsconf.h vauxlib.h vslib.h
vsc.o: vsc.c vs.h vsconf.h vauxlib.h vobject.h vlimits.h \
 vstate.h vzio.h vmem.h vundump.h vdebug.h vopcodes.h
vundump.o: vundump.c vs.h vsconf.h vdebug.h vstate.h \
 vobject.h vlimits.h vzio.h vmem.h vdo.h vfunc.h vstring.h vgc.h \
 vundump.h
vvm.o: vvm.c vs.h vsconf.h vdebug.h vstate.h vobject.h \
 vlimits.h vzio.h vmem.h vdo.h vfunc.h vgc.h vopcodes.h vstring.h \
 vtable.h vvm.h
vzio.o: vzio.c vs.h vsconf.h vlimits.h vmem.h vstate.h \
 vobject.h vzio.h

# list targets that do not create files (but not all makes understand .PHONY)
.PHONY: all clean
