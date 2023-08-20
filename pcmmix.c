#include <u.h>
#include <libc.h>

typedef struct File File;
struct File{
	int fd;
	char *path;
	int n;
	uchar *buf;
	int bufsz;
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
	int n, last, min, k, notrunc;
	double f;
	File *ftab, *fp;
	uchar u[IOUNIT], *p, *q;
	s32int v;
	int nf;

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
	if((n = iounit(0)) > 0){
		fp->fd = 0;
		fp->bufsz = n;
		if((fp->buf = mallocz(fp->bufsz, 1)) == nil)
			sysfatal("mallocz: %r");
		fp->path = "/fd/0";
	}else
		fp->fd = -1;
	for(fp++; *argv!=nil; fp++){
		if((fp->fd = open(*argv, OREAD)) < 0)
			sysfatal("open: %r");
		fp->bufsz = iounit(fp->fd);
		if((fp->buf = mallocz(fp->bufsz, 1)) == nil)
			sysfatal("mallocz: %r");
		fp->path = *argv++;
	}
	last = 0;
	for(;;){
		k = 0;
		min = sizeof u;
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			fp->n -= last;
			p = fp->buf;
			n = fp->bufsz;
			if(fp->n > 0){
				memmove(fp->buf, fp->buf+last, fp->n);
				p += fp->n;
				n -= fp->n;
			}
			assert(fp->n >= 0);
			fp->n = read(fp->fd, p, n);
			if(fp->n == 0)
				close(fp->fd);
			if(fp->n >= 0)
				fp->n += p - fp->buf;
			if(fp->n > 0){
				if(min > fp->n)
					min = fp->n;
				k++;
				continue;
			}
			if(fp->n < 0)
				fprint(2, "read %s: %r\n", fp->path);
			fp->fd = -1;
			if(!notrunc)
				goto end;
		}
		if(k == 0)
			break;
		memset(u, 0, min);
		for(fp=ftab; fp<ftab+nf; fp++){
			if(fp->fd < 0)
				continue;
			for(p=u, q=fp->buf; q<fp->buf+min; p+=2, q+=2){
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
		write(1, u, min);
		last = min;
	}
end:
	exits(nil);
}
