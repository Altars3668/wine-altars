/* mesasharerace - a context that is still being created is already on its share group's list.
 *
 * Mesa's _mesa_initialize_context() puts a new context on ctx->Shared->Contexts before
 * st_create_context_priv() has given it ctx->st.  A thread that releases a buffer with the gallium
 * threaded context on walks that list under Shared->Mutex and reads shared_ctx->st->pipe of every
 * context on it.  Here one thread keeps creating and destroying contexts in a share group while
 * another keeps respecifying a buffer with another size, which releases the old storage.  Without
 * the fix the walk soon reads the NULL st and the process dies of SIGSEGV; under Wine the same fault
 * happened inside a unix call, which returned an error and left Shared->Mutex locked.
 *
 * Runs on EGL's surfaceless platform, so on the render node and without a display.
 *
 *   mesasharerace [seconds]       exit 0 when nothing went wrong in that time (default 30)
 *
 * GALLIUM_THREAD=0 takes the threaded context, and the walk, away.
 *
 * Copyright 2026 AltarsCN.  MIT licence.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static EGLDisplay display;
static EGLConfig config;
static EGLContext root;
static atomic_bool stop;
static atomic_ulong created, released;

static EGLContext create_context(EGLContext share)
{
    static const EGLint attribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                     EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};

    /* the bound API is per thread, and GLES in a new one */
    eglBindAPI(EGL_OPENGL_API);
    return eglCreateContext(display, config, share, attribs);
}

/* creates and destroys contexts that share with the root one */
static void *creator(void *arg)
{
    while (!atomic_load(&stop))
    {
        EGLContext context = create_context(root);

        if (context == EGL_NO_CONTEXT)
        {
            fprintf(stderr, "eglCreateContext failed: %#x\n", eglGetError());
            exit(2);
        }
        eglDestroyContext(display, context);
        atomic_fetch_add(&created, 1);
    }
    return NULL;
}

/* respecifies a buffer, so that its old storage is released, in a context of the share group */
static void *releaser(void *arg)
{
    EGLContext context = create_context(root);
    GLuint buffer;
    char data[256];

    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
    {
        fprintf(stderr, "releaser context: %#x\n", eglGetError());
        exit(2);
    }
    memset(data, 0x5a, sizeof(data));
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    while (!atomic_load(&stop))
    {
        unsigned long n = atomic_fetch_add(&released, 1);

        /* the same size and usage would only discard the contents: another size releases the old storage */
        glBufferData(GL_ARRAY_BUFFER, n & 1 ? sizeof(data) : sizeof(data) / 2, data, GL_STREAM_DRAW);
        if (!(n % 4096)) glFlush();
    }
    glDeleteBuffers(1, &buffer);
    glFinish();
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    return NULL;
}

int main(int argc, char **argv)
{
    static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    pthread_t threads[2];
    const char *renderer;
    EGLint count;

    get_platform_display = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    display = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (!eglInitialize(display, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API) ||
        !eglChooseConfig(display, config_attribs, &config, 1, &count) || !count ||
        (root = create_context(EGL_NO_CONTEXT)) == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, root))
    {
        fprintf(stderr, "EGL setup failed: %#x\n", eglGetError());
        return 2;
    }
    renderer = (const char *)glGetString(GL_RENDERER);
    printf("%s, %s\n", renderer, (const char *)glGetString(GL_VERSION));
    fflush(stdout);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    pthread_create(&threads[0], NULL, releaser, NULL);
    pthread_create(&threads[1], NULL, creator, NULL);
    for (int i = 0; i < seconds * 10; i++) nanosleep(&(struct timespec){0, 100000000}, NULL);
    atomic_store(&stop, true);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    printf("%d s: %lu contexts created and destroyed, %lu buffers respecified, no fault\n", seconds,
           atomic_load(&created), atomic_load(&released));
    eglDestroyContext(display, root);
    eglTerminate(display);
    return 0;
}
