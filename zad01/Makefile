debug=0
mode=
dbg_flg=-ggdb3
rl_flg=-DNDEBUG -O3

#convention: path don't need to start with /, but must be trailed with it
#dir always returns /-terminated path
mkfile_dir:=${dir ${abspath ${lastword ${MAKEFILE_LIST}}}}

projectdir:=${mkfile_dir}
srcdir=${projectdir}src/
basetmpdir=${projectdir}temp/
baseoutdir=${projectdir}bin/
baselibdir=${projectdir}3rdp/

dbgoutdir=${baseoutdir}debug/
rloutdir=${baseoutdir}release/
dbgtmpdir=${basetmpdir}debug/
rltmpdir=${basetmpdir}release/

o=${tmpdir}${1}

ifeq (${debug},1)
	mode=debug
	tflags=${dbg_flg}
	outdir=${dbgoutdir}
	tmpdir=${dbgtmpdir}
	libdirs=${dbglibdirs}
else
	mode=release
	tflags=${rl_flg}
	outdir=${rloutdir}
    tmpdir=${rltmpdir}
    libdirs=${rllibdirs}
endif

cflags=-Wall -Wextra -std=c99 ${tflags} ${libs} ${include}
comp=gcc ${cflags}
ocomp=gcc -c ${cflags}

runbefore0:=${shell mkdir -p ${dbgtmpdir} ${rltmpdir} ${srcdir} ${dbgoutdir} ${rloutdir}}

# ADD ALL NEW TARGET HERE AS WELL
.PHONY : all
all : client.x

.PHONY : clean
clean:
	-rm ${dbgtmpdir}*.o ${rltmpdir}*.o ${rloutdir}*.x ${dbgoutdir}*.x ${depfile}

sources=${shell find ${srcdir}*.c}
# @ToDo: tmpdir is debug/release depentend - examine whehter .deps should also be target dependent
depfile=${tmpdir}/.deps
.PHONY : ${depfile}
${depfile} :
	@-rm -f ${depfile}
	@${foreach sfile,${sources},${comp} -MT ${sfile:${srcdir}%.c=${tmpdir}%.o} -MM ${sfile} >> ${depfile};}
include ${depfile}

define objectcomp =
	${comp} -o ${outdir}$@ $^
endef

${tmpdir}%.o : ${srcdir}%.c
	${ocomp} -o $@ $<

# ADD TARGETS HERE
# To create new one:
# - copy this
# - replace 'exe' with executable name
# - remove 'main.o' compilation unit and add whatever you need 
cliento=${call o,main.o} # this is how you add compilation unit to link
client.x : ${cliento}
	$(objectcomp)

