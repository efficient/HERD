CFLAGS  := -O3 -Wall -Werror -Wno-unused-result
LD      := gcc
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lrt -lpthread

all: common.o conn.o main.o
	${LD} -o $@ $^ ${LDFLAGS}

PHONY: clean
clean:
	rm -f *.o ${APPS}
