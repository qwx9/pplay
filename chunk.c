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

typedef struct Op Op;
struct Op{
	Chunk *p1;
	Chunk *p2;
	Chunk *l;
	Chunk *r;
	Dot *dot;
};
static Op *opbuf, *ophead, *opend;
static usize opbufsz;

static struct{
	Chunk *c;
	Dot;
} hold;

int
Δfmt(Fmt *fmt)
{
	Dot *d;

	d = va_arg(fmt->args, Dot*);
	if(d == nil)
		return fmtstrcpy(fmt, "[??:??:??:??]");
	return fmtprint(fmt, "[cur=%#p from=%08zux to=%08zux off=%08zux tot=%08zux]",
		d->norris, d->from, d->to, d->off, d->totalsz);
}

int
χfmt(Fmt *fmt)
{
	Chunk *c;

	c = va_arg(fmt->args, Chunk*);
	if(c == nil)
		return fmtstrcpy(fmt, "[]");
	return fmtprint(fmt, "0x%08p N=%08zux →L=0x%08p ←R=0x%08p", c, c->len, c->left, c->right);
}

static usize
clength(Chunk *r)
{
	usize len;
	Chunk *c;

	c = r;
	len = 0;
	do{
		len += c->len;
		c = c->right;
	}while(c != r);
	return len;
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
	if(c != nil)
		printchunks(c);
}

static Chunk *
newchunk(Buf *b)
{
	Chunk *c;

	c = emalloc(sizeof *c);
	c->left = c;
	c->right = c;
	c->b = b;
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
	nc->boff = c->boff;
	nc->len = c->len;
	incref(c->b);
	return nc;
}
static Chunk *
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
static Chunk *
splitchunk(Chunk *p, usize from, usize to)
{
	Chunk *c;

	/* 0-size chunks are permitted */
	assert(to <= p->len && to - from <= p->len);
	c = clonechunk(p);
	c->boff += from;
	c->len = to - from;
	c->left = c->right = c;
	return c;
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

usize
chainlen(Chunk *c)
{
	usize n;
	Chunk *cp;

	for(cp=c, n=cp->len, cp=cp->right; cp!=c; cp=cp->right)
		n += cp->len;
	return n;
}

static Dot
newdot(Dot *dp)
{
	usize sz;
	Dot d = {0};
	Chunk *c;

	sz = dp->to - dp->from;
	d.norris = dp->norris;
	d.totalsz = d.norris->len;
	/* paranoia */
	for(c=d.norris->right; c!=d.norris; c=c->right)
		d.totalsz += c->len;
	d.cur = d.from = dp->from < d.totalsz ? dp->from : 0;
	d.to = d.from + sz;
	if(d.to > d.totalsz)
		d.to = d.totalsz;
	d.off = -1;
	return d;
}

Chunk *
p2c(usize p, usize *off, Dot *d)
{
	Chunk *c;

	for(c=d->norris; p>=c->len; c=c->right){
		if(c == d->norris->left){
			assert(p == c->len);
			break;
		}
		p -= c->len;
	}
	if(off != nil)
		*off = p;
	return c;
}

static void
forgetop(Op *op)
{
	freechain(op->l);
}

int
unpop(char *)
{
	Op *op;
	Dot *d;

	if(opend == opbuf || ophead == opend)
		return 0;
	op = ophead++;
	d = op->dot;
	dprint(op->p1, "cmd/unpop dot=%Δ\n\tP [ %χ ][ %χ ]\n\tLR [ %χ ][ %χ ]\n",
		d, op->p1, op->p2, op->l, op->r);
	linkchunk(op->p1->left, op->l);
	unlink(op->p1, op->p2);
	if(d->norris == op->p1)
		d->norris = op->l;
	*d = newdot(d);
	return 1;
}

int
popop(char *)
{
	Op *op;
	Dot *d;

	if(ophead == opbuf)
		return 0;
	op = --ophead;
	d = op->dot;
	dprint(op->l, "cmd/pop dot=%Δ\n\tP [ %χ ][ %χ ]\n\tLR [ %χ ][ %χ ]\n",
		d, op->p1, op->p2, op->l, op->r);
	linkchunk(op->l->left, op->p1);
	unlink(op->l, op->r);
	if(d->norris == op->l)
		d->norris = op->p1;
	*d = newdot(d);
	return 1;
}

static void
pushop(Chunk *p1, Chunk *p2, Chunk *l, Chunk *r, Dot *d)
{
	Op *op;

	if(ophead == opbuf + opbufsz){
		opbuf = erealloc(opbuf,
			(opbufsz + 1024) * sizeof *opbuf,
			opbufsz * sizeof *opbuf);
		ophead = opbuf + opbufsz;
		opend = ophead;
		opbufsz += 1024;
	}
	if(opend > ophead){
		for(op=ophead; op<opend; op++)
			forgetop(op);
		memset(ophead, 0, (opend - ophead) * sizeof *ophead);
	}
	*ophead++ = (Op){p1, p2, l, r, d};
	opend = ophead;
}

/* ..[p1]..[p2].. → ..[p1|l]..[r|p2].. → [l]..[r]  */
void
ccrop(Dot *d)
{
	usize foff, toff;
	Chunk *p1, *p2, *l, *r;

	if(d->to == d->from){
		fprint(2, "empty crop\n");
		return;
	}
	assert(d->from < d->to && d->to <= d->totalsz);
	p1 = p2c(d->from, &foff, d);
	p2 = p2c(d->to, &toff, d);
	if(p1 == p2){
		l = splitchunk(p1, foff, toff);
		r = l;
	}else{
		l = splitchunk(p1, foff, p1->len);
		r = splitchunk(p2, 0, toff);
		if(p1->right != p2)
			linkchunk(p2->left, r);
		else
			linkchunk(l, r);
	}
	dprint(d->norris, "ccrop dot=%Δ\n\tP [ %χ ][ %χ ]\n\tLR [ %χ ][ %χ ]\n", d, p1, p2, l, r);
	linkchunk(p1, l);
	unlink(p2, p1);
	pushop(p1, p2, l, r, d);
	d->norris = l;
	d->from = 0;
	*d = newdot(d);
}

/* [p1]..[p2] → [l|p1]..[p2|r] → [l]..c..[r]  */
int
creplace(Dot *d, Chunk *c)
{
	usize sz, foff, toff;
	Chunk *p1, *p2, *l, *r;

	assert(d->from <= d->totalsz && d->to - d->from > 0);
	sz = clength(c);
	p1 = p2c(d->from, &foff, d);
	p2 = p2c(d->to, &toff, d);
	l = splitchunk(p1, 0, foff);
	r = splitchunk(p2, toff, p2->len);
	linkchunk(l, c);
	linkchunk(c, r);
	dprint(d->norris, "creplace dot=%Δ\n\tP [ %χ ][ %χ ]\n\tLR [ %χ ][ %χ ]\n", d, p1, p2, l, r);
	linkchunk(p1->left, l);
	unlink(p1, p2);
	pushop(p1, p2, l, r, d);
	if(p1 == d->norris)
		d->norris = l;
	d->to = d->from + sz;
	*d = newdot(d);
	return 0;
}

/* ..[p1].. → ..[l|r].. → ..[l|c|r].. */
static int
cinsert(Dot *d, Chunk *c)
{
	usize sz, off;
	Chunk *p1, *l, *r;

	sz = clength(c);
	assert(d->off != -1);
	p1 = p2c(d->off, &off, d);
	l = splitchunk(p1, 0, off);
	r = splitchunk(p1, off, p1->len);
	linkchunk(l, c);
	linkchunk(c, r);
	dprint(d->norris, "cinsert dot=%Δ\n\tP [ %χ ]\n\tLR [ %χ ][ %χ ]\n", d, p1, l, r);
	linkchunk(p1->left, l);
	unlink(p1, p1);
	pushop(p1, p1, l, r, d);
	if(p1 == d->norris)
		d->norris = l;
	d->from = d->off;
	d->to = d->from + sz;
	/* FIXME: get rid of indirection */
	*d = newdot(d);
	return 0;
}

int
cpaste(Dot *d)
{
	Chunk *c;

	if(hold.c == nil){
		werrstr("cpaste: nothing to paste");
		return -1;
	}
	dprint(d->norris, "cpaste dot=%Δ hold=%Δ\n", d, &hold.Dot);
	c = clone(hold.c, hold.c->left);
	return d->off == -1 ? creplace(d, c) : cinsert(d, c);
}

void
chold(Chunk *c, Dot *d)
{
	if(hold.c != nil)
		freechain(hold.c);
	hold.c = c;
	hold.Dot = *d;
}

/* [p1]..[x]..[p2] → [p1|l]..[x]..[r|p2] → hold = [l]..[x]..[r] */
Chunk *
ccopy(Dot *d)
{
	usize foff, toff;
	Chunk *p1, *p2, *l, *r;

	if(hold.c != nil)
		freechain(hold.c);
	p1 = p2c(d->from, &foff, d);
	p2 = p2c(d->to, &toff, d);
	if(p1 == p2){
		l = splitchunk(p1, foff, toff);
		r = nil;
	}else{
		l = splitchunk(p1, foff, p1->len);
		r = splitchunk(p2, 0, toff);
		if(p1->right != p2)
			linkchunk(l, clone(p1->right, p2->left));
		linkchunk(l->left, r);
	}
	dprint(d->norris, "ccopy dot=%Δ\n\tP [ %χ ][ %χ ]\n\tLR [ %χ ][ %χ ]\n",
		d, p1, p2, l, p1 == p2 ? l : r);
	hold.c = l;
	hold.Dot = *d;
	return hold.c;
}

/* [p1]..[x]..[p2] → [l|p1]..[x]..[p2|r] → [l][r] */
void
ccut(Dot *d)
{
	usize off;
	Chunk *p1, *p2, *l, *r;

	if(d->from - d->to == d->totalsz){
		fprint(2, "ccut: not cutting entire buffer\n");
		return;
	}
	assert(d->from < d->to && d->to <= d->totalsz);
	ccopy(d);
	p1 = p2c(d->from, &off, d);
	l = splitchunk(p1, 0, off);
	if(d->from == d->to)
		p2 = p1;
	else
		p2 = p2c(d->to, &off, d);
	r = splitchunk(p2, off, p2->len);
	linkchunk(l, r);
	dprint(d->norris, "ccut dot=%Δ\n\tP [ %χ ][ %χ ]\n\tLR [ %χ ][ %χ ]\n",
		d, p1, p2, l, r);
	linkchunk(p1->left, l);
	unlink(p1, p2);
	pushop(p1, p2, l, r, d);
	if(p1 == d->norris)
		d->norris = l;
	*d = newdot(d);
}

uchar *
getslice(Dot *d, usize want, usize *have)
{
	usize n, off;
	Chunk *c;

	if(d->cur >= d->totalsz){
		werrstr("out of bounds: %zd >= %zd", d->cur, d->totalsz);
		*have = 0;
		return nil;
	}
	c = p2c(d->cur, &off, d);
	n = c->len - off;
	*have = want > n ? n : want;
	return c->b->buf + c->boff + off;
}

Chunk *
loadfile(int fd, Dot *d)
{
	int n;
	Chunk *c;
	Buf *b;

	c = newbuf(Chunksz);
	b = c->b;
	for(;;){
		if(b->bufsz - c->len < Chunksz){
			b->buf = erealloc(b->buf, 2 * b->bufsz, b->bufsz);
			b->bufsz *= 2;
		}
		if((n = readn(fd, b->buf + c->len, Chunksz)) < Chunksz)
			break;
		c->len += n;
		yield();
	}
	if(n < 0){
		fprint(2, "loadfile: %r\n");
		freechunk(c);
		return nil;
	}
	c->len += n;
	if(c->len == 0){
		fprint(2, "loadfile: nothing read\n");
		freechunk(c);
		return nil;
	}else if(c->len < b->bufsz){
		b->buf = erealloc(b->buf, c->len, b->bufsz);
		b->bufsz = c->len;
	}
	d->norris = c;
	d->from = 0;
	d->to = c->len;
	*d = newdot(d);
	return c;
}

void
appendfile(char *path)
{
	int fd;
	Chunk *c;
	Dot d;

	if((fd = path != nil ? open(path, OREAD) : 0) < 0)
		sysfatal("open: %r");
	if((c = loadfile(fd, &d)) == nil)
		sysfatal("initcmd: %r");
	close(fd);
	if(dot.totalsz == 0){
		dot = d;
		return;
	}
	linkchunk(dot.norris->left, c);
	dot.totalsz += d.totalsz;
	dot.to = dot.totalsz;
}
