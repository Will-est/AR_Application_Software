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

// Include standard headers
#include <stdio.h>
#include <stdlib.h>
#include <vector>

// Include GLEW
#include <GL/glew.h>

// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
using namespace glm;

#include "objloader.hpp"
#include "texture.hpp"

GLuint VertexArrayID;

std::vector<glm::vec3> vertices;
std::vector<glm::vec2> uvs;
std::vector<glm::vec3> normals; // Won't be used at the moment.

bool res;

glm::vec3 a;
glm::vec2 b;

GLuint Texture;

static void PrintGLError(const char* where)
{
	GLenum err = glGetError();
	if (err != GL_NO_ERROR)
	{
		printf("GL error at %s: %u\n", where, (unsigned)err);
		fflush(stdout);
	}
}

void ARDrawingContextDrawCallback(void* param)
{
	ARDrawingContext * ctx = static_cast<ARDrawingContext*>(param);
	if (ctx)
	{
		ctx->draw();
	}
}

ARDrawingContext::ARDrawingContext(std::string windowName, cv::Size frameSize, const CameraCalibration& c)
	: m_isTextureInitialized(false)
	, m_calibration(c)
	, m_windowName(windowName)
{
	// Create window with OpenGL support
	cv::namedWindow(windowName, cv::WINDOW_OPENGL);

	// Resize it exactly to video size
	cv::resizeWindow(windowName, frameSize.width, frameSize.height);

	// Initialize OpenGL draw callback:
	cv::setOpenGlContext(windowName);
	cv::setOpenGlDrawCallback(windowName, ARDrawingContextDrawCallback, this);

	glewExperimental = true; // Needed for core profile
	if (glewInit() != GLEW_OK) {
		fprintf(stderr, "Failed to initialize GLEW\n");
	}

	// Print GL info so we know what context we actually got
	const GLubyte* ver = glGetString(GL_VERSION);
	const GLubyte* ren = glGetString(GL_RENDERER);
	const GLubyte* ven = glGetString(GL_VENDOR);
	printf("GL_VERSION:  %s\n", ver ? (const char*)ver : "(null)");
	printf("GL_RENDERER: %s\n", ren ? (const char*)ren : "(null)");
	printf("GL_VENDOR:   %s\n", ven ? (const char*)ven : "(null)");
	fflush(stdout);

	// Keep culling for your 3D model if you want, but we will DISABLE it for background quad
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	// Load .bmp file as texture
	Texture = loadBMP_custom("/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/testcube.bmp");

	// load(parse) .obj file
	res = loadOBJ("/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/testcube.obj", vertices, uvs, normals);

	// Scale 3D Model
	scale3DModel(0.1f);
}

ARDrawingContext::~ARDrawingContext()
{
	cv::setOpenGlDrawCallback(m_windowName, 0, 0);
}

void ARDrawingContext::updateBackground(const cv::Mat& frame)
{
	// Store latest frame (OpenCV is usually BGR)
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
		printf("draw() running | bg empty=%d size=%dx%d ch=%d continuous=%d\n",
			m_backgroundImage.empty(),
			m_backgroundImage.cols, m_backgroundImage.rows,
			m_backgroundImage.empty() ? 0 : m_backgroundImage.channels(),
			m_backgroundImage.empty() ? 0 : (int)m_backgroundImage.isContinuous());
		fflush(stdout);
	}

	// Clear entire screen
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	drawCameraFrame();    // Render background
	drawAugmentedScene(); // Draw AR overlay

	PrintGLError("end of draw()");
	glFlush();
}

void ARDrawingContext::drawCameraFrame()
{
	if (m_backgroundImage.empty())
		return;

	// Initialize texture for background image
	if (!m_isTextureInitialized)
	{
		glGenTextures(1, &m_backgroundTextureId);
		glBindTexture(GL_TEXTURE_2D, m_backgroundTextureId);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		m_isTextureInitialized = true;
	}

	const int w = m_backgroundImage.cols;
	const int h = m_backgroundImage.rows;

	// Upload pixels TO OpenGL => UNPACK
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glBindTexture(GL_TEXTURE_2D, m_backgroundTextureId);

	// Avoid GL_BGR_EXT on embedded stacks: convert to RGB/RGBA and upload as GL_RGB/GL_RGBA
	if (m_backgroundImage.channels() == 3)
	{
		cv::Mat rgb;
		cv::cvtColor(m_backgroundImage, rgb, cv::COLOR_BGR2RGB);
		if (!rgb.isContinuous()) rgb = rgb.clone();

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
	}
	else if (m_backgroundImage.channels() == 4)
	{
		cv::Mat rgba;
		cv::cvtColor(m_backgroundImage, rgba, cv::COLOR_BGRA2RGBA);
		if (!rgba.isContinuous()) rgba = rgba.clone();

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data);
	}
	else if (m_backgroundImage.channels() == 1)
	{
		cv::Mat gray = m_backgroundImage;
		if (!gray.isContinuous()) gray = gray.clone();

		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, gray.data);
	}

	PrintGLError("after glTexImage2D (background)");
	// If upload failed, don’t try to draw quad
	if (glGetError() != GL_NO_ERROR)
		return;

	// Draw full-screen textured quad using fixed function pipeline
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	const GLfloat bgTextureVertices[] = { 0, 0, (GLfloat)w, 0, 0, (GLfloat)h, (GLfloat)w, (GLfloat)h };
	const GLfloat bgTextureCoords[]   = { 1, 0, 1, 1, 0, 0, 0, 1 };
	const GLfloat proj[] = {
		0, -2.f / w, 0, 0,
		-2.f / h, 0, 0, 0,
		0, 0, 1, 0,
		1, 1, 0, 1
	};

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(proj);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, m_backgroundTextureId);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(2, GL_FLOAT, 0, bgTextureVertices);
	glTexCoordPointer(2, GL_FLOAT, 0, bgTextureCoords);

	glColor4f(1, 1, 1, 1);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glDisable(GL_TEXTURE_2D);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	PrintGLError("end of drawCameraFrame()");
}

void ARDrawingContext::drawAugmentedScene()
{
	if (m_backgroundImage.empty())
		return;

	// Init augmentation projection
	Matrix44 projectionMatrix;

	int w = m_backgroundImage.cols;
	int h = m_backgroundImage.rows;

	buildProjectionMatrix(m_calibration, w, h, projectionMatrix);

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(projectionMatrix.data);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if (isPatternPresent)
	{
		// Set the pattern transformation
		Matrix44 glMatrix = patternPose.getMat44();
		glLoadMatrixf(reinterpret_cast<const GLfloat*>(&glMatrix.data[0]));

		// Render model
		// drawCoordinateAxis();
		draw3DModel();
	}
}

void ARDrawingContext::buildProjectionMatrix(const CameraCalibration& calibration, int screen_width, int screen_height, Matrix44& projectionMatrix)
{
	float nearPlane = 0.01f;  // Near clipping distance
	float farPlane = 100.0f;  // Far clipping distance

	// Camera parameters
	float f_x = calibration.fx();
	float f_y = calibration.fy();
	float c_x = calibration.cx();
	float c_y = calibration.cy();

	projectionMatrix.data[0] = -2.0f * f_x / screen_width;
	projectionMatrix.data[1] = 0.0f;
	projectionMatrix.data[2] = 0.0f;
	projectionMatrix.data[3] = 0.0f;

	projectionMatrix.data[4] = 0.0f;
	projectionMatrix.data[5] = 2.0f * f_y / screen_height;
	projectionMatrix.data[6] = 0.0f;
	projectionMatrix.data[7] = 0.0f;

	projectionMatrix.data[8] = 2.0f * c_x / screen_width - 1.0f;
	projectionMatrix.data[9] = 2.0f * c_y / screen_height - 1.0f;
	projectionMatrix.data[10] = -(farPlane + nearPlane) / (farPlane - nearPlane);
	projectionMatrix.data[11] = -1.0f;

	projectionMatrix.data[12] = 0.0f;
	projectionMatrix.data[13] = 0.0f;
	projectionMatrix.data[14] = -2.0f * farPlane * nearPlane / (farPlane - nearPlane);
	projectionMatrix.data[15] = 0.0f;
}

void ARDrawingContext::drawCoordinateAxis()
{
	static float lineX[] = { 0, 0, 0, 1, 0, 0 };
	static float lineY[] = { 0, 0, 0, 0, 1, 0 };
	static float lineZ[] = { 0, 0, 0, 0, 0, 1 };

	glLineWidth(2);

	glBegin(GL_LINES);

	glColor3f(1.0f, 0.0f, 0.0f);
	glVertex3fv(lineX);
	glVertex3fv(lineX + 3);

	glColor3f(0.0f, 1.0f, 0.0f);
	glVertex3fv(lineY);
	glVertex3fv(lineY + 3);

	glColor3f(0.0f, 0.0f, 1.0f);
	glVertex3fv(lineZ);
	glVertex3fv(lineZ + 3);

	glEnd();
}

void ARDrawingContext::draw3DModel()
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, Texture);

	glBegin(GL_TRIANGLES);

	for (int i = 0; i < (int)vertices.size(); i += 1)
	{
		a = vertices[i];
		b = uvs[i];
		glNormal3f(a.x, a.y, a.z);
		glTexCoord2d(b.x, b.y);
		glVertex3f(a.x, a.y, a.z);
	}

	glEnd();
	glDisable(GL_TEXTURE_2D);

	PrintGLError("end of draw3DModel()");
}

void ARDrawingContext::scale3DModel(float scaleFactor)
{
	for (int i = 0; i < (int)vertices.size(); i += 1)
	{
		vertices[i] = vertices[i] * vec3(scaleFactor, scaleFactor, scaleFactor);
	}

	for (int i = 0; i < (int)normals.size(); i += 1)
	{
		normals[i] = normals[i] * vec3(scaleFactor, scaleFactor, scaleFactor);
	}

	// NOTE: scaling UVs is unusual. Keep if you intentionally want it.
	for (int i = 0; i < (int)uvs.size(); i += 1)
	{
		uvs[i] = uvs[i] * vec2(scaleFactor, scaleFactor);
	}
}
