</$objtype/mkfile
BIN=/$objtype/bin/audio
MAN=/sys/man/1
TARG=\
	pplay\

OFILES=\
	chunk.$O\
	cmd.$O\
	draw.$O\
	pplay.$O\
	util.$O\

HFILES=dat.h fns.h
</sys/src/cmd/mkmany

# fuck you mk, there is no pcmmix manpage and can't
# override $MANFILES??
TARG=\
	pcmmix\
	pplay\

$O.pcmmix: pcmmix.$O
	$LD $LDFLAGS -o $target pcmmix.$O
