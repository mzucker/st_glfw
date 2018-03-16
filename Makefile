all: st_glfw

%: %.c
	gcc -Wall -g -O3 -o $@ $< `pkg-config glew glfw3 --cflags --libs` -framework OpenGL -lpng

clean:
	rm -rf *.o st_glfw

rebuild: clean all

.PHONY : clean
