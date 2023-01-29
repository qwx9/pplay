void	fixroot(Chunk*, usize);
void	dprint(Chunk*, char*, ...);
void	freechain(Chunk*);
usize	chunklen(Chunk*);
void	recalcsize(void);
void	paranoia(int);
void	setdot(Dot*, Chunk*);
Chunk*	clone(Chunk*, Chunk*);
int	splitrange(usize, usize, Chunk**, Chunk**);
void	graphfrom(Chunk*);
Chunk*	inserton(usize, usize, Chunk*, Chunk**);
Chunk*	insertat(usize, Chunk*);
Chunk*	croprange(usize, usize, Chunk**);
Chunk*	cutrange(usize, usize, Chunk**);
Chunk*	readintochunks(int);
int	cmd(char*);
void	initcmd(void);
void	update(void);
void	setzoom(int, int);
int	zoominto(vlong, vlong);
void	setpan(int);
void	setpage(int);
void	setloop(vlong);
void	setofs(usize);
void	setjump(usize);
void	redraw(int);
void	initdrw(int);
int	advance(Dot*, usize);
int	jump(usize);
Chunk*	p2c(usize, usize*);
usize	c2p(Chunk*);
void	setrange(usize, usize);
int	setpos(usize);
uchar*	getslice(Dot*, usize, usize*);
vlong	getbuf(Dot, usize, uchar*, usize);
int	loadin(int);
int	χfmt(Fmt*);
int	Δfmt(Fmt*);
int	τfmt(Fmt*);
void*	emalloc(usize);
void*	erealloc(void*, usize, usize);
char*	estrdup(char*);
int	setpri(int);

#pragma	varargck	argpos	dprint	2
#pragma	varargck	type	"χ"	Chunk*
#pragma	varargck	type	"Δ"	Dot*
#pragma	varargck	type	"τ"	usize
