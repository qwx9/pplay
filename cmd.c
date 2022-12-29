#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Dot dot;
usize totalsz;
int treadsoftly;

// FIXME: undo/redo as an unbatched series of inserts and deletes
// FIXME: crazy idea, multisnarf with addressable elements; $n registers; fork pplay to display them → ?

enum{
	Nhold = 64,
};
static Chunk *hold[Nhold], *snarf;
static int epfd[2];

void
setrange(usize from, usize to)
{
	assert((from & 3) == 0);
	assert((to & 3) == 0);
	dot.from.pos = from;
	dot.to.pos = to;
	if(dot.pos < from || dot.pos >= to)
		dot.pos = from;
}

int
setpos(usize off)
{
	assert((off & 3) == 0);
	setrange(0, totalsz);
	assert(off >= dot.from.pos && off < dot.to.pos);
	dot.pos = off;
	return 0;
}

int
jump(usize off)
{
	if(off < dot.from.pos || off > dot.to.pos){
		werrstr("cannot jump outside of loop bounds\n");
		return -1;
	}
	dot.pos = off;
	return 0;
}

static int
replace(char *, Chunk *c)
{
	Chunk *left, *latch;

	if(c == nil){
		fprint(2, "replace: nothing to paste\n");
		return -1;
	}
	if((left = inserton(dot.from.pos, dot.to.pos, c, &latch)) == nil){
		fprint(2, "insert: %r\n");
		return -1;
	}
	setdot(&dot, nil);
	dot.pos = c2p(left->right);
	return 1;
}

static int
insert(char *, Chunk *c)
{
	Chunk *left;

	if(c == nil){
		fprint(2, "insert: nothing to paste\n");
		return -1;
	}
	if((left = insertat(dot.pos, c)) == nil){
		fprint(2, "insert: %r\n");
		return -1;
	}
	setdot(&dot, nil);
	dot.pos = c2p(left->right);
	return 1;
}

static int
paste(char *s, Chunk *c)
{
	if(c == nil && (c = snarf) == nil){
		werrstr("paste: no buffer");
		return -1;
	}
	c = replicate(c, c->left);
	if(dot.from.pos == 0 && dot.to.pos == totalsz)
		return insert(s, c);
	else
		return replace(s, c);
}

static int
copy(char *)
{
	Chunk *c, *left, *right;

	splitrange(dot.from.pos, dot.to.pos, &left, &right);
	c = replicate(left, right);
	freechain(snarf);
	snarf = c;
	return 0;
}

static vlong
cut(char *)
{
	Chunk *latch;

	if(dot.from.pos == 0 && dot.to.pos == totalsz){
		werrstr("cut: no range selected");
		return -1;
	}
	cutrange(dot.from.pos, dot.to.pos, &latch);
	setdot(&dot, nil);
	return 1;
}

static int
crop(char *)
{
	Chunk *latch;

	if(croprange(dot.from.pos, dot.to.pos, &latch) == nil)
		return -1;
	setdot(&dot, nil);
	dot.pos = 0;
	return 1;
}

vlong
getbuf(Dot d, usize n, uchar *buf, usize bufsz)
{
	uchar *p, *b;
	usize sz;

	assert(d.pos < totalsz);
	assert(n <= bufsz);
	b = buf;
	while(n > 0){
		if((p = getslice(&d, n, &sz)) == nil || sz < Sampsz)
			return -1;
		memcpy(b, p, sz);
		b += sz;
		n -= sz;
	}
	return b - buf;
}

static int
writebuf(int fd)
{
	static uchar *buf;
	static usize bufsz;
	int nio;
	usize n, m, c, k;
	Dot d;

	d.pos = d.from.pos = dot.from.pos;
	d.to.pos = dot.to.pos;
	if((nio = iounit(fd)) == 0)
		nio = 8192;
	if(bufsz < nio){
		buf = erealloc(buf, nio, bufsz);
		bufsz = nio;
	}
	for(m=d.to.pos-d.from.pos, c=0; m>0;){
		k = nio < m ? nio : m;
		if(getbuf(d, k, buf, bufsz) < 0){
			fprint(2, "writebuf: couldn\'t snarf: %r\n");
			return -1;
		}
		if((n = write(fd, buf, k)) != k){
			fprint(2, "writebuf: short write not %zd: %r\n", k);
			return -1;
		}
		m -= n;
		d.pos += n;
		c += n;
	}
	write(fd, buf, 0);	/* close pipe */
	return 0;
}

int
advance(Dot *d, usize n)
{
	usize m, sz;

	m = 0;
	while(n > 0){
		if(getslice(d, n, &sz) == nil)
			return -1;
		m += sz;
		n -= sz;
	}
	return m;
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
/* using a thread does slow down reads a bit */
// FIXME: ugly
static void
rthread(void *efd)
{
	int fd;
	Dot d;
	Chunk *c;

	d = dot;
	treadsoftly = 1;
	fd = (intptr)efd;
	if((c = readintochunks(fd)) == nil)
		threadexits("failed reading from pipe: %r");
	close(fd);
	dot = d;
	paste(nil, c);
	dot.pos = dot.from.pos;
	setdot(&dot, nil);
	recalcsize();
	redraw(0);
	treadsoftly = 0;
	threadexits(nil);
}

static int
pipeline(char *arg, int rr, int wr)
{
	if(pipe(epfd) < 0)
		sysfatal("pipe: %r");
	if(procrfork(rc, arg, mainstacksize, RFFDG|RFNOTEG|RFNAMEG) < 0)
		sysfatal("procrfork: %r");
	close(epfd[0]);
	if(wr && procrfork(wproc, (int*)dup(epfd[1], -1), mainstacksize, RFFDG) < 0){
		fprint(2, "procrfork: %r\n");
		return -1;
	}
	if(rr && threadcreate(rthread, (int*)dup(epfd[1], -1), mainstacksize) < 0){
		fprint(2, "threadcreate: %r\n");
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
readfrom(char *s)
{
	int fd;

	if((fd = open(s, OREAD)) < 0)
		return -1;
	if(threadcreate(rthread, (int*)fd, mainstacksize) < 0){
		fprint(2, "threadcreate: %r\n");
		return -1;
	}
	return 0;
}

/* the entire string is treated as the filename, ie.
 * spaces and any other weird characters will be part
 * of it */
static int
writeto(char *arg)
{
	int r, fd;

	if(dot.to.pos - dot.from.pos == 0){
		werrstr("writeto: dot isn't a range");
		return -1;
	}
	if((fd = create(arg, OWRITE, 0664)) < 0){
		werrstr("writeto: %r");
		return -1;
	}
	r = writebuf(fd);
	close(fd);
	return r;
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
	if(debug)
		paranoia(1);
	switch(r){
	case '<': x = pipefrom(s); break;
	case '^': x = pipethrough(s); break;
	case '|': x = pipeto(s); break;
	case 'c': x = copy(s); break;
	case 'd': x = cut(s); break;
//	case 'm': x = forcemerge(s); break;
	case 'p': x = paste(s, nil); break;
	case 'q': threadexitsall(nil);
	case 'r': x = readfrom(s); break;
	case 'w': x = writeto(s); break;
	case 'x': x = crop(s); break;
	default: werrstr("unknown command %C", r); x = -1; break;
	}
	if(debug)
		paranoia(0);
	recalcsize();
	return x;
}

int
loadin(int fd)
{
	Chunk *c;

	if((c = readintochunks(fd)) == nil)
		sysfatal("loadin: %r");
	graphfrom(c);
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
