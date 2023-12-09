test:
	mkdir -p bin
	mkdir -p log
	./grade.sh

clean:
	rm -f *.o
	rm -f *.out
	rm -f *.err
	rm -f *.log
	rm -f img
	rm -f test[0-9]