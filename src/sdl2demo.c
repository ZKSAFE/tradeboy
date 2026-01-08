#include <SDL2/SDL.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *)malloc((size_t)log_len + 1);
        if (log) {
            glGetShaderInfoLog(shader, log_len, NULL, log);
            log[log_len] = '\0';
            fprintf(stderr, "shader compile failed: %s\n", log);
            free(log);
        } else {
            fprintf(stderr, "shader compile failed (no mem for log)\n");
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        char *log = (char *)malloc((size_t)log_len + 1);
        if (log) {
            glGetProgramInfoLog(program, log_len, NULL, log);
            log[log_len] = '\0';
            fprintf(stderr, "program link failed: %s\n", log);
            free(log);
        } else {
            fprintf(stderr, "program link failed (no mem for log)\n");
        }
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    fprintf(stdout, "sdl2demo: starting...\n");
    fflush(stdout);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES) != 0) {
        fprintf(stderr, "SDL_GL_SetAttribute PROFILE failed: %s\n", SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
        fprintf(stderr, "SDL_GetCurrentDisplayMode failed: %s\n", SDL_GetError());
        mode.w = 640;
        mode.h = 480;
    }

    fprintf(stdout, "sdl2demo: display0 mode %dx%d@%d\n", mode.w, mode.h, mode.refresh_rate);

    SDL_Window *win = SDL_CreateWindow(
        "sdl2demo",
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        mode.w,
        mode.h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);

    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    if (SDL_GL_MakeCurrent(win, ctx) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
    }

    SDL_GL_SetSwapInterval(1);

    const char *gl_vendor = (const char *)glGetString(GL_VENDOR);
    const char *gl_renderer = (const char *)glGetString(GL_RENDERER);
    const char *gl_version = (const char *)glGetString(GL_VERSION);

    fprintf(stdout, "sdl2demo: GL_VENDOR=%s\n", gl_vendor ? gl_vendor : "(null)");
    fprintf(stdout, "sdl2demo: GL_RENDERER=%s\n", gl_renderer ? gl_renderer : "(null)");
    fprintf(stdout, "sdl2demo: GL_VERSION=%s\n", gl_version ? gl_version : "(null)");

    const char *vs_src =
        "attribute vec2 a_pos;\n"
        "void main(){\n"
        "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
        "}\n";

    const char *fs_src =
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "void main(){\n"
        "  gl_FragColor = u_color;\n"
        "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        fprintf(stderr, "sdl2demo: shader compile failed\n");
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    GLuint prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog) {
        fprintf(stderr, "sdl2demo: program link failed\n");
        SDL_GL_DeleteContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    GLint a_pos = glGetAttribLocation(prog, "a_pos");
    GLint u_color = glGetUniformLocation(prog, "u_color");

    const GLfloat square[] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.5f,  0.5f,
        -0.5f,  0.5f,
    };

    fprintf(stdout, "sdl2demo: running (about 20s)...\n");
    fflush(stdout);

    double start = now_sec();
    double last_fps_t = start;
    int frames = 0;

    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
        }

        double t = now_sec() - start;
        if (t > 20.0) {
            running = 0;
        }

        float phase = (float)fmod(t, 3.0);
        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (phase < 1.0f) {
            r = 1.0f;
        } else if (phase < 2.0f) {
            g = 1.0f;
        } else {
            b = 1.0f;
        }

        glViewport(0, 0, mode.w, mode.h);
        glClearColor(r * 0.15f, g * 0.15f, b * 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform4f(u_color, r, g, b, 1.0f);
        glVertexAttribPointer((GLuint)a_pos, 2, GL_FLOAT, GL_FALSE, 0, square);
        glEnableVertexAttribArray((GLuint)a_pos);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableVertexAttribArray((GLuint)a_pos);

        SDL_GL_SwapWindow(win);

        frames++;
        double now = now_sec();
        if (now - last_fps_t >= 1.0) {
            double fps = (double)frames / (now - last_fps_t);
            fprintf(stdout, "sdl2demo: FPS %.1f\n", fps);
            fflush(stdout);
            frames = 0;
            last_fps_t = now;
        }
    }

    glDeleteProgram(prog);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();

    fprintf(stdout, "sdl2demo: exit\n");
    fflush(stdout);
    return 0;
}
