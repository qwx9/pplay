int	cmd(char*);
void	initcmd(void);
void	update(void);
void	setzoom(int, int);
int	zoominto(vlong, vlong);
void	setpan(int);
void	setloop(vlong);
void	setofs(usize);
void	setjump(usize);
void	redraw(int);
void	initdrw(void);
int	advance(Dot*, usize);
int	jump(usize);
Chunk*	p2c(usize, usize*);
void	setrange(usize, usize);
int	setpos(usize);
uchar*	getslice(Dot*, usize, usize*);
vlong	getbuf(Dot, usize, uchar*, usize);
int	loadin(int);
void*	emalloc(usize);
void*	erealloc(void*, usize, usize);
char*	estrdup(char*);
int	setpri(int);
