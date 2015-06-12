outdir:=bin/
sourcedir:=src/

all:
	gcc -std=c99 -pthread ${sourcedir}client.c -Wall -Wextra -o ${outdir}client
	gcc -std=c99 ${sourcedir}server.c ${sourcedir}sockaddr_cmp.c -Wall -Wextra -o ${outdir}server

clean:
	rm -f ${outdir}client ${outdir}server
