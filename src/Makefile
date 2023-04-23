CXX = clang++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98 -fsanitize=address,undefined
LDFLAGS = -fsanitize=address,undefined

OBJS = main.o

TARGET = main.exe

all: $(TARGET)

clean:
	rm -f $(OBJS)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^
