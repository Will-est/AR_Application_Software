/*
---------------------------------------------------------------------
--- Author         : Ahmet Özlü
--- Mail           : ahmetozlu93@gmail.com
--- Date           : 1st August 2017
--- Version        : 1.0
--- OpenCV Version : 2.4.10
--- Demo Video     : https://www.youtube.com/watch?v=nPfR5ACrqu0
---------------------------------------------------------------------
*/

#include "ARDrawingContext.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>

#include <opencv2/opencv.hpp>

// You are on OpenGL ES 3.1, so use GLES3 headers (no fixed-function pipeline)
#include <GLES3/gl3.h>

// Keep your loaders (OBJ load is fine; drawing it needs a GLES port later)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
using namespace glm;

#include "objloader.hpp"
#include "texture.hpp"   // still included, but we will NOT call loadBMP_custom

// ---------- Debug helpers ----------
static void PrintGLInfo()
{
    const GLubyte* ver  = glGetString(GL_VERSION);
    const GLubyte* ren  = glGetString(GL_RENDERER);
    const GLubyte* ven  = glGetString(GL_VENDOR);
    const GLubyte* glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("GL_VERSION:   %s\n", ver  ? (const char*)ver  : "(null)");
    printf("GL_RENDERER:  %s\n", ren  ? (const char*)ren  : "(null)");
    printf("GL_VENDOR:    %s\n", ven  ? (const char*)ven  : "(null)");
    printf("GLSL_VERSION: %s\n", glsl ? (const char*)glsl : "(null)");
    fflush(stdout);
}

static void CheckGLError(const char* where)
{
    GLenum e = glGetError();
    if (e != GL_NO_ERROR)
    {
        printf("GL error at %s: %u\n", where, (unsigned)e);
        fflush(stdout);
    }
}

// ---------- Minimal GLES shader pipeline for background ----------
static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        printf("Shader compile error: %.*s\n", (int)n, log);
        fflush(stdout);
    }
    return s;
}

static GLuint LinkProgram(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof(log), &n, log);
        printf("Program link error: %.*s\n", (int)n, log);
        fflush(stdout);
    }
    return p;
}

static GLuint gProg = 0;
static GLuint gVAO  = 0;
static GLuint gVBO  = 0;
static GLuint gCamTex = 0;
static GLint  gTexLoc = -1;

// Fullscreen quad in NDC with UVs
static const float kQuad[] = {
    //   x     y     u     v
    -1.0f, -1.0f,  0.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 0.0f
};

static const char* kVS = R"GLSL(
#version 300 es
precision mediump float;
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* kFS = R"GLSL(
#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV);
}
)GLSL";

static void InitBackgroundQuadOnce() //programmable replacement for draw camera frame
{
    if (gProg) return;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFS);
    gProg = LinkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &gVAO);
    glBindVertexArray(gVAO);

    glGenBuffers(1, &gVBO);
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1); // uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    glGenTextures(1, &gCamTex);
    glBindTexture(GL_TEXTURE_2D, gCamTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glUseProgram(gProg);
    gTexLoc = glGetUniformLocation(gProg, "uTex");
    glUniform1i(gTexLoc, 0); // texture unit 0
    glUseProgram(0);

    CheckGLError("InitBackgroundQuadOnce");
}

static void UploadCameraFrameRGB(const cv::Mat& bgr)
{
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous()) rgb = rgb.clone();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCamTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    static int texW = 0, texH = 0;
    if (texW != rgb.cols || texH != rgb.rows)
    {
        texW = rgb.cols;
        texH = rgb.rows;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texW, texH, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH,
                        GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
    }
}

static void DrawBackgroundQuad(int w, int h)
{
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(gProg);
    glBindVertexArray(gVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCamTex);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    glUseProgram(0);
}

// ---------- Your original globals (kept) ----------
std::vector<glm::vec3> vertices;
std::vector<glm::vec2> uvs;
std::vector<glm::vec3> normals;
bool res = false;

// ---------- OpenCV draw callback ----------
void ARDrawingContextDrawCallback(void* param)
{
    ARDrawingContext * ctx = static_cast<ARDrawingContext*>(param);
    if (ctx) ctx->draw();
}

// ---------- ARDrawingContext ----------
ARDrawingContext::ARDrawingContext(std::string windowName, cv::Size frameSize, const CameraCalibration& c)
    : m_isTextureInitialized(false)
    , m_calibration(c)
    , m_windowName(windowName)
{
    cv::namedWindow(windowName, cv::WINDOW_OPENGL);
    cv::resizeWindow(windowName, frameSize.width, frameSize.height);

    cv::setOpenGlContext(windowName);
    cv::setOpenGlDrawCallback(windowName, ARDrawingContextDrawCallback, this);

    PrintGLInfo();

    InitBackgroundQuadOnce();

    // IMPORTANT: DO NOT call loadBMP_custom here (it segfaults on your system)
    // Texture = loadBMP_custom("...");

    // OBJ load is fine; drawing it needs GLES shader/VBO rewrite (next step)
    res = loadOBJ("/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/testcube.obj",
                  vertices, uvs, normals);

    // You can keep scaling vertices if you want, but it won't be drawn yet.
    // scale3DModel(0.1f);
}

ARDrawingContext::~ARDrawingContext()
{
    cv::setOpenGlDrawCallback(m_windowName, 0, 0);
}

void ARDrawingContext::updateBackground(const cv::Mat& frame)
{
    frame.copyTo(m_backgroundImage);
}

void ARDrawingContext::updateWindow()
{
    cv::updateWindow(m_windowName);
}

void ARDrawingContext::draw()
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_backgroundImage.empty())
    {
        UploadCameraFrameRGB(m_backgroundImage);
        DrawBackgroundQuad(m_backgroundImage.cols, m_backgroundImage.rows);
    }

    // DO NOT call fixed-function overlay (will GL_INVALID_OPERATION on GLES)
    // drawAugmentedScene();

    CheckGLError("end of draw()");
}
