#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

void *
emalloc(usize n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("emalloc: %r");
	setmalloctag(p, getcallerpc(&n));
	return p;
}

void *
erealloc(void *p, usize n, usize oldn)
{
	if((p = realloc(p, n)) == nil)
		sysfatal("realloc: %r");
	setrealloctag(p, getcallerpc(&p));
	if(n > oldn)
		memset((uchar *)p + oldn, 0, n - oldn);
	return p;
}

char *
estrdup(char *s)
{
	if((s = strdup(s)) == nil)
		sysfatal("estrdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

int
setpri(int pri)
{
	int n, fd, pid;
	char path[32];

	if((pid = getpid()) == 0)
		return -1;
	snprint(path, sizeof path, "/proc/%d/ctl", pid);
	if((fd = open(path, OWRITE)) < 0)
		return -1;
	n = fprint(fd, "pri %d\n", pri);
	close(fd);
	if(n < 0)
		return -1;
	return 0;
}
