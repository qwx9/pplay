#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Dot dot;
usize totalsz;
int treadsoftly;

static int epfd[2];

static int
paste(char *)
{
	usize from, to;

	from = dot.from;
	to = dot.to;
	if(latchedpos >= 0 && dot.from == 0 && dot.to == totalsz)
		to = from = latchedpos;
	return cpaste(from, to) == 0 ? 1 : -1;
}

static int
copy(char *)
{
	ccopy(dot.from, dot.to);
	return 0;
}

static vlong
cut(char *)
{
	dprint(nil, "cmd/cut %Δ\n", &dot);
	if(dot.from == 0 && dot.to == totalsz){
		werrstr("cut: can't cut entire buffer");
		return -1;
	}
	ccut(dot.from, dot.to);
	return 1;
}

static int
crop(char *)
{
	dprint(nil, "cmd/crop %Δ\n", &dot);
	ccrop(dot.from, dot.to);
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
	d.pos = d.from;
	if((nio = iounit(fd)) == 0)
		nio = 8192;
	for(m=d.to-d.from, b=(uchar*)&d; m>0; m-=n, d.pos+=n){
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
	Dot d;
	Chunk *c;

	d = dot;
	treadsoftly++;
	fd = (intptr)efd;
	if((c = chunkfile(fd)) == nil){
		treadsoftly = 0;
		threadexits("failed reading from pipe: %r");
	}
	close(fd);
	qlock(&lsync);
	dot.from = d.from;
	dot.to = d.to;
	chold(c);
	paste(nil);
	qunlock(&lsync);
	treadsoftly--;
	redraw(0);
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
	switch(r){
	case '<': x = pipefrom(s); break;
	case '^': x = pipethrough(s); break;
	case '|': x = pipeto(s); break;
	case 'c': x = copy(s); break;
	case 'd': x = cut(s); break;
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
	return x;
}

int
advance(Dot *d, usize n)
{
	usize m;

	assert(d->pos >= d->from && d->pos <= d->to);
	while(n > 0){
		m = d->to - d->pos > n ? n : d->to - d->pos;
		n -= m;
		d->pos += m;
		if(d->pos == d->to)
			d->pos = d->from;
	}
	return 0;
}

int
loadin(int fd)
{
	Chunk *c;

	if((c = chunkfile(fd)) == nil)
		sysfatal("loadin: %r");
	initbuf(c);
	return 0;
}

static void
catch(void *, char *msg)
{
	if(strstr(msg, "closed pipe"))
		noted(NCONT);
	noted(NDFLT);
}

void
initcmd(void)
{
	notify(catch);
}
