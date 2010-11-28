
all: abootimg

abootimg: abootimg.c bootimg.h
	gcc -g -O2 -Wno-unused-result -o abootimg abootimg.c -DHAS_BLKID -lblkid

clean:
	rm -f abootimg abootimg-static

archive: clean
	cd ..; tar cvzf abootimg.tar.gz abootimg --exclude tests --exclude tmp\* --exclude .git


