#include <u.h>
#include <libc.h>

typedef struct File File;
struct File{
	int fd;
	char *path;
	int n;
	uchar buf[8192];
};
void
usage(void)
{
	fprint(2, "usage: %s [-f factor] [FILE..]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int n;
	double f;
	uchar u[8192], *p, *q;
	s32int v;
	int nf;
	File *ftab, *fp;
	Dir *d;

	f = 1.0;
	ARGBEGIN{
	case 'f': f = strtod(EARGF(usage()), nil); break;
	default: usage();
	}ARGEND
	nf = argc + 1;
	if((ftab = mallocz(nf * sizeof *ftab, 1)) == nil)
		sysfatal("mallocz: %r");
	fp = ftab;
	fp->path = "stdin";
	if(nf > 1){
		if((d = dirfstat(0)) == nil)
			sysfatal("dirfstat: %r");
		fp->fd = d->length > 0 ? 0 : -1;
		free(d);
	}
	fp++;
	while(*argv != nil){
		if((fp->fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
		fp->path = *argv++;
		fp++;
	}
	for(;;){
		n = 0;
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			if((fp->n = read(fp->fd, fp->buf, sizeof fp->buf)) > 0)
				continue;
			fp->fd = -1;
			if(fp->n < 0)
				fprint(2, "file %s: read: %r\n", fp->path);
		}
		memset(u, 0, sizeof u);
		for(fp=ftab; fp<ftab+nf; fp++){
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
			if(fp->n > n)
				n = fp->n;
		}
		if(n == 0)
			break;
		write(1, u, n);
	}
	exits(nil);
}
