# Jesse Joseph
# jjoseph@hmc.edu
# 040161840
#
GXX=gcc -std=c11
FLAGS=-Wall -Wextra -Wpedantic
LIBS=-lmraa -lpthread -lm

lab4b: lab4b.c
	$(GXX) lab4b.c -o lab4b $(LIBS) $(FLAGS)

clean:
	-rm lab4b
	-rm logfile

dist:
	tar -czvf lab4b-040161840.tar.gz lab4b.c Makefile README

test:
	make lab4b
	./lab4b --log=logfile
