#include <u.h>
#include <libc.h>

enum{
	Bufsz = 8192,
};

typedef struct File File;
struct File{
	int fd;
	char *path;
	int n;
	uchar buf[Bufsz];
};
void
usage(void)
{
	fprint(2, "usage: %s [-t] [-f factor] [FILE..]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int m, max, notrunc;
	double f;
	File *ftab, *fp;
	uchar u[Bufsz], *p, *q;
	s32int v;
	int nf;
	Dir *d;

	notrunc = 0;
	f = 1.0;
	ARGBEGIN{
	case 'f': f = strtod(EARGF(usage()), nil); break;
	case 't': notrunc = 1; break;
	default: usage();
	}ARGEND
	nf = argc + 1;
	if((ftab = mallocz(nf * sizeof *ftab, 1)) == nil)
		sysfatal("mallocz: %r");
	fp = ftab;
	fp->path = "/fd/0";
	if((d = dirfstat(0)) == nil)
		sysfatal("dirfstat: %r");
	m = d->length;
	free(d);
	fp->fd = m > 0 ? 0 : -1;
	for(fp++; *argv!=nil; fp++){
		if((fp->fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
		fp->path = *argv++;
	}
	for(;;){
		max = sizeof ftab[0].buf;
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			fp->n = read(fp->fd, fp->buf, max);
			if(fp->n > 0){
				if(fp == ftab)
					max = fp->n;
				continue;
			}
			fp->fd = -1;
			if(fp->n < 0)
				fprint(2, "read %s: %r\n", fp->path);
			if(fp == ftab && !notrunc)
				goto end;
		}
		memset(u, 0, max);
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			for(p=u, q=fp->buf; q<fp->buf+fp->n; p+=2, q+=2){
				v = (s16int)(q[1] << 8 | q[0]);
				v *= f;
				v += (s16int)(p[1] << 8 | p[0]);
				if(v > 32767)
					v = 32767;
				else if(v < -32768)
					v = -32768;
				p[0] = v;
				p[1] = v >> 8;
			}
		}
		write(1, u, max);
	}
end:
	exits(nil);
}
