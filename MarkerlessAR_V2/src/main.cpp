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
#include "dma_driver.h"

// Standard includes:
#include <opencv2/opencv.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
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

bool receive_dma_frame(cv::Mat& grayFrame);

/**
 * Performs full detection routine on camera frame and draws the scene using drawing context.
 * In addition, this function draw overlay with debug information on top of the AR window.
 * Returns true if processing loop should be stopped; otherwise - false.
 */
bool processFrame(const cv::Mat& cameraFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx);

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

    bool shouldQuit = false;
    do
    {

        capture >> currentFrame;
        if (currentFrame.empty())
        {
            shouldQuit = true;
            continue;
        }
        else{
            // Resize frame to 640x480
            cv::resize(currentFrame, currentFrame, cv::Size(640, 480));

            // Prepare buffer with messages
            int total_pixels = 640 * 480;
            int messages = total_pixels / 5;
            size_t buffer_size = messages * 16;
            uint8_t* buffer = new uint8_t[buffer_size];
            int msg_num = 0;
            for(int pixel_idx = 0; pixel_idx < total_pixels; pixel_idx += 5){
                uint8_t* message = buffer + msg_num * 16;
                message[0] = msg_num % 256; // header: message number
                for(int p = 0; p < 5; p++){
                    int idx = pixel_idx + p;
                    int row = idx / 640;
                    int col = idx % 640;
                    cv::Vec3b pixel = currentFrame.at<cv::Vec3b>(row, col);
                    message[1 + p*3] = pixel[0]; // B
                    message[1 + p*3 + 1] = pixel[1]; // G
                    message[1 + p*3 + 2] = pixel[2]; // R
                }
                msg_num++;
            }

            // Send via DMA
            send_via_dma(buffer, buffer_size);

            delete[] buffer;
        }

    

        shouldQuit = processFrame(currentFrame, pipeline, drawingCtx);
    } while (!shouldQuit);
}

void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image)
{
    cv::Size frameSize(image.cols, image.rows);
    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration);

    bool shouldQuit = false;
    do
    {
        shouldQuit = processFrame(image, pipeline, drawingCtx);
    } while (!shouldQuit);
}

bool receive_dma_frame(cv::Mat& grayFrame)
{
    if (grayFrame.empty() || grayFrame.type() != CV_8UC1)
    {
        std::cerr << "[DMA FRAME] ERROR: Invalid output frame (empty or not grayscale)" << std::endl;
        return false;
    }

    const int pixelsPerMessage = 15;
    const int pixelsPerRow = grayFrame.cols;
    const int messagesPerRow = (pixelsPerRow + pixelsPerMessage - 1) / pixelsPerMessage;
    int totalPixels = grayFrame.cols * grayFrame.rows;
    int messages = (totalPixels + pixelsPerMessage - 1) / pixelsPerMessage;
    size_t rawSize = messages * 16;

    uint8_t* rawBuffer = new uint8_t[rawSize];
    receive_via_dma(rawBuffer, rawSize);

    for (int m = 0; m < messages; ++m)
    {
        uint8_t* message = rawBuffer + m * 16;
        int basePixel = m * pixelsPerMessage;
        int remainingPixels = totalPixels - basePixel;
        int copyPixels = min(remainingPixels, pixelsPerMessage);

        // Header is message[0]: cycles 0-42 per row for standard 640-pixel width
        // (each row = 43 messages, so headers reset at row boundaries)
        uint8_t msgHeader = message[0];
        int expectedHeaderPerRow = m % messagesPerRow;
        // Optional: validate header == expectedHeaderPerRow for frame integrity
        
        for (int p = 0; p < copyPixels; ++p)
        {
            grayFrame.data[basePixel + p] = message[1 + p];
        }
    }

    delete[] rawBuffer;
    return true;
}

bool processFrame(const cv::Mat& cameraFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx)
{
    // Clone image used for background (we will draw overlay on it)
    cv::Mat img = cameraFrame.clone();

    // Now let's get this working with DMA: PL already returns a grayscale + gaussian blurred frame.
    // Use it directly for detection; do not re-grayscale or blur it again in this application.
    cv::Mat plGray(CAM_HEIGHT, CAM_WIDTH, CV_8UC1);
    bool gotPlFrame = receive_dma_frame(plGray);

    const cv::Mat& processFrameMat = gotPlFrame ? plGray : cameraFrame;

    drawingCtx.isPatternPresent = pipeline.processFrame(processFrameMat);

    // Update a pattern pose:
    drawingCtx.patternPose = pipeline.getPatternLocation();

    // Draw homography contour on the background image in Debug builds
#if _DEBUG
    if (drawingCtx.isPatternPresent)
    {
        pipeline.getPatternInfo().draw2dContour(img, CV_RGB(0,200,0));
    }
#endif

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
