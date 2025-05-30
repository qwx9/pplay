void	dprint(Chunk*, char*, ...);
void	freechain(Chunk*);
void	killreader(void);
int	unpop(char*);
int	popop(char*);
int	creplace(Dot*, Chunk*);
int	cpaste(Dot*);
Chunk*	ccopy(Dot*);
void	chold(Chunk*, Dot*);
void	ccut(Dot*);
void	ccrop(Dot*);
Chunk*	loadfile(int, Dot*);
int	cmd(char*);
void	appendfile(char*);
void	refresh(int);
void	setcenter(int);
void	setzoom(int, int);
int	zoominto(vlong, vlong);
void	setrange(usize, usize);
int	setloop(vlong);
int	setpan(int);
void	setpage(int);
int	setjump(vlong);
vlong	ss2view(int);
vlong	view2ss(int);
void	reset(int);
void	paint(int);
void	initdrw(int);
void	advance(usize);
Chunk*	p2c(usize, usize*, Dot*);
int	setpos(usize);
uchar*	getslice(Dot*, usize, usize*);
vlong	getbuf(Dot, usize, uchar*, usize);
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
#pragma	varargck	type	"τ"	ssize
