#include <u.h>
#include <libc.h>

void
main(int argc, char **argv)
{
	int fd;
	long n;
	s16int *p, *e, buf[IOUNIT / sizeof *p];

	ARGBEGIN{
	}ARGEND
	fd = 0;
	if(*argv != nil && (fd = open(*argv, OREAD)) < 0)
		sysfatal("open: %r");
	for(;;){
		if((n = read(fd, buf, sizeof buf)) <= 0)
			break;
		for(p=buf,e=buf+n/sizeof(*buf); p<e; p++)
			*p = -(*p);
		if(write(1, buf, n) != n)
			sysfatal("write: %r");
	}
	if(n < 0)
		sysfatal("read: %r");
	exits(nil);
}
