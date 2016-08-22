include config.mk

all:
	$(MAKE) -C src all
	cp src/$(EXEC) .

clean:
	$(MAKE) -C src clean

mrproper:
	$(MAKE) -C src mrproper
	rm -rfv $(EXEC)
