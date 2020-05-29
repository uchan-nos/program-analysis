TARGET = mt

.PHONY: all
all: $(TARGET)

$(TARGET): $(TARGET).o
	$(CXX) -o $@ $^ -lpthread -no-pie

.PHONY: clean
clean:
	rm -f *.o $(TARGET)

.PHONY: run
run: $(TARGET)
	$(PIN_ROOT)/pin -t ../obj-intel64/VectorClock.so -- ./$(TARGET)
