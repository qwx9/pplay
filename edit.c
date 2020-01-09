#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

void
writepcm(char *path)
{
	int n, fd, sz;
	uchar *p;

	if((fd = create(path, OWRITE, 0664)) < 0){
		fprint(2, "create: %r\n");
		return;
	}
	if((sz = iounit(fd)) == 0)
		sz = 8192;
	for(p=loops; p<loope; p+=sz){
		n = loope - p < sz ? loope - p : sz;
		if(write(fd, p, n) != n){
			fprint(2, "write: %r\n");
			break;
		}
	}
	close(fd);
}
