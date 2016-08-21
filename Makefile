include config.mk

SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

ifdef STATIC
	LDFLAGS += -static
endif

RM  = rm -fv
	
ifneq ($(wildcard /usr/include/endian.h),)
	CFLAGS += -DHAVE_ENDIAN_H
endif

all: options $(EXEC)

options: config.mk
	@echo $(EXEC) build options:
	@echo "CFLAGS   = $(CFLAGS)"
	@echo "LDFLAGS  = $(LDFLAGS)"
	@echo "CC       = $(CC)"

$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) *.o

mrproper: clean
	$(RM) $(EXEC)

