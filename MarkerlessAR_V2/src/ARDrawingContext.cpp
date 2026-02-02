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

// File includes:
#include "ARDrawingContext.hpp"

// Standard
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>

// OpenCV
#include <opencv2/opencv.hpp>

// NOTE: You're on OpenGL ES 3.1 at runtime.
// Do NOT use fixed-function pipeline calls.
#include <GLES3/gl3.h>

// Your loaders (kept, though 3D draw is disabled for now)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
using namespace glm;

#include "objloader.hpp"
#include "texture.hpp"

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

std::vector<glm::vec3> vertices;
std::vector<glm::vec2> uvs;
std::vector<glm::vec3> normals;
bool res = false;
GLuint Texture = 0;

// ---- GLES background quad resources ----
static GLuint gProg = 0;
static GLuint gVAO  = 0;
static GLuint gVBO  = 0;
static GLuint gCamTex = 0;
static GLint  gTexLoc = -1;

// Fullscreen quad: pos (x,y) in NDC, uv (u,v)
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

static void InitBackgroundQuadOnce()
{
    if (gProg) return;

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFS);
    gProg = LinkProgram(vs, fs);

    glDeleteShader(vs);
    glDeleteShader(fs);

    // VAO/VBO
    glGenVertexArrays(1, &gVAO);
    glBindVertexArray(gVAO);

    glGenBuffers(1, &gVBO);
    glBindBuffer(GL_ARRAY_BUFFER, gVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // pos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1); // uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    // Texture
    glGenTextures(1, &gCamTex);
    glBindTexture(GL_TEXTURE_2D, gCamTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Uniform location
    glUseProgram(gProg);
    gTexLoc = glGetUniformLocation(gProg, "uTex");
    glUniform1i(gTexLoc, 0); // texture unit 0
    glUseProgram(0);

    CheckGLError("InitBackgroundQuadOnce");
}

static void UploadCameraFrameRGB(const cv::Mat& bgr)
{
    // Convert BGR -> RGB for GLES upload
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous()) rgb = rgb.clone();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCamTex);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    static int texW = 0, texH = 0;
    if (texW != rgb.cols || texH != rgb.rows)
    {
        texW = rgb.cols; texH = rgb.rows;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texW, texH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
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

// ---- OpenCV OpenGL callback ----
void ARDrawingContextDrawCallback(void* param)
{
    ARDrawingContext * ctx = static_cast<ARDrawingContext*>(param);
    if (ctx) ctx->draw();
}

// ---- ARDrawingContext ----
ARDrawingContext::ARDrawingContext(std::string windowName, cv::Size frameSize, const CameraCalibration& c)
    : m_isTextureInitialized(false) // unused now, but keep
    , m_calibration(c)
    , m_windowName(windowName)
{
    // Create window with OpenGL support (OpenCV manages the GLES context)
    cv::namedWindow(windowName, cv::WINDOW_OPENGL);
    cv::resizeWindow(windowName, frameSize.width, frameSize.height);

    cv::setOpenGlContext(windowName);
    cv::setOpenGlDrawCallback(windowName, ARDrawingContextDrawCallback, this);

    // Print GL context info ONCE
    PrintGLInfo();

    // Init shader quad + texture
    InitBackgroundQuadOnce();

    // Keep your model loads (but drawing is disabled until we port it to GLES)
    Texture = loadBMP_custom("/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/testcube.bmp");
    res = loadOBJ("/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/testcube.obj", vertices, uvs, normals);

    // scale3DModel(0.1f);  // ok to keep if you want
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
    static int c = 0;
    if ((c++ % 120) == 0)
    {
        printf("draw() running | bg empty=%d size=%dx%d ch=%d\n",
               m_backgroundImage.empty(),
               m_backgroundImage.cols, m_backgroundImage.rows,
               m_backgroundImage.empty() ? 0 : m_backgroundImage.channels());
        fflush(stdout);
    }

    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_backgroundImage.empty())
    {
        UploadCameraFrameRGB(m_backgroundImage);
        DrawBackgroundQuad(m_backgroundImage.cols, m_backgroundImage.rows);
    }

    // IMPORTANT: disable old fixed-function AR overlay for now (it will GL_INVALID_OPERATION on GLES)
    // drawAugmentedScene();

    CheckGLError("end of draw()");
}
