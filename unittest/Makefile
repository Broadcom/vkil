SRCS = test_vkil.c ../vkil_api.c ../vkil_session.c
OBJS = $(SRCS:.c=.o)
LIBS = -lvkdrv
RM   = rm -f

all: $(OBJS)
	$(CC) -o test_vkil $^ $(LIBS)

run: all
	./test_vkil

clean:
	$(RM) test_vkil
