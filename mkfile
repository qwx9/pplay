</$objtype/mkfile
BIN=/$objtype/bin/audio
MAN=/sys/man/1
TARG=\
	pcmmix\
	pplay\

OFILES=\
	chunk.$O\
	cmd.$O\
	draw.$O\
	pplay.$O\
	util.$O\

HFILES=dat.h fns.h
</sys/src/cmd/mkmany

$O.pcmmix: pcmmix.$O
	$LD $LDFLAGS -o $target pcmmix.$O
