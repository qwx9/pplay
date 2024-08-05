typedef struct Chunk Chunk;
typedef struct Dot Dot;
typedef struct Buf Buf;
typedef struct Track Track;
typedef struct Punkt Punkt;
typedef struct Rekt Rekt;

typedef intptr ssize;

struct Punkt{
	int x;
	int y;
};
struct Rekt{
	Punkt min;
	Punkt max;
};

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
// FIXME: some stuff goes in track
struct Dot{
	ssize trk;
	usize from;
	usize to;
	usize cur;
	ssize off;
	usize totalsz;
	Chunk *norris;
};
struct Track{
	Dot;
	Rekt;
	int working;
	// FIXME: both for samples:
	vlong len;
	s16int *graph[2];
};
extern Dot *current;
extern usize ntracks;
extern Track *tracks;

extern QLock lsync;

enum{
	Drawcur,
	Drawrender,
	Drawall,
};

extern int stereo;
extern int debug, paused;
extern int debugdraw;

#define MIN(x,y)	((x) < (y) ? (x) : (y))
#define MAX(x,y)	((x) > (y) ? (x) : (y))
