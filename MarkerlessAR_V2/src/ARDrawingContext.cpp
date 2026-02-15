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
// Prints runtime GL implementation details to verify that an ES context is active.
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
// Compiles a single shader stage (vertex or fragment) and logs errors.
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

// Static GL resources for the camera background pass.
static GLuint gProg = 0;
static GLuint gVAO  = 0;
static GLuint gVBO  = 0;
static GLuint gCamTex = 0;
static GLint  gTexLoc = -1;

// Fullscreen quad in NDC with UVs for drawing camera frames.
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
    // One-time GL resource initialization.
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
    // GLES texture upload expects RGB here, so convert from OpenCV's BGR layout.
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous()) rgb = rgb.clone();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gCamTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Reallocate texture storage only when frame dimensions change.
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
    // Render camera image into a camera-sized viewport.
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

static void CompositeOverlayOnFrame(const cv::Mat& backgroundBgr,
                                    const cv::Mat& overlayBgra,
                                    cv::Mat& outputBgr)
{
    // Fallback overlay mode: pin the overlay image in the top-left corner.
    backgroundBgr.copyTo(outputBgr);
    if (overlayBgra.empty() || overlayBgra.channels() != 4 || outputBgr.empty())
        return;

    const int margin = 12;
    // Constrain overlay to roughly 1/4 of the background size.
    const int maxOverlayW = std::max(1, outputBgr.cols / 4);
    const int maxOverlayH = std::max(1, outputBgr.rows / 4);

    const float sx = static_cast<float>(maxOverlayW) / static_cast<float>(overlayBgra.cols);
    const float sy = static_cast<float>(maxOverlayH) / static_cast<float>(overlayBgra.rows);
    const float scale = std::min(sx, sy);

    const int drawW = std::max(1, static_cast<int>(overlayBgra.cols * scale));
    const int drawH = std::max(1, static_cast<int>(overlayBgra.rows * scale));

    cv::Mat resizedOverlay;
    cv::resize(overlayBgra, resizedOverlay, cv::Size(drawW, drawH), 0, 0, cv::INTER_AREA);

    const int x = std::min(margin, std::max(0, outputBgr.cols - drawW));
    const int y = std::min(margin, std::max(0, outputBgr.rows - drawH));

    cv::Mat roi = outputBgr(cv::Rect(x, y, drawW, drawH));
    for (int row = 0; row < drawH; ++row)
    {
        const cv::Vec4b* srcPtr = resizedOverlay.ptr<cv::Vec4b>(row);
        cv::Vec3b* dstPtr = roi.ptr<cv::Vec3b>(row);
        for (int col = 0; col < drawW; ++col)
        {
            // Standard "source-over" alpha blend in BGR space.
            const float alpha = srcPtr[col][3] / 255.0f;
            const float invAlpha = 1.0f - alpha;
            dstPtr[col][0] = static_cast<uchar>(srcPtr[col][0] * alpha + dstPtr[col][0] * invAlpha);
            dstPtr[col][1] = static_cast<uchar>(srcPtr[col][1] * alpha + dstPtr[col][1] * invAlpha);
            dstPtr[col][2] = static_cast<uchar>(srcPtr[col][2] * alpha + dstPtr[col][2] * invAlpha);
        }
    }
}

static void CompositeOverlayOnPattern(const cv::Mat& backgroundBgr,
                                      const cv::Mat& overlayBgra,
                                      const std::vector<cv::Point2f>& patternQuad,
                                      cv::Mat& outputBgr)
{
    // Pattern-locked mode: warp overlay image onto the detected pattern quadrilateral.
    backgroundBgr.copyTo(outputBgr);
    if (overlayBgra.empty() || overlayBgra.channels() != 4 || outputBgr.empty())
        return;
    if (patternQuad.size() != 4)
        return;

    std::vector<cv::Point2f> srcQuad(4);
    srcQuad[0] = cv::Point2f(0.0f, 0.0f);
    srcQuad[1] = cv::Point2f(static_cast<float>(overlayBgra.cols - 1), 0.0f);
    srcQuad[2] = cv::Point2f(static_cast<float>(overlayBgra.cols - 1), static_cast<float>(overlayBgra.rows - 1));
    srcQuad[3] = cv::Point2f(0.0f, static_cast<float>(overlayBgra.rows - 1));

    // Find homography from overlay image space -> frame space.
    cv::Mat H = cv::findHomography(srcQuad, patternQuad, 0);
    if (H.empty())
        return;

    // Warp overlay into full frame with transparent background.
    cv::Mat warpedBgra(backgroundBgr.rows, backgroundBgr.cols, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    cv::warpPerspective(
        overlayBgra,
        warpedBgra,
        H,
        backgroundBgr.size(),
        cv::INTER_LINEAR,
        cv::BORDER_CONSTANT,
        cv::Scalar(0, 0, 0, 0));

    // Separate alpha channel and compose warped BGR over background.
    std::vector<cv::Mat> channels;
    cv::split(warpedBgra, channels);
    cv::Mat warpedBgr;
    cv::merge(std::vector<cv::Mat>{channels[0], channels[1], channels[2]}, warpedBgr);

    cv::Mat bg32f;
    cv::Mat fg32f;
    backgroundBgr.convertTo(bg32f, CV_32FC3, 1.0 / 255.0);
    warpedBgr.convertTo(fg32f, CV_32FC3, 1.0 / 255.0);

    cv::Mat alpha32f;
    channels[3].convertTo(alpha32f, CV_32FC1, 1.0 / 255.0);
    cv::Mat alpha3;
    cv::merge(std::vector<cv::Mat>{alpha32f, alpha32f, alpha32f}, alpha3);

    cv::Mat invAlpha3 = cv::Scalar(1.0, 1.0, 1.0) - alpha3;
    cv::Mat out32f = fg32f.mul(alpha3) + bg32f.mul(invAlpha3);
    out32f.convertTo(outputBgr, CV_8UC3, 255.0);
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
    , m_backgroundTextureId(0)
    , m_calibration(c)
    , m_overlayEnabled(false)
    , m_overlayPatternPresent(false)
    , m_windowName(windowName)
{
    // Create a dedicated OpenGL-backed window and bind this draw callback.
    cv::namedWindow(windowName, cv::WINDOW_OPENGL);
    cv::resizeWindow(windowName, frameSize.width, frameSize.height);

    cv::setOpenGlContext(windowName);
    cv::setOpenGlDrawCallback(windowName, ARDrawingContextDrawCallback, this);

    PrintGLInfo();

    // Initialize shader pipeline used for rendering the camera frame.
    InitBackgroundQuadOnce();

    // IMPORTANT: DO NOT call loadBMP_custom here (it segfaults on your system)
    // Texture = loadBMP_custom("...");

    // OBJ load remains for compatibility with older code paths, but is not rendered.
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
    // Keep the newest frame; actual upload to GL happens during draw().
    frame.copyTo(m_backgroundImage);
}

void ARDrawingContext::setOverlayImage(const cv::Mat& overlayImage)
{
    // Normalize overlay storage to BGRA so alpha compositing has a stable format.
    m_overlayImage.release();
    if (overlayImage.empty())
        return;

    if (overlayImage.channels() == 4)
    {
        overlayImage.copyTo(m_overlayImage);
        return;
    }

    if (overlayImage.channels() == 3)
    {
        // BGR input has no alpha; create opaque alpha channel.
        cv::cvtColor(overlayImage, m_overlayImage, cv::COLOR_BGR2BGRA);
        return;
    }

    if (overlayImage.channels() == 1)
    {
        cv::Mat tmpBgr;
        cv::cvtColor(overlayImage, tmpBgr, cv::COLOR_GRAY2BGR);
        cv::cvtColor(tmpBgr, m_overlayImage, cv::COLOR_BGR2BGRA);
    }
}

void ARDrawingContext::setOverlayEnabled(bool enabled)
{
    // Global runtime switch for all overlay modes.
    m_overlayEnabled = enabled;
}

void ARDrawingContext::setPatternOverlayState(bool patternPresent, const std::vector<cv::Point2f>& patternQuad)
{
    // Store per-frame detection output for the render callback.
    m_overlayPatternPresent = patternPresent;
    if (patternPresent && patternQuad.size() == 4)
        m_overlayPatternQuad = patternQuad;
    else
        m_overlayPatternQuad.clear();
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

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_backgroundImage.empty())
    {
        // Compose overlay on CPU, then upload final frame to a GL texture for display.
        if (m_overlayEnabled && !m_overlayImage.empty() && m_backgroundImage.channels() == 3)
        {
            cv::Mat compositedFrame;
            // Prefer pattern-locked warp when pattern corners are available.
            if (m_overlayPatternPresent && m_overlayPatternQuad.size() == 4)
                CompositeOverlayOnPattern(m_backgroundImage, m_overlayImage, m_overlayPatternQuad, compositedFrame);
            else
                // Fallback to fixed corner overlay when pattern is not visible.
                CompositeOverlayOnFrame(m_backgroundImage, m_overlayImage, compositedFrame);
            UploadCameraFrameRGB(compositedFrame);
        }
        else
        {
            UploadCameraFrameRGB(m_backgroundImage);
        }

        DrawBackgroundQuad(m_backgroundImage.cols, m_backgroundImage.rows);
    }

    // DO NOT call fixed-function overlay (will GL_INVALID_OPERATION on GLES)
    // drawAugmentedScene();

    CheckGLError("end of draw()");
}
