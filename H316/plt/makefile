SIMH_H316 = ../../BIN/h316
PS_DISPLAY=evince

all: examples

examples: tree.ps
	$(PS_DISPLAY) $^

h16-plt2ps: h16-plt2ps.cpp

tree.ps: tree.plt tree_script h16-plt2ps
	./h16-plt2ps -o $@ $<

tree.plt: tree.slst
	$(SIMH_H316) tree_script

clean:
	rm -f h16-plt2ps tree.ps tree.plt
	rm -f *~

.PHONY: all clean examples
