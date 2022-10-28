#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static int
writediskbuf(int fd)
{
	int n, m, iosz;

	seek(ifd, loops, 0);
	if((iosz = iounit(fd)) == 0)
		iosz = 8192;
	for(m=loope-loops; m>0;){
		n = m < iosz ? m : iosz;
		if(read(ifd, pcmbuf, n) != n)
			return -1;
		if(write(fd, pcmbuf, n) != n)
			return -1;
		m -= n;
	}
	seek(ifd, seekp, 0);
	return 0;
}

static int
writebuf(int fd)
{
	int n, iosz;
	uchar *p, *e;

	if((iosz = iounit(fd)) == 0)
		iosz = 8192;
	for(p=pcmbuf+loops, e=pcmbuf+loope; p<e;){
		n = e - p < iosz ? e - p : iosz;
		if(write(fd, p, n) != n)
			return -1;
		p += n;
	}
	return 0;
}

/* the entire string is treated as the filename, ie.
 * spaces and any other weird characters will be part
 * of it */
int
writeto(char *arg)
{
	int r, fd;

	if(loope - loops == 0){
		werrstr("writeto: dot isn't a range");
		return -1;
	}
	if((fd = create(arg, OWRITE, 0664)) < 0){
		werrstr("writeto: %r");
		return -1;
	}
	r = file ? writediskbuf(fd) : writebuf(fd);
	close(fd);
	return r;
}

int
cmd(char *s)
{
	int n;
	Rune r, r´;

	/* FIXME: avoid potential conflicts with keys in main() */
	assert(s != nil);
	s += chartorune(&r, s);
	for(;;){
		n = chartorune(&r´, s);
		if(r´ == Runeerror || r´ == 0){
			werrstr("truncated or malformed input");
			return -1;
		}
		if(r´ != ' ' && r´ != '\t')
			break;
		s += n;
	}
	switch(r){
//	case 'd': delete(s); break;
//	case 'c': cut(s); break;
//	case 'p': paste(s); break;
//	case 'x': crop(s); break;
//	case '^': exchange(s); break;
//	case '|': pipeto(s); break;
//	case '<': pipefrom(s); break;
	case 'w': return writeto(s);
//	case 'r': readfrom(s); break;
	default: werrstr("unknown command %C", r); break;
	}
	return -1;
}
