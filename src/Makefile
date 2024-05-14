CC = gcc
CFLAGS = -g -Wall -O3 -fPIC -fno-omit-frame-pointer
#CFLAGS = -g3 -Wall -O0 -fPIC
LDFLAGS = -shared -fno-omit-frame-pointer
INCLUDES = -I../linux/usr/include/
LIBS = -lm -lpthread -luring -ldl -lsyscall_intercept

default: libextmem-default.so

all: libextmem-default.so libextmem-pagerank.so


libextmem-default.so: extmem-default.o policies/default.o intercept_layer.o fifo.o storagemanager.o storage_iouring.o observability.o
	$(CC) $(LDFLAGS) -o libextmem-default.so extmem-default.o policies/default.o intercept_layer.o observability.o fifo.o storagemanager.o storage_iouring.o $(LIBS)

libextmem-pagerank.so: extmem-pagerank.o policies/pagerank.o intercept_layer.o fifo.o storagemanager.o storage_iouring.o observability.o
	$(CC) $(LDFLAGS) -o libextmem-pagerank.so extmem-pagerank.o policies/pagerank.o intercept_layer.o observability.o fifo.o storagemanager.o storage_iouring.o $(LIBS)

extmem-default.o: core.c core.h policies/disklinux.h intercept_layer.h observability.h fifo.h storagemanager.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_DISK -D LRU_SWAP -c core.c -o extmem-default.o

extmem-pagerank.o: core.c core.h policies/pageRank.h intercept_layer.h observability.h fifo.h storagemanager.h
	$(CC) $(CFLAGS) $(INCLUDES) -D ALLOC_DISK -D PAGE_RANK -D LRU_SWAP -c core.c -o extmem-pagerank.o

observability.o: observability.c observability.h core.h
	$(CC) $(CFLAGS) $(INCLUDES) -c observability.c

intercept_layer.o: intercept_layer.c intercept_layer.h core.h
	$(CC) $(CFLAGS) $(INCLUDES) -c intercept_layer.c

timer.o: timer.c timer.h
	$(CC) $(CFLAGS) $(INCLUDES) -c timer.c

policies/default.o: policies/disklinux.c policies/disklinux.h observability.h core.h fifo.h storagemanager.h
	$(CC) $(CFLAGS) $(INCLUDES) -D LRU_SWAP -D ALLOC_DISK -c policies/disklinux.c -o policies/default.o

policies/pagerank.o: policies/pageRank.c policies/pageRank.h observability.h core.h fifo.h storagemanager.h
	$(CC) $(CFLAGS) $(INCLUDES) -D LRU_SWAP -D ALLOC_DISK -D PAGE_RANK -c policies/pageRank.c -o policies/pagerank.o

storage_iouring.o: storage_iouring.c storage_iouring.h core.h fifo.h
	$(CC) $(CFLAGS) $(INCLUDES) -c storage_iouring.c

storagemanager.o: storagemanager.c storagemanager.h core.h
	$(CC) $(CFLAGS) $(INCLUDES) -c storagemanager.c

fifo.o: fifo.c fifo.h core.h
	$(CC) $(CFLAGS) $(INCLUDES) -c fifo.c

clean:
	$(RM) *.o *.so policies/*.o
