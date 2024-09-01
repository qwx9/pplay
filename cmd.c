#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

extern Channel *pidc;

Dot dot;
int bound;

static int epfd[2];

static int
setright(char *s)
{
	vlong off, m;

	off = atoll(s) * Sampsz;
	if(dot.cur > off){
		m = Rate * Sampsz;
		setjump(off - m >= 0 ? off - m : 0);
	}
	return setloop(off);
}

static int
setleft(char *s)
{
	vlong off;

	off = atoll(s) * Sampsz;
	if(dot.cur < off)
		setjump(off + Sampsz);
	return setloop(off);
}

static int
jumpto(char *s)
{
	return setjump(atoll(s) * Sampsz);
}

static int
paste(char *)
{
	int r;

	qlock(&lsync);
	r = cpaste(&dot) == 0 ? 1 : -1;
	qunlock(&lsync);
	return r;
}

static int
copy(char *)
{
	ccopy(&dot);
	return 0;
}

static int
cut(char *)
{
	dprint(nil, "cmd/cut %Δ\n", &dot);
	if(dot.from == 0 && dot.to == dot.totalsz){
		werrstr("cut: can't cut entire buffer");
		return -1;
	}
	qlock(&lsync);
	ccut(&dot);
	qunlock(&lsync);
	return 1;
}

static int
crop(char *)
{
	dprint(nil, "cmd/crop %Δ\n", &dot);
	qlock(&lsync);
	ccrop(&dot);
	qunlock(&lsync);
	return 1;
}

static int
writebuf(int fd)
{
	int nio;
	uchar *b;
	usize n, m, k;
	Dot d;

	d = dot;
	d.cur = d.from;
	if((nio = iounit(fd)) == 0)
		nio = 8192;
	for(m=d.to-d.from, b=(uchar*)&d; m>0; m-=n, d.cur+=n){
		k = nio < m ? nio : m;
		if((b = getslice(&d, k, &k)) == nil || k <= 0){
			fprint(2, "writebuf: couldn\'t snarf: %r\n");
			return -1;
		}
		if((n = write(fd, b, k)) != k){
			fprint(2, "writebuf: short write not %zd: %r\n", k);
			return -1;
		}
	}
	write(fd, b, 0);	/* close pipe */
	return 0;
}

static void
rc(void *s)
{
	close(epfd[1]);
	dup(epfd[0], 0);
	dup(epfd[0], 1);
	close(epfd[0]);
	procexecl(pidc, "/bin/rc", "rc", "-c", s, nil);
	sysfatal("procexec: %r");
}

static void
wproc(void *efd)
{
	int fd;

	fd = (intptr)efd;
	writebuf(fd);
	close(fd);
	threadexits(nil);
}

static void
rproc(void *efd)
{
	int fd;
	Dot d, cd;
	Chunk *c;

	d = dot;
	if(d.from != 0 && d.to != d.totalsz)
		d.off = -1;
	fd = (intptr)efd;
	if((c = loadfile(fd, &cd)) == nil){
		fprint(2, "failed reading from pipe: %r");
		threadexits("read error");
	}
	close(fd);
	qlock(&lsync);
	chold(c, &d);
	dot = d;
	qunlock(&lsync);
	if(paste(nil) < 0)
		fprint(2, "paste: %r\n");
	redraw(1);
	threadexits(nil);
}

static int
pipeline(char *arg, int rr, int wr)
{
	if(nslots == 0){
		fprint(2, "pipeline: too many backgrounded processes\n");
		return -1;
	}
	if(rr)
		reader = 0;
	if(pipe(epfd) < 0)
		sysfatal("pipe: %r");
	if(procrfork(rc, arg, mainstacksize, RFFDG|RFNOTEG|RFNAMEG) < 0)
		sysfatal("procrfork: %r");
	close(epfd[0]);
	if(wr && procrfork(wproc, (int*)dup(epfd[1], -1), mainstacksize, RFFDG|RFNOTEG|RFNAMEG) < 0){
		fprint(2, "procrfork: %r\n");
		return -1;
	}
	if(rr && procrfork(rproc, (int*)dup(epfd[1], -1), mainstacksize, RFFDG) < 0){
		fprint(2, "procrfork: %r\n");
		return -1;
	}
	close(epfd[1]);
	return 0;
}

static int
pipeto(char *arg)
{
	return pipeline(arg, 0, 1);
}

static int
pipefrom(char *arg)
{
	return pipeline(arg, 1, 0);
}

static int
pipethrough(char *arg)
{
	return pipeline(arg, 1, 1);
}

static int
pipeselflessly(char *arg)
{
	return pipeline(arg, 0, 0);
}

static int
replicate(char *)
{
	static char u[256];

	snprint(u, sizeof u, "<[3=0] window -m %s /fd/3", argv0);
	return pipeto(u);
}

static int
readfrom(char *s)
{
	int fd;

	if((fd = open(s, OREAD)) < 0)
		return -1;
	if(procrfork(rproc, (int*)fd, mainstacksize, RFFDG) < 0){
		fprint(2, "procrfork: %r\n");
		return -1;
	}
	return 0;
}

/* the entire string is treated as the filename, ie. spaces
 * and any other weird characters will be part of it */
static int
writeto(char *arg)
{
	int fd;

	if((fd = create(arg, OWRITE, 0664)) < 0){
		werrstr("writeto: %r");
		return -1;
	}
	if(procrfork(wproc, (int*)fd, mainstacksize, RFFDG|RFNOTEG|RFNAMEG) < 0){
		fprint(2, "procrfork: %r\n");
		return -1;
	}
	close(fd);
	return 0;
}

int
cmd(char *s)
{
	int n, x;
	int (*fn)(char*);
	Rune r, r´;

	assert(s != nil);
	s += chartorune(&r, s);
	for(;;){
		n = chartorune(&r´, s);
		if(r´ == Runeerror){
			werrstr("malformed input");
			return -1;
		}
		if(r´ == 0 || r´ != ' ' && r´ != '\t')
			break;
		s += n;
	}
	dprint(dot.norris, "current dot=%Δ\n", &dot);
	if(reader >= 0){
		switch(r){
		case '<':
		case '^':
		case 'c':
		case 'd':
		//case 'm':
		case 'p':
		case 'r':
		case 'U':
		case 'u':
		case 'w':
		case 'x':
			werrstr("still reading from external process\n");
			return -1;
		}
	}
	x = 666;
	fn = nil;
	switch(r){
	case 'q': threadexitsall(nil);
	case 'D': killreader(); break;
	case '<': fn = pipefrom; break;
	case '^': fn = pipethrough; break;
	case '|': fn = pipeto; break;
	case '!': fn = pipeselflessly; break;
	case 'L': fn = setleft; break;
	case 'R': fn = setright; break;
	case 'c': fn = copy; break;
	case 'd': fn = cut; break;
	case 'j': fn = jumpto; break;
	//case 'm': fn = mark; break;
	case 'p': fn = paste; break;
	case 'r': fn = readfrom; break;
	case 's': fn = replicate; break;
	case 'U': fn = unpop; break;
	case 'u': fn = popop; break;
	case 'w': fn = writeto; break;
	case 'x': fn = crop; break;
	default:
		werrstr("unknown command %C", r);
		return -1;
	}
	if(fn != nil){
		x = fn(s);
		dprint(dot.norris, "final dot=%Δ\n", &dot);
	}
	return x;
}

static void
advanceone(Dot *d, usize n)
{
	usize m;

	if(d->cur < d->from || d->cur >= d->to)
		d->cur = d->from;
	while(n > 0){
		m = d->to - d->cur > n ? n : d->to - d->cur;
		n -= m;
		d->cur += m;
		if(d->cur == d->to)
			d->cur = d->from;
	}
}

void
advance(usize n)
{
	advanceone(&dot, n);
}
