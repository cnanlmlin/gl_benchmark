#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <ui/GraphicBuffer.h>
#include <time.h>
#include <sys/system_properties.h>

#include <utils/String8.h>
#include <gui/SurfaceComposerClient.h>
#include <android/native_window.h>


using namespace android;

#define DEBUG_DOWNLOAD 0
#define DEBUG_SURFACE 1

#define LOG_DEBUG
//#define LOG_TAG    "render"
//#define LOG(...)   __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG  printf

static EGLDisplay mEglDisplay;
static EGLContext mEglContext;
static EGLSurface mEglSurface;
static int mEglWidth = 0;
static int mEglHeight = 0;

static double now_ms(void)
{
	struct timespec res;
	clock_gettime(CLOCK_REALTIME, &res);
	return 1000.0*res.tv_sec + (double)res.tv_nsec/1e6;
}

static void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE) {
    if (returnVal != EGL_TRUE) {
        LOG("%s() returned %d\n", op, returnVal);
    }
    for (EGLint error = eglGetError(); error != EGL_SUCCESS; error = eglGetError()) {
        LOG("after %s() eglError (0x%x)\n", op,  error);
    }
}

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error = glGetError()) {
        LOG("after %s() glError (0x%x)\n", op, error);
    }
}

static void checkFramebufferStatusDetail(const char* name)
{
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == 0) {
      LOG("Checking completeness of Framebuffer:%s", name);
      checkGlError("checkFramebufferStatus (is the target \"GL_FRAMEBUFFER\"?)");
    } else if (status != GL_FRAMEBUFFER_COMPLETE) {
        const char* msg = "not listed";
        switch (status) {
          case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: msg = "attachment"; break;
          case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS: msg = "dimensions"; break;
          case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: msg = "missing attachment"; break;
          case GL_FRAMEBUFFER_UNSUPPORTED: msg = "unsupported"; break;
        }
        LOG("Framebuffer: %s is INCOMPLETE: %s, %x", name, msg, status);
    }
}

static void WriteFile(const char* path, unsigned char* buf, int bufSize)
{
	FILE* fs = NULL;
	if (NULL != path && NULL != buf && bufSize > 0) {
		fs = fopen(path, "w+");
		if (NULL != fs) {
			fwrite(buf, 1, bufSize, fs);
			fflush(fs);
			fclose(fs);
		}
	}
}

static unsigned char* ReadFile(const char* path, int bufSize)
{
	unsigned char* buf = NULL;
	FILE* fs = NULL;
	if (NULL != path && bufSize > 0) {
		fs = fopen(path, "r+");
		if (NULL != fs) {
			buf = (unsigned char*) malloc(bufSize);
			if (NULL != buf) {
				fread(buf, 1, bufSize, fs);
			}
			fclose(fs);
		}
	}
	return buf;
}

static int setupGL() {
    EGLint s_configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT|EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,       8,
        EGL_GREEN_SIZE,     8,
        EGL_BLUE_SIZE,      8,
        EGL_ALPHA_SIZE,     0,
        EGL_DEPTH_SIZE,     EGL_DONT_CARE,
        EGL_STENCIL_SIZE,   EGL_DONT_CARE,
        EGL_SAMPLE_BUFFERS, EGL_DONT_CARE,
        EGL_NONE
    };

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

    EGLint attribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };

    EGLBoolean returnValue;
    EGLint w, h, dummy;
    EGLint numConfigs;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLDisplay display;

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    checkEglError("eglGetDisplay", EGL_TRUE);
    if (EGL_NO_DISPLAY == display) {
    	LOG("eglGetDisplay fail\n");
    	return 0;
    }
    returnValue = eglInitialize(display, 0, 0);
    if (EGL_TRUE != returnValue) {
    	LOG("eglInitialize fail\n");
    	return 0;
    }
    if (!eglGetConfigs(display, NULL, 0, &numConfigs)) {
    	LOG("eglGetConfig fail\n");
    	return 0;
    }
    if (!eglChooseConfig(display, s_configAttribs, &config, 1, &numConfigs)) {
    	LOG("eglChooseConfig fail\n");
    	return 0;
    }
    printf("numConfigs = %d\n", numConfigs);

#if DEBUG_SURFACE
    surface = eglCreatePbufferSurface(display, config, attribs);
    if (surface == EGL_NO_SURFACE) {
        LOG("eglCreatePbufferSurface error %d\n", eglGetError());
        return false;
    }
#else
    EGLint format;
    status_t err;
    int width = 720;//needing to adjust
    int height = 1280;
    err = eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
    sp<SurfaceComposerClient> mComposerClient;
    sp<SurfaceControl> mSurfaceControl;
    mComposerClient = new SurfaceComposerClient;
    mSurfaceControl = mComposerClient->createSurface(String8("Test Surface"), width, height, format, 0);
    SurfaceComposerClient::openGlobalTransaction();
    mSurfaceControl->setLayer(30000);
    //mSurfaceControl->setPosition(100, 100);//options
    //mSurfaceControl->setSize(width, height);//options
    mSurfaceControl->show();
    SurfaceComposerClient::closeGlobalTransaction();
    sp<ANativeWindow> window = mSurfaceControl->getSurface();
    err = ANativeWindow_setBuffersGeometry(window.get(), width, height, format);
    if (err) {
	LOG("ANativeWindow_setBuffersGeometry return error!\n");
	return false;
    }
    surface = eglCreateWindowSurface(display, config, window.get(), NULL);
#endif

    context = eglCreateContext(display, config, NULL, context_attribs);
    checkEglError("eglCreateContext", EGL_TRUE);
    if (EGL_NO_CONTEXT == context) {
    	LOG("eglCreateContext error:%d\n", eglGetError());
    	return 0;
    }
    returnValue = eglMakeCurrent(display, surface, surface, context);
    if (returnValue != EGL_TRUE) {
    	LOG("eglMakeCurrent error:%d\n", eglGetError());
        return 0;
    }
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    mEglDisplay = display;
    mEglSurface = surface;
    mEglContext = context;
    mEglWidth = w;
    mEglHeight = h;
    return 1;
}

static void releaseGL() {
    if (EGL_NO_CONTEXT != mEglContext) {
        eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(mEglDisplay, mEglSurface);
        eglDestroyContext(mEglDisplay, mEglContext);
        eglTerminate(mEglDisplay);

        mEglSurface = EGL_NO_SURFACE;
        mEglContext = EGL_NO_CONTEXT;
        mEglDisplay = EGL_NO_DISPLAY;
    }
}

GLuint loadShader(int shaderType, const char* pSource)
{
	GLuint shader = glCreateShader(shaderType);
	if (shader) {
		glShaderSource(shader, 1, &pSource, NULL);
		glCompileShader(shader);
		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if (!compiled) {
			GLint infoLen = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
			if (infoLen) {
				char* buf = (char*) malloc(infoLen);
				if (NULL != buf) {
					glGetShaderInfoLog(shader, infoLen, NULL, buf);
					LOG("Could not compile shader %d:\n%s\n", shaderType, buf);
					free(buf);
				}
				glDeleteShader(shader);
				shader = 0;
			}
		}
	}
	return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource)
{
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
	if (!vertexShader) {
		return 0;
	}
	GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
	if (!fragmentShader) {
		return 0;
	}
	GLuint program = glCreateProgram();
	if (program) {
		glAttachShader(program, vertexShader);
		checkGlError("glAttachShader");
		glAttachShader(program, fragmentShader);
		checkGlError("glAttachShader");

		glLinkProgram(program);
		GLint linkStatus = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);

		if (linkStatus != GL_TRUE) {
			GLint bufLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
			if (bufLength) {
				char* buf = (char*) malloc(bufLength);
				if (NULL != buf) {
					glGetProgramInfoLog(program, bufLength, NULL, buf);
					LOG("Could not link program:\n%s\n", buf);
					free(buf);
				}
			}
			glDeleteProgram(program);
			program = 0;
		}
	}
	return program;
}

static const char* gVertexShader =   "attribute vec4 a_position;\n"
                                     "attribute vec2 a_texcoord;\n"
                                     "varying vec2 v_texcoord;\n"
                                     "void main() {\n"
                                     "	gl_Position = a_position;\n"
                                     "	v_texcoord = a_texcoord;\n"
                                     "}\n";

static const char* gFragmentShader = "precision mediump float;\n"
                                     "uniform sampler2D texture;\n"
                                     "varying vec2 v_texcoord;\n"
                                     "void main() {\n"
                                     "	gl_FragColor = texture2D(texture, v_texcoord);\n"
                                     "  //gl_FragColor.r = 1.0;\n"
                                     "}\n";

static const float POS_VERTICES[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
static const float TEX_VERTICES[] = {  0.0f,  1.0f, 1.0f,  1.0f,  0.0f, 0.0f, 1.0f, 0.0f };

int main(int argc, char* argv[])
{
    int width = 3264;
    int height = 2448;

    unsigned char* rawBuf = ReadFile("frame1_3264_2448.rgba8888", width * height * 4);
    if (NULL == rawBuf) {
        LOG("rawBuf is null!");
        return 0;
    }  
    unsigned char* outputBuf = (unsigned char*)malloc(width * height * 4);
    if (NULL == outputBuf) {
	    LOG("outputBuf is null!!!");
	    return 0;
    }
    memset(outputBuf, 0, width * height * 4);

    setupGL();

    double t0 = now_ms();
    // setup input texture
    GLuint texture;
    glGenTextures(1, &texture);
    checkGlError("glGenTextures");

    glBindTexture(GL_TEXTURE_2D, texture);
    checkGlError("glBindTexture");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rawBuf);
    checkGlError("glTexImage2D");
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    checkGlError("glTexParameterf");

    // setup output texture
#if DEBUG_DOWNLOAD
#else
    GraphicBuffer *dstBuffer = new GraphicBuffer(width, height, PIXEL_FORMAT_RGBA_8888, GraphicBuffer::USAGE_SW_READ_OFTEN | GraphicBuffer::USAGE_SW_WRITE_OFTEN | GraphicBuffer::USAGE_HW_TEXTURE);
    EGLint eglImgAttrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR dstImage = eglCreateImageKHR(mEglDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)dstBuffer->getNativeBuffer(), eglImgAttrs);
#endif

    GLuint outputTexture;
    glGenTextures(1, &outputTexture);
    checkGlError("glGenTextures");
    glBindTexture(GL_TEXTURE_2D, outputTexture);
    checkGlError("glBindTexture");
#if DEBUG_DOWNLOAD
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	checkGlError("glTexImage2D");
#else
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dstImage);
    checkGlError("glEGLImageTargetTexture2DOES");
#endif
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    checkGlError("glTexParameterf");


    // setup framebuffer
    GLuint framebuffer;
    glGenFramebuffers(1, &framebuffer);
    checkGlError("glGenFramebuffers");
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0);
    checkGlError("glFramebufferTexture2D");
    checkFramebufferStatusDetail("glFramebufferTexture2D");

    GLuint program = createProgram(gVertexShader, gFragmentShader);
    GLint posCoordHandle = glGetAttribLocation(program, "a_position");
    GLint texCoordHandle = glGetAttribLocation(program, "a_texcoord");
    GLint texSamplerHandle = glGetUniformLocation(program, "texture");

    // setup program
    glUseProgram(program);
    checkGlError("glUseProgram");
    glDisable(GL_BLEND);
    checkGlError("glDisable");
    glViewport(0, 0, width, height);

    glVertexAttribPointer(posCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, POS_VERTICES);
    glEnableVertexAttribArray(posCoordHandle);
    checkGlError("pos vertices attribute setup");
    glVertexAttribPointer(texCoordHandle, 2, GL_FLOAT, GL_FALSE, 0, TEX_VERTICES);
    glEnableVertexAttribArray(texCoordHandle);
    checkGlError("texture vertices attribute setup");

    glActiveTexture(GL_TEXTURE0);
    checkGlError("glActiveTexture");
    glBindTexture(GL_TEXTURE_2D, texture);
    checkGlError("glBindTexture");
    glUniform1i(texSamplerHandle, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish(); 

#if DEBUG_DOWNLOAD
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outputBuf);
    double t1 = now_ms();
    LOG("download debug times using glReadPixels: %g ms\n", t1-t0);
#else
    unsigned char* buf = NULL;
    glBindTexture(GL_TEXTURE_2D, outputTexture);
    checkGlError("glBindTexture");
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)dstImage);
    checkGlError("glEGLImageTargetTexture2DOES");

    status_t err = dstBuffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, (void**)(&buf));
    if (err != 0) {
	LOG("dstBuffer->lock() failed: %d\n", err);
	return 0;
    }
    memcpy(outputBuf, buf, width * height * 4);
    err = dstBuffer->unlock();
    if (err != 0) {
	LOG("dstBuffer->unlock() failed: %d\n", err);
	return 0;
    }
    double t1 = now_ms();
    LOG("download debug times using grahicBuffer: %g ms\n", t1-t0);
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGlError("glBindFramebuffer");
    LOG("write download data\n");

    //save to file
    WriteFile("download.rgba8888", outputBuf, width * height * 4);

    releaseGL();

    return 0;
}
