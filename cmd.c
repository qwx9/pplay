#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

usize ndots;
Dot *current, *dots;

static int epfd[2];

// FIXME: cursor moves too fast
static int
setright(char *s)
{
	vlong off, m;

	off = atoll(s) * Sampsz;
	if(current->cur > off){
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
	if(current->cur < off)
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
	r = cpaste(current) == 0 ? 1 : -1;
	qunlock(&lsync);
	return r;
}

static int
copy(char *)
{
	ccopy(current);
	return 0;
}

static vlong
cut(char *)
{
	dprint(nil, "cmd/cut %Δ\n", current);
	if(current->from == 0 && current->to == current->totalsz){
		werrstr("cut: can't cut entire buffer");
		return -1;
	}
	qlock(&lsync);
	ccut(current);
	qunlock(&lsync);
	return 1;
}

static int
crop(char *)
{
	dprint(nil, "cmd/crop %Δ\n", current);
	qlock(&lsync);
	ccrop(current);
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

	d = *current;
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
	procexecl(nil, "/bin/rc", "rc", "-c", s, nil);
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

	d = *current;
	fd = (intptr)efd;
	if((c = loadfile(fd, &cd)) == nil){
		fprint(2, "failed reading from pipe: %r");
		threadexits("read error");
	}
	close(fd);
	qlock(&lsync);
	chold(c, &d);
	*current = d;
	qunlock(&lsync);
	if(paste(nil) < 0)
		fprint(2, "paste: %r\n");
	redraw(1);
	threadexits(nil);
}

// FIXME: make sure writes complete even after exit
static int
pipeline(char *arg, int rr, int wr)
{
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
	Rune r, r´;

	/* FIXME: avoid potential conflicts with keys in main() */
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
	dprint(current->norris, "current dot=%Δ\n", current);
	switch(r){
	case '<': x = pipefrom(s); break;
	case '^': x = pipethrough(s); break;
	case '|': x = pipeto(s); break;
	case 'L': x = setleft(s); break;
	case 'R': x = setright(s); break;
	case 'c': x = copy(s); break;
	case 'd': x = cut(s); break;
	case 'j': x = jumpto(s); break;
	//case 'm': x = mark(s); break;
	case 'p': x = paste(s); break;
	case 'q': threadexitsall(nil);
	case 'r': x = readfrom(s); break;
	case 's': x = replicate(s); break;
	case 'U': x = unpop(s); break;
	case 'u': x = popop(s); break;
	case 'w': x = writeto(s); break;
	case 'x': x = crop(s); break;
	default: werrstr("unknown command %C", r); x = -1; break;
	}
	dprint(current->norris, "final dot=%Δ\n", current);
	return x;
}

int
advance(Dot *d, usize n)
{
	usize m;

	assert(d->cur >= d->from && d->cur <= d->to);
	while(n > 0){
		m = d->to - d->cur > n ? n : d->to - d->cur;
		n -= m;
		d->cur += m;
		if(d->cur == d->to)
			d->cur = d->from;
	}
	return 0;
}

static void
catch(void *, char *msg)
{
	if(strstr(msg, "closed pipe"))
		noted(NCONT);
	noted(NDFLT);
}

int
initcmd(int fd)
{
	Dot d;

	if(loadfile(fd, &d) == nil)
		sysfatal("initcmd: %r");
	dots = emalloc(++ndots * sizeof *dots);
	dots[0] = d;
	current = dots;
	return 0;
}
