#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

Dot dot;
usize totalsz;
static Chunk norris = {.left = &norris, .right = &norris};
static Chunk *held;
static uchar plentyofroom[Iochunksz];
static int cutheld;
static int epfd[2];

static void
printchunks(Chunk *r)
{
	Chunk *c;

	fprint(2, "chunklist dot %zux %zux %zux: ", 
		dot.from.pos, dot.pos, dot.to.pos);
	c = r;
	do{
		fprint(2, "%#p:%zux:←%#p→%#p - ", c, c->bufsz, c->left, c->right);
		assert(c->right->left == c);
		c = c->right;
	}while(c != r);
	fprint(2, "\n");
}

static void
recalcsize(void)
{
	int n;
	Chunk *c;

	for(c=norris.right, n=0; c!=&norris; c=c->right)
		n += c->bufsz;
	if(dot.to.pos == totalsz || n < totalsz && dot.to.pos > n)
		dot.to.pos = n;
	totalsz = n;
}

#define ASSERT(x) {if(!(x)) printchunks(&norris); assert((x)); }
static void
paranoia(void)
{
	Chunk *c;

	ASSERT(dot.pos >= dot.from.pos && dot.pos < dot.to.pos);
	ASSERT(dot.to.pos <= totalsz);
	for(c=norris.right; c!=&norris; c=c->right){
		ASSERT(c->buf != nil);
		ASSERT((c->bufsz & 3) == 0 && c->bufsz >= Sampsz);
	}
}

static Chunk *
newchunk(usize n)
{
	Chunk *c;

	assert((n & 3) == 0);
	c = emalloc(sizeof *c);
	c->bufsz = n;
	c->buf = emalloc(c->bufsz);
	c->left = c;
	c->right = c;
	return c;
}

static Chunk *
clonechunk(void)
{
	Chunk *c;

	assert(held != nil);
	c = newchunk(held->bufsz);
	memcpy(c->buf, held->buf, c->bufsz);
	return c;
}

static void
freechunk(Chunk *c)
{
	if(c == nil)
		return;
	free(c->buf);
	free(c);
}

static void
linkchunk(Chunk *left, Chunk *c)
{
	c->left->right = left->right;
	left->right->left = c->left;
	c->left = left;
	left->right = c;
}

static void
unlinkchunk(Chunk *c)
{
	c->left->right = c->right;
	c->right->left = c->left;
	c->left = c->right = nil;
}

static void
resizechunk(Chunk *c, usize newsz)
{
	vlong Δ;

	Δ = newsz - c->bufsz;
	c->buf = erealloc(c->buf, newsz, c->bufsz);
	c->bufsz = newsz;
	if(c->right == &norris && Δ < 0)
		dot.to.pos += Δ;
}

Chunk *
p2c(usize p, usize *off)
{
	Chunk *c;

	assert(p < totalsz);
	c = norris.right;
	while(p >= c->bufsz){
		p -= c->bufsz;
		c = c->right;
	}
	if(off != nil)
		*off = p;
	assert(c != &norris);
	return c;
}
static usize
c2p(Chunk *tc)
{
	Chunk *c;
	usize p;

	p = 0;
	c = norris.right;
	while(c != tc){
		p += c->bufsz;
		c = c->right;
	}
	return p;
}

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
holdchunk(Chunk *c, int cut)
{
	if(held != nil){
		if(held == c)
			return 0;
		else if(cutheld)
			freechunk(held);
	}
	held = c;
	cutheld = cut;
	if(cut){
		unlinkchunk(c);
		setpos(dot.from.pos);
	}
	return 0;
}

static Chunk *
merge(Chunk *left, Chunk *right)
{
	usize Δ;

	assert(right != &norris);
	if(left->buf == nil || right->buf == nil){
		werrstr("can\'t merge self into void");
		return nil;
	}
	if(left->buf != right->buf){
		Δ = left->bufsz;
		resizechunk(left, left->bufsz + right->bufsz);
		memmove(left->buf + Δ, right->buf, right->bufsz);
	}else{
		right->buf = nil;
		left->bufsz += right->bufsz;
	}
	unlinkchunk(right);
	freechunk(right);
	return 0;
}

static Chunk *
splitright(Chunk *left, usize off)
{
	usize Δ;
	Chunk *c;

	Δ = left->bufsz - off;
	if(off == 0 || Δ == 0)
		return left;
	c = newchunk(Δ);
	memcpy(c->buf, left->buf+off, Δ);
	resizechunk(left, off);
	linkchunk(left, c);
	return c;
}

static Chunk *
mergedot(usize *off)
{
	usize p;
	Chunk *c;

	c = p2c(dot.from.pos, &p);
	*off = p;
	p = dot.from.pos - p;
	while(p + c->bufsz < dot.to.pos)
		merge(c, c->right);
	return c;
}

/* before one may split oneself, one must first merge oneself */
static Chunk *
splitdot(void)
{
	usize p;
	Chunk *c;

	c = mergedot(&p);
	splitright(c, p + dot.to.pos - dot.from.pos);
	return splitright(c, p);
}

uchar *
getslice(Dot *d, usize n, usize *sz)
{
	usize Δbuf, Δloop, off;
	Chunk *c;

	if(d->pos >= totalsz){
		werrstr("out of bounds");
		*sz = 0;
		return nil;
	}
	c = p2c(d->pos, &off);
	Δloop = d->to.pos - d->pos;
	Δbuf = c->bufsz - off;
	if(n < Δloop && n < Δbuf){
		*sz = n;
		d->pos += n;
	}else if(Δloop <= Δbuf){
		*sz = Δloop;
		d->pos = d->from.pos;
	}else{
		*sz = Δbuf;
		d->pos += Δbuf;
	}
	return c->buf + off;
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

void
advance(Dot *d, usize n)
{
	usize Δ, Δbuf, Δloop, m, off;
	Chunk *c;

	c = p2c(d->pos, &off);
	m = n;
	while(m > 0){
		Δloop = d->to.pos - d->pos;
		Δbuf = c->bufsz - off;
		if(m < Δloop && m < Δbuf){
			d->pos += m;
			break;
		}else if(Δloop < Δbuf){
			Δ = Δloop;
			d->pos = d->from.pos;
			c = p2c(d->from.pos, nil);
			off = 0;
		}else{
			Δ = Δbuf;
			d->pos += Δ;
			c = c->right;
			off = 0;
		}
		m -= Δ;
	}
}

static int
insert(char *, Chunk *c)
{
	usize p;
	Chunk *left;

	if(c == nil && (c = clonechunk()) == nil){
		werrstr("insert: no buffer");
		return -1;
	}
	left = p2c(dot.pos, &p);
	splitright(left, p);
	linkchunk(left, c);
	setrange(dot.pos, dot.pos + c->bufsz);
	return 1;
}

static int
copy(char *)
{
	Chunk *c;

	c = splitdot();
	holdchunk(c, 0);
	return 0;
}

static int
cut(char *)
{
	Chunk *c;

	if(dot.from.pos == 0 && dot.to.pos == totalsz){
		werrstr("cut: no range selected");
		return -1;
	}
	c = splitdot();
	holdchunk(c, 1);
	return 1;
}

static int
replace(char *, Chunk *c)
{
	Chunk *left, *right;

	if(c == nil && (c = clonechunk()) == nil){
		werrstr("replace: no buffer");
		return -1;
	}
	right = splitdot();
	left = right->left;
	unlinkchunk(right);
	freechunk(right);
	right = left->right;
	linkchunk(left, c);
	setrange(dot.from.pos, right != &norris ? c2p(right) : totalsz);
	return 1;
}

static int
paste(char *s, Chunk *c)
{
	if(dot.from.pos == 0 && dot.to.pos == totalsz)
		return insert(s, c);
	return replace(s, c);
}

static int
crop(char *)
{
	usize Δ;
	Chunk *c, *d;

	Δ = 0;
	for(c=norris.right; c!=&norris; c=d){
		if(Δ + c->bufsz >= dot.from.pos)
			break;
		d = c->right;
		Δ += c->bufsz;
		unlinkchunk(c);
		freechunk(c);
	}
	dot.from.pos -= Δ;
	dot.to.pos -= Δ;
	if(dot.from.pos > 0){
		Δ = c->bufsz - dot.from.pos;
		memmove(c->buf, c->buf + dot.from.pos, Δ);
		resizechunk(c, Δ);
		dot.to.pos -= dot.from.pos;
		dot.from.pos = 0;
	}
	for(; c!=&norris; dot.to.pos-=c->bufsz, c=c->right)
		if(c->bufsz >= dot.to.pos)
			break;
	if(dot.to.pos > 0)
		resizechunk(c, dot.to.pos);
	for(c=c->right; c!=&norris; c=d){
		d = c->right;
		unlinkchunk(c);
		freechunk(c);
	}
	dot.pos = 0;
	dot.to.pos = totalsz;
	return 1;
}

static int
forcemerge(char *)
{
	usize p;

	if(dot.from.pos == 0 && dot.to.pos == totalsz){
		werrstr("merge: won\'t implicitely merge entire buffer\n");
		return -1;
	}
	mergedot(&p);
	return 0;
}

static Chunk *
readintochunks(int fd)
{
	int n;
	usize m;
	Chunk *rc, *c, *nc;

	for(m=0, rc=c=nil;; m+=n){
		nc = newchunk(Iochunksz);
		if(rc == nil)
			rc = nc;
		else
			linkchunk(c, nc);
		c = nc;
		if((n = readn(fd, c->buf, Iochunksz)) < Iochunksz)
			break;
		yield();
	}
	close(fd);
	if(n < 0)
		fprint(2, "readintochunks: %r\n");
	else if(n == 0){
		if(c != rc)
			unlinkchunk(c);
		freechunk(c);
		if(c == rc){
			werrstr("readintochunks: nothing read");
			return nil;
		}
	}else if(n > 0 && n < Iochunksz)
		resizechunk(c, n);
	return rc;
}

static int
readfrom(char *s)
{
	int fd;
	Chunk *c;

	if((fd = open(s, OREAD)) < 0)
		return -1;
	c = readintochunks(fd);
	close(fd);
	if(c == nil)
		return -1;
	return paste(nil, c);
}

static int
writebuf(int fd)
{
	static uchar *buf;
	static usize bufsz;
	int nio;
	usize n, m, c, k;
	uchar *p;
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
rthread(void *efd)
{
	int fd;
	Chunk *c;

	fd = (intptr)efd;
	if((c = readintochunks(fd)) == nil)
		threadexits("failed reading from pipe: %r");
	close(fd);
	paste(nil, c);
	recalcsize();
	redraw(0);
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
	switch(r){
	case '<': x = pipefrom(s); break;
	case '^': x = pipethrough(s); break;
	case '|': x = pipeto(s); break;
	case 'c': x = copy(s); break;
	case 'd': x = cut(s); break;
	case 'm': x = forcemerge(s); break;
	case 'p': x = paste(s, nil); break;
	case 'q': threadexitsall(nil);
	case 'r': x = readfrom(s); break;
	case 'w': x = writeto(s); break;
	case 'x': x = crop(s); break;
	default: werrstr("unknown command %C", r); x = -1; break;
	}
	if(debug)
		paranoia();
	recalcsize();
	return x;
}

int
loadin(int fd)
{
	Chunk *c;

	if((c = readintochunks(fd)) == nil)
		sysfatal("loadin: %r");
	linkchunk(&norris, c);
	recalcsize();
	setrange(0, totalsz);
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
