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
#include "ARPipeline.hpp"
#include "DebugHelpers.hpp"

// Standard includes:
#include <opencv2/opencv.hpp>
#include <cstdlib>
#define NOMINMAX
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#define CAMERA_INDEX 0
#define CAM_WIDTH  640
#define CAM_HEIGHT 480


/**
 * Processes a recorded video or live view from web-camera and allows you to adjust homography refinement and 
 * reprojection threshold in runtime.
 */
void processVideo(const cv::Mat& patternImage, CameraCalibration& calibration, cv::VideoCapture& capture);

/**
 * Processes single image. The processing goes in a loop.
 * It allows you to control the detection process by adjusting homography refinement switch and 
 * reprojection threshold in runtime.
 */
void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image);

/**
 * Performs full detection routine on camera frame and draws the scene using drawing context.
 * In addition, this function draw overlay with debug information on top of the AR window.
 * Returns true if processing loop should be stopped; otherwise - false.
 */
bool processFrame(const cv::Mat& cameraFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx);

static void configureImageOverlay(ARDrawingContext& drawingCtx);

int main(int argc, const char * argv[])
{
    // Change this calibration to yours:
    CameraCalibration calibration(526.58037684199849f, 524.65577209994706f, 318.41744018680112f, 202.96659047014398f);
	
    if (argc < 2)
    {
        std::cout << "Input image not specified" << std::endl;
        std::cout << "Usage: markerless_ar_demo <pattern image> [filepath to recorded video or image]" << std::endl;
        return 1;
    }

    // Try to read the pattern:
    cv::Mat patternImage = cv::imread(argv[1]);
    if (patternImage.empty())
    {
        std::cout << "Input image cannot be read" << std::endl;
        return 2;
    }

    if (argc == 2)
    {
        cv::VideoCapture cap;

        // Open camera explicitly
        cap.open(CAMERA_INDEX, cv::CAP_V4L2);
        if (!cap.isOpened())
        {
            std::cerr << "Failed to open camera" << std::endl;
            return 1;
        }

        //  Set format BEFORE first frame is grabbed
        cap.set(cv::CAP_PROP_FRAME_WIDTH,  CAM_WIDTH);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, CAM_HEIGHT);

        // Prefer MJPEG (huge for USB stability)
        cap.set(cv::CAP_PROP_FOURCC,
                cv::VideoWriter::fourcc('M','J','P','G'));

        // Optional: set FPS
        cap.set(cv::CAP_PROP_FPS, 30);

        // Confirm what you actually got
        std::cout << "Camera opened at "
                << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
                << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
                << std::endl;

        processVideo(patternImage, calibration, cap);    
    }
    else if (argc == 3)
    {
        std::string input = argv[2];
        cv::Mat testImage = cv::imread(input);
        if (!testImage.empty())
        {
            processSingleImage(patternImage, calibration, testImage);
        }
        else 
        {
            cv::VideoCapture cap;
            if (cap.open(input))
            {
                processVideo(patternImage, calibration, cap);
            }
        }
    }
    else
    {
        std::cerr << "Invalid number of arguments passed" << std::endl;
        return 1;
    }

    return 0;
}

void processVideo(const cv::Mat& patternImage, CameraCalibration& calibration, cv::VideoCapture& capture)
{
    // Grab first frame to get the frame dimensions
    cv::Mat currentFrame;  
    capture >> currentFrame;

    // Check the capture succeeded:
    if (currentFrame.empty())
    {
        std::cout << "Cannot open video capture device" << std::endl;
        return;
    }

    cv::Size frameSize(currentFrame.cols, currentFrame.rows);

    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration);
    // Load optional overlay image once and keep it in rendering context.
    configureImageOverlay(drawingCtx);

    bool shouldQuit = false;
    do
    {
        capture >> currentFrame;
        if (currentFrame.empty())
        {
            shouldQuit = true;
            continue;
        }

        shouldQuit = processFrame(currentFrame, pipeline, drawingCtx);
    } while (!shouldQuit);
}

void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image)
{
    cv::Size frameSize(image.cols, image.rows);
    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration);
    // Load optional overlay image once and keep it in rendering context.
    configureImageOverlay(drawingCtx);

    bool shouldQuit = false;
    do
    {
        shouldQuit = processFrame(image, pipeline, drawingCtx);
    } while (!shouldQuit);
}

bool processFrame(const cv::Mat& cameraFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx)
{
    // Clone image used for background (we will draw overlay on it)
    cv::Mat img = cameraFrame.clone();

    // Draw information:
    // COMMENTED OUT: Overlay text disabled for homography testing
    // if (pipeline.m_patternDetector.enableHomographyRefinement)
    //     cv::putText(img, "Pose refinement: On   ('h' to switch off)", cv::Point(10,15), cv::FONT_HERSHEY_PLAIN, 1, CV_RGB(0,200,0));
    // else
    //     cv::putText(img, "Pose refinement: Off  ('h' to switch on)",  cv::Point(10,15), cv::FONT_HERSHEY_PLAIN, 1, CV_RGB(0,200,0));

    cv::putText(img, "RANSAC threshold: " + ToString(pipeline.m_patternDetector.homographyReprojectionThreshold) + "( Use'-'/'+' to adjust)", cv::Point(10, 30), cv::FONT_HERSHEY_PLAIN, 1, CV_RGB(0,200,0));

    // Find a pattern and update it's detection status:
    drawingCtx.isPatternPresent = pipeline.processFrame(cameraFrame);

    // Update a pattern pose:
    drawingCtx.patternPose = pipeline.getPatternLocation();

    // Update 2D pattern corners for pattern-locked image overlay.
    // This uses existing detector output and does not alter detection behavior.
    if (drawingCtx.isPatternPresent)
        drawingCtx.setPatternOverlayState(true, pipeline.getPatternInfo().points2d);
    else
        drawingCtx.setPatternOverlayState(false, std::vector<cv::Point2f>());

    // Set a new camera frame:
    drawingCtx.updateBackground(img);

    // Request redraw of the window:
    drawingCtx.updateWindow();

    // Read the keyboard input:
    int keyCode = cv::waitKey(5); 

    bool shouldQuit = false;
    if (keyCode == '+' || keyCode == '=')
    {
        pipeline.m_patternDetector.homographyReprojectionThreshold += 0.2f;
        pipeline.m_patternDetector.homographyReprojectionThreshold = min(10.0f, pipeline.m_patternDetector.homographyReprojectionThreshold);
    }
    else if (keyCode == '-')
    {
        pipeline.m_patternDetector.homographyReprojectionThreshold -= 0.2f;
        pipeline.m_patternDetector.homographyReprojectionThreshold = max(0.0f, pipeline.m_patternDetector.homographyReprojectionThreshold);
    }
    else if (keyCode == 'h')
    {
        pipeline.m_patternDetector.enableHomographyRefinement = !pipeline.m_patternDetector.enableHomographyRefinement;
    }
    else if (keyCode == 27 || keyCode == 'q')
    {
        shouldQuit = true;
    }

    return shouldQuit;
}

static void configureImageOverlay(ARDrawingContext& drawingCtx)
{
    // The environment variable takes precedence over the default in-repo path.
    const char* overlayPath = std::getenv("AR_OVERLAY_IMAGE");
    std::string resolvedPath = overlayPath ? overlayPath : "Artifacts/overlay.png";

    // IMREAD_UNCHANGED preserves alpha channel for proper compositing.
    cv::Mat overlay = cv::imread(resolvedPath, cv::IMREAD_UNCHANGED);
    if (overlay.empty())
    {
        if (overlayPath)
        {
            std::cerr << "Overlay image could not be loaded from AR_OVERLAY_IMAGE: "
                      << resolvedPath << std::endl;
        }
        return;
    }

    drawingCtx.setOverlayImage(overlay);
    drawingCtx.setOverlayEnabled(true);
    std::cout << "Image overlay enabled: " << resolvedPath << std::endl;
}
