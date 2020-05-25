TARGET = of

.PHONY: all
all: $(TARGET)

$(TARGET): $(TARGET).o
	$(CC) -o $@ $^

.PHONY: clean
clean:
	rm -f *.o $(TARGET)

.PHONY: run
run: $(TARGET)
	$(PIN_ROOT)/pin -t ../obj-intel64/Overflow.so -- ./$(TARGET)
