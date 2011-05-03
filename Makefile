
all: abootimg

abootimg: abootimg.c bootimg.h
	echo '#define VERSION_STR "$(shell git describe --tags --abbrev=0)"' > version.h
	gcc -g -O2 -Wno-unused-result -o abootimg abootimg.c -DHAS_BLKID -lblkid

clean:
	rm -f abootimg version.h

archive: clean
	cd ..; tar cvzf abootimg.tar.gz abootimg --exclude tests --exclude tmp\* --exclude .git


