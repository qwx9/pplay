#include <u.h>
#include <libc.h>
#include <thread.h>
#include "dat.h"
#include "fns.h"

/* chunk ≡ chain of chunks */
struct Buf{
	uchar *buf;
	usize bufsz;
	Ref;
};
static Chunk *norris;

int
Δfmt(Fmt *fmt)
{
	Dot *d;

	d = va_arg(fmt->args, Dot*);
	if(d == nil)
		return fmtstrcpy(fmt, "[??:??:??:??]");
	return fmtprint(fmt, "[from=%08zux cur=%08zux at=%08zux to=%08zux]",
		d->from, d->pos, d->at, d->to);
}

int
χfmt(Fmt *fmt)
{
	Chunk *c;

	c = va_arg(fmt->args, Chunk*);
	if(c == nil)
		return fmtstrcpy(fmt, "[]");
	return fmtprint(fmt, "0x%08p:%08zux::0x%08p:0x%08p", c, c->len, c->left, c->right);
}

static void
printchunks(Chunk *r)
{
	usize len;
	Chunk *c;

	c = r;
	len = 0;
	do{
		fprint(2, "\t%χ\toff=%08zux\n", c, len);
		assert(c->right->left == c);
		len += c->len;
		c = c->right;
	}while(c != r);
	fprint(2, "\n");
}

void
dprint(Chunk *c, char *fmt, ...)
{
	char s[256];
	va_list arg;

	if(!debug)
		return;
	va_start(arg, fmt);
	vseprint(s, s+sizeof s, fmt, arg);
	va_end(arg);
	fprint(2, "%s", s);
	printchunks(c == nil ? norris : c);
}

static Chunk *
newchunk(Buf *b)
{
	Chunk *c;

	c = emalloc(sizeof *c);
	c->left = c;
	c->right = c;
	c->b = b;
	c->off = 0;
	c->len = b->bufsz;
	incref(&b->Ref);
	return c;
}
static Chunk *
newbuf(usize n)
{
	Buf *b;

	assert((n & 3) == 0);
	b = emalloc(sizeof *b);
	b->bufsz = n;
	b->buf = emalloc(b->bufsz);
	return newchunk(b);
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
unlink(Chunk *left, Chunk *right)
{
	left->left->right = right->right;
	right->right->left = left->left;
	left->left = right;
	right->right = left;
}

static Chunk *
clonechunk(Chunk *c)
{
	Chunk *nc;

	assert(c != nil && c->b != nil);
	nc = newchunk(c->b);
	nc->off = c->off;
	nc->len = c->len;
	incref(c->b);
	return nc;
}
Chunk *
clone(Chunk *left, Chunk *right)
{
	Chunk *cl, *c, *nc;

	cl = clonechunk(left);
	c = cl;
	for(; left!=right;){
		left = left->right;
		nc = clonechunk(left);
		linkchunk(c, nc);
		c = nc;
	}
	return cl;
}

static void
freebuf(Buf *b)
{
	if(b == nil)
		return;
	free(b->buf);
	free(b);
}
static void
freechunk(Chunk *c)
{
	if(c == nil)
		return;
	if(c->b != nil && decref(c->b) == 0)
		freebuf(c->b);
	free(c);
}
void
freechain(Chunk *c)
{
	Chunk *cl, *cp;

	if(c == nil)
		return;
	for(cl=c->right; cl!=c; cl=cp){
		cp = cl->right;
		unlink(cl, cl);
		freechunk(cl);
	}
	freechunk(c);
}

static void
shrinkbuf(Chunk *c, usize newsz)
{
	Buf *b;

	b = c->b;
	assert(b != nil);
	assert(newsz < b->bufsz && newsz > 0);
	if(c->off + c->len > newsz)
		c->len = newsz - c->off;
	b->buf = erealloc(b->buf, newsz, b->bufsz);
}

usize
chunklen(Chunk *c)
{
	usize n;
	Chunk *cp;

	for(cp=c, n=cp->len, cp=cp->right; cp!=c; cp=cp->right)
		n += cp->len;
	return n;
}

Chunk *
p2c(usize p, usize *off)
{
	Chunk *c;

	for(c=norris; p>=c->len; c=c->right){
		if(c == norris->left){
			assert(p == c->len);
			break;
		}
		p -= c->len;
	}
	if(off != nil)
		*off = p;
	return c;
}
usize
c2p(Chunk *tc)
{
	Chunk *c;
	usize p;

	for(p=0, c=norris; c!=tc; c=c->right)
		p += c->len;
	return p;
}

void
recalcsize(void)
{
	int n;

	n = c2p(norris->left) + norris->left->len;
	if(dot.to == totalsz || dot.to > n)
		dot.to = n;
	if(dot.pos < dot.from || dot.pos > dot.to)
		dot.pos = dot.from;
	dot.at = -1ULL;
	dprint(nil, "final %Δ\n", &dot);
	totalsz = n;
}

#define ASSERT(x) {if(!(x)) printchunks(norris); assert((x)); }
void
paranoia(int exact)
{
	usize n;
	Chunk *c, *pc;
	Buf *b;

	ASSERT(dot.pos >= dot.from && dot.pos < dot.to);
	for(pc=norris, n=pc->len, c=pc->right; c!=norris; pc=c, c=c->right){
		b = c->b;
		ASSERT(b != nil);
		ASSERT((b->bufsz & 3) == 0 && b->bufsz >= Sampsz);
		ASSERT(c->off < b->bufsz);
		ASSERT(c->len > Sampsz);
		ASSERT(c->off + c->len <= b->bufsz);
		ASSERT(c->left == pc);
		n += c->len;
	}
	if(exact){
		ASSERT(n <= totalsz);
		ASSERT(dot.to <= totalsz);
	}
}
#undef ASSERT

void
setdot(Dot *dot, Chunk *right)
{
	dot->from = 0;
	if(right == nil)
		dot->to = c2p(norris->left) + norris->left->len;
	else
		dot->to = c2p(right);
	dot->at = -1ULL;
}

void
fixroot(Chunk *rc, usize off)
{
	Chunk *c;

	dprint(rc, "fixroot [%χ] %08zux\n", rc, off);
	for(c=rc->left; off>0; off-=c->len, c=c->left){
		if(off - c->len == 0)
			break;
		assert(off - c->len < off);
	}
	norris = c;
}

Chunk *
splitchunk(Chunk *c, usize off)
{
	Chunk *nc;

	dprint(nil, "splitchunk %Δ [%χ] off=%08zux\n", &dot, c, off);
	if(off == 0 || c == norris->left && off == c->len)
		return c;
	assert(off <= c->len);
	nc = clonechunk(c);
	nc->off = c->off + off;
	nc->len = c->len - off;
	c->len = off;
	linkchunk(c, nc);
	return nc;
}

/* c1 [nc … c2] nc */
int
splitrange(usize from, usize to, Chunk **left, Chunk **right)
{
	usize off;
	Chunk *c;

	dprint(nil, "splitrange from=%08zux to=%08zux\n", from, to);
	c = p2c(from, &off);
	if(off > 0){
		splitchunk(c, off);
		*left = c->right;
	}else
		*left = c;	/* dangerous in combination with *right */
	c = p2c(to, &off);
	if(off < c->len - 1){
		splitchunk(c, off);
		*right = c;
	}else
		*right = c;
	return 0;
}

Chunk *
cutrange(usize from, usize to, Chunk **latch)
{
	Chunk *c, *left, *right;

	dprint(nil, "cutrange from=%08zux to=%08zux\n", from, to);
	if(splitrange(from, to, &left, &right) < 0)
		return nil;
	c = left->left;
	if(left == norris)
		norris = right->right;
	unlink(left, right);
	if(latch != nil)
		*latch = left;
	return c;
}

Chunk *
croprange(usize from, usize to, Chunk **latch)
{
	Chunk *cut, *left, *right;

	dprint(nil, "croprange from=%08zux to=%08zux\n", from, to);
	if(splitrange(from, to, &left, &right) < 0)
		return nil;
	norris = left;
	cut = right->right;
	if(latch != nil)
		*latch = cut;
	unlink(right->right, left->left);
	return left;
}

// FIXME: generalized insert(from, to), where from and to not necessarily distinct
Chunk *
inserton(usize from, usize to, Chunk *c, Chunk **latch)
{
	Chunk *left;

	dprint(c, "inserton from=%08zux to=%08zux\n", from, to);
	left = cutrange(from, to, latch);
	linkchunk(left, c);
	dprint(nil, "done\n");
	return left;
}

Chunk *
insertat(usize pos, Chunk *c)
{
	usize off;
	Chunk *left;

	dprint(c, "insertat cur=%08zux\n", pos);
	if(pos == 0){
		left = norris->left;
		norris = c;
	}else{
		left = p2c(pos, &off);
		splitchunk(left, off);
	}
	if(off == 0)
		left = left->left;
	linkchunk(left, c);
	return left;
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
	Δloop = d->to - d->pos;
	Δbuf = c->len - off;
	if(n < Δloop && n < Δbuf){
		*sz = n;
		d->pos += n;
	}else if(Δloop <= Δbuf){
		*sz = Δloop;
		d->pos = d->from;
	}else{
		*sz = Δbuf;
		d->pos += Δbuf;
	}
	return c->b->buf + c->off + off;
}

Chunk *
readintochunks(int fd)
{
	int n;
	usize m;
	Chunk *rc, *c, *nc;

	for(m=0, rc=c=nil;; m+=n){
		nc = newbuf(Iochunksz);
		if(rc == nil)
			rc = nc;
		else
			linkchunk(c, nc);
		c = nc;
		if((n = readn(fd, c->b->buf, Iochunksz)) < Iochunksz)
			break;
		yield();
	}
	close(fd);
	if(n < 0)
		fprint(2, "readintochunks: %r\n");
	else if(n == 0){
		if(c != rc)
			unlink(c, c);
		freechunk(c);
		if(c == rc){
			werrstr("readintochunks: nothing read");
			return nil;
		}
	}else if(n > 0 && n < Iochunksz)
		shrinkbuf(c, n);
	return rc;
}

void
graphfrom(Chunk *c)
{
	norris = c;
	recalcsize();
	setdot(&dot, nil);
}
