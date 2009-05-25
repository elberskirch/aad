TARGET=aad

all:
	gcc -o $(TARGET) $(TARGET).c -levent

clean:
	rm $(TARGET) temperature.log
