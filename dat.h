typedef struct Chunk Chunk;
typedef struct Dot Dot;
typedef struct Buf Buf;
typedef struct Track Track;
typedef struct Seg Seg;

enum{
	Rate = 44100,
	WriteRate = 25,
	WriteDelay = Rate / WriteRate,	/* 1764 default delay */
	Sampsz = 2 * 2,
	Outsz = WriteDelay * Sampsz,
	Chunksz = Sampsz * Rate,
};
#pragma incomplete Buf
struct Chunk{
	Buf *b;
	usize boff;
	usize len;
	Chunk *left;
	Chunk *right;
};
struct Track{
	vlong len;
	s16int *graph[2];
};
struct Seg{
	Track *t;
	usize from;
	usize to;
};
struct Dot{
	Seg;
	usize cur;
	usize off;
	usize totalsz;
	Chunk *norris;
};
extern Dot *dots, *current;
extern usize ndots, ntracks;
extern Track *tracks;

extern QLock lsync;

extern int stereo;
extern int debug, paused;
extern int debugdraw;

#define MIN(x,y)	((x) < (y) ? (x) : (y))
#define MAX(x,y)	((x) > (y) ? (x) : (y))
