CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2
# Falls back to the system javac/java if the Homebrew OpenJDK is absent
JAVA_HOME_GUESS := $(firstword $(wildcard /opt/homebrew/opt/openjdk /usr/local/opt/openjdk))
JAVAC   := $(if $(JAVA_HOME_GUESS),$(JAVA_HOME_GUESS)/bin/javac,javac)
JAVA    := $(if $(JAVA_HOME_GUESS),$(JAVA_HOME_GUESS)/bin/java,java)
JAR     := $(if $(JAVA_HOME_GUESS),$(JAVA_HOME_GUESS)/bin/jar,jar)

SRC := src/main.c src/classfile.c src/interp.c src/jar.c
OBJ := $(SRC:.c=.o)
BIN := minijvm

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

src/%.o: src/%.c src/classfile.h src/interp.h src/jar.h
	$(CC) $(CFLAGS) -c -o $@ $<

examples/Test.class: examples/Test.java
	$(JAVAC) $<

examples/HelloWorld.class: examples/HelloWorld.java
	$(JAVAC) $<

# Deflated, the way the jar tool packs by default
examples/Test.jar: examples/Test.class
	cd examples && $(JAR) cfe Test.jar Test Test.class

# Stored (the 0 flag), to exercise the uncompressed path too
examples/Test-stored.jar: examples/Test.class
	cd examples && $(JAR) cfe0 Test-stored.jar Test Test.class

test: $(BIN) examples/Test.class examples/HelloWorld.class examples/Test.jar
	./$(BIN) examples/Test.class
	./$(BIN) examples/HelloWorld.class
	./$(BIN) -jar examples/Test.jar

# Compare our output against a real JVM, as a class and inside a jar
verify: $(BIN) examples/Test.class examples/Test.jar examples/Test-stored.jar
	$(JAVA) -cp examples Test > /tmp/realjvm.out
	./$(BIN) examples/Test.class > /tmp/minijvm.out
	diff /tmp/minijvm.out /tmp/realjvm.out && echo "OK: class output matches real JVM"
	./$(BIN) -jar examples/Test.jar > /tmp/minijvm-jar.out
	diff /tmp/minijvm-jar.out /tmp/realjvm.out && echo "OK: deflated jar matches real JVM"
	./$(BIN) -jar examples/Test-stored.jar > /tmp/minijvm-stored.out
	diff /tmp/minijvm-stored.out /tmp/realjvm.out && echo "OK: stored jar matches real JVM"

clean:
	rm -f $(BIN) $(OBJ) examples/*.class examples/*.jar

.PHONY: all test verify clean
