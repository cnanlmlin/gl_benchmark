LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := RenderEngine
LOCAL_SRC_FILES :=  \
	test_glesDownload.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libEGL \
    libGLESv2 \
    libutils \
    libui \
    libandroid \
    libgui

LOCAL_LDFLAGS    := -lm -llog -ljnigraphics
LOCAL_CFLAGS := -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES

include $(BUILD_EXECUTABLE)
