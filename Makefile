CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
# Falls back to the system javac/java if the Homebrew OpenJDK is absent
JAVA_HOME_GUESS := $(firstword $(wildcard /opt/homebrew/opt/openjdk /usr/local/opt/openjdk))
JAVAC   := $(if $(JAVA_HOME_GUESS),$(JAVA_HOME_GUESS)/bin/javac,javac)
JAVA    := $(if $(JAVA_HOME_GUESS),$(JAVA_HOME_GUESS)/bin/java,java)

SRC := src/main.c src/classfile.c src/interp.c
OBJ := $(SRC:.c=.o)
BIN := minijvm

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/classfile.h src/interp.h
	$(CC) $(CFLAGS) -c -o $@ $<

examples/Test.class: examples/Test.java
	$(JAVAC) $<

examples/HelloWorld.class: examples/HelloWorld.java
	$(JAVAC) $<

test: $(BIN) examples/Test.class examples/HelloWorld.class
	./$(BIN) examples/Test.class
	./$(BIN) examples/HelloWorld.class

# Compare our output against a real JVM
verify: $(BIN) examples/Test.class
	./$(BIN) examples/Test.class > /tmp/minijvm.out
	$(JAVA) -cp examples Test > /tmp/realjvm.out
	diff /tmp/minijvm.out /tmp/realjvm.out && echo "OK: output matches real JVM"

clean:
	rm -f $(BIN) $(OBJ) examples/*.class

.PHONY: all test verify clean
