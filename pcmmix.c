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
	int Δ;
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
	int n, m, notrunc, gotem;
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
	(fp++)->path = "stdin";
	while(*argv != nil){
		if((fp->fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
		(fp++)->path = *argv++;
	}
	gotem = 0;
	for(;;){
		n = 0;
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			if(fp == ftab){
				if((d = dirfstat(0)) == nil)
					sysfatal("dirfstat: %r");
				m = d->length;
				free(d);
				if(m > 0)
					gotem = 1;
				else if(gotem){
					if(!notrunc)
						exits(nil);
					fp->fd = -1;
					continue;
				}
			}
			m = sizeof fp->buf - fp->Δ;
			if(m < 0)
				m = sizeof fp->buf;
			if(n > 0 && n < m)
				m = n;
			fp->n = read(fp->fd, fp->buf+fp->Δ, m);
			if(n == 0 || notrunc && n < fp->n || !notrunc && n > fp->n)
				n = fp->n;
			fprint(2, "%zd n %d fp->n %d Δ %d\n", fp-ftab, n, fp->n, fp->Δ);
			if(fp->n > 0)
				continue;
			fp->fd = -1;
			if(!notrunc)
				exits(nil);
			if(fp->n < 0)
				fprint(2, "file %s: read: %r\n", fp->path);
		}
		if(n <= 0)
			break;
		memset(u, 0, n);
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			for(p=u, q=fp->buf; q<fp->buf+n; p+=2, q+=2){
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
			fp->Δ = n < fp->n ? fp->n - n : 0;
		}
		write(1, u, n);
	}
	exits(nil);
}
