# Nome do executável
TARGET = vision_app

# Compiladores
CXX = g++
CC = gcc

# Flags (O pkg-config é essencial aqui)
CXXFLAGS = -Wall -O2 `pkg-config --cflags opencv4`
CFLAGS = -Wall -O2
LDFLAGS = `pkg-config --libs opencv4` -lstdc++

# Aqui estão os teus ficheiros, todos .c como o professor deu
OBJS = main.o executaOpenCV.o vc.o

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

# O TRUQUE: Usamos o $(CXX) que é o g++ para compilar o executaOpenCV.c
# A flag "-x c++" diz ao compilador: "Lê isto como C++ mesmo sendo .c"
executaOpenCV.o: executaOpenCV.c
	$(CXX) $(CXXFLAGS) -x c++ -c executaOpenCV.c -o executaOpenCV.o

# O main e o vc podem ser compilados como C normal
main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

vc.o: vc.c
	$(CC) $(CFLAGS) -c vc.c -o vc.o

clean:
	rm -f *.o $(TARGET)

run: $(TARGET)
	./$(TARGET)