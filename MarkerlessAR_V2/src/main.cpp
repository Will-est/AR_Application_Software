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
#include "dma_driver.hpp"

// Standard includes:
#include <opencv2/opencv.hpp>
#include <cstdlib>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>
#define NOMINMAX
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#define CAMERA_INDEX 0
#define CAM_WIDTH  640
#define CAM_HEIGHT 480

#define ACCEL_CTRL_ADDR_IN_BREATH        0x10
#define ACCEL_CTRL_ADDR_OUT_BREATH       0x20
#define ACCEL_CTRL_ADDR_BGR_FIFO_BREATH  0x30
#define ACCEL_CTRL_ADDR_PAD_FIFO_BREATH  0x40
#define ACCEL_CTRL_ADDR_GRAY_FIFO_BREATH 0x50






namespace
{
int getTargetFps()
{
    // Keep default smoothness while avoiding busy render loops.
    int fps = 30;
    const char* rawFps = std::getenv("AR_TARGET_FPS");
    if (rawFps)
    {
        int parsed = std::atoi(rawFps);
        if (parsed > 0)
            fps = parsed;
    }

    return max(1, min(120, fps));
}

std::string resolvePatternImagePath(const char* overridePath = nullptr)
{
    if (overridePath && *overridePath)
    {
        cv::Mat image = cv::imread(overridePath, cv::IMREAD_COLOR);
        if (!image.empty())
            return overridePath;
    }

    const char* envPatternPath = std::getenv("AR_PATTERN_IMAGE");
    if (envPatternPath && *envPatternPath)
        return envPatternPath;

    const char* candidates[] = {
        "pattern.png",
        "../Artifacts/pattern.png",
        "../../Artifacts/pattern.png",
        "Artifacts/pattern.png",
        "/home/shreeya607/seniordesign/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern.png"
    };

    for (const char* candidate : candidates)
    {
        cv::Mat image = cv::imread(candidate, cv::IMREAD_COLOR);
        if (!image.empty())
            return candidate;
    }

    return overridePath ? overridePath : "Artifacts/pattern.png";
}

std::string resolvePatternHexdumpPath()
{
    const char* envHexdumpPath = std::getenv("AR_PATTERN_HEXDUMP");
    if (envHexdumpPath && *envHexdumpPath)
        return envHexdumpPath;

    return "/home/shreeya607/seniordesign/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern_hex.txt";
}

std::string resolvePatternHexdumpBeforePath()
{
    const char* envBeforePath = std::getenv("AR_PATTERN_HEX_BEFORE");
    if (envBeforePath && *envBeforePath)
        return envBeforePath;

    return "/home/shreeya607/seniordesign/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern_before_hex.txt";
}

std::string resolvePatternHexdumpAfterPath()
{
    const char* envAfterPath = std::getenv("AR_PATTERN_HEX_AFTER");
    if (envAfterPath && *envAfterPath)
        return envAfterPath;

    // Fall back to the original single grayscale hexdump path behavior.
    return resolvePatternHexdumpPath();
}

void writeColorHexdump(std::ostream& output, const cv::Mat& colorImage)
{
    CV_Assert(colorImage.type() == CV_8UC3);

    cv::Mat contiguous = colorImage.isContinuous() ? colorImage : colorImage.clone();
    const unsigned char* bytes = contiguous.ptr<unsigned char>(0);
    const size_t byteCount = contiguous.total() * contiguous.channels();

    output << "Color image (BGR): " << contiguous.cols << "x" << contiguous.rows
           << " (" << byteCount << " bytes)" << std::endl;

    for (size_t offset = 0; offset < byteCount; offset += 16)
    {
        output << std::setfill('0') << std::setw(8) << std::hex << offset << "  ";

        for (size_t index = 0; index < 16; ++index)
        {
            if (offset + index < byteCount)
            {
                const unsigned int value = bytes[offset + index];
                output << std::setw(2) << value << ' ';
            }
            else
            {
                output << "   ";
            }
        }

        output << " ";
        for (size_t index = 0; index < 16 && offset + index < byteCount; ++index)
        {
            const unsigned char value = bytes[offset + index];
            output << (std::isprint(value) ? static_cast<char>(value) : '.');
        }

        output << std::endl;
    }

    output << std::dec << std::setfill(' ');
}

void writeHexdump(std::ostream& output, const cv::Mat& grayImage)
{
    CV_Assert(grayImage.type() == CV_8UC1);

    cv::Mat contiguous = grayImage.isContinuous() ? grayImage : grayImage.clone();
    const unsigned char* bytes = contiguous.ptr<unsigned char>(0);
    const size_t byteCount = contiguous.total();

    output << "Grayscale image: " << contiguous.cols << "x" << contiguous.rows
           << " (" << byteCount << " bytes)" << std::endl;

    for (size_t offset = 0; offset < byteCount; offset += 16)
    {
        output << std::setfill('0') << std::setw(8) << std::hex << offset << "  ";

        for (size_t index = 0; index < 16; ++index)
        {
            if (offset + index < byteCount)
            {
                const unsigned int value = bytes[offset + index];
                output << std::setw(2) << value << ' ';
            }
            else
            {
                output << "   ";
            }
        }

        output << " ";
        for (size_t index = 0; index < 16 && offset + index < byteCount; ++index)
        {
            const unsigned char value = bytes[offset + index];
            output << (std::isprint(value) ? static_cast<char>(value) : '.');
        }

        output << std::endl;
    }

    output << std::dec << std::setfill(' ');
}

bool getEnvFlag(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value && std::strcmp(value, "0") != 0;
}

int getEnvInt(const char* name, int fallback)
{
    const char* value = std::getenv(name);
    if (!value || !*value)
        return fallback;
    return std::atoi(value);
}
}


static void log_breath(const char* tag)
{
    fprintf(stderr,
        "[BREATH %s] in=%3u  out=%3u  bgr_fifo=%3u  pad_fifo=%3u  gray_fifo=%3u\n",
        tag,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_IN_BREATH)        & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_OUT_BREATH)       & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_BGR_FIFO_BREATH)  & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_PAD_FIFO_BREATH)  & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_GRAY_FIFO_BREATH) & 0xFF);
}

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
bool processFrame(const cv::Mat& displayFrame, const cv::Mat& processedFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx);

static void configureImageOverlay(ARDrawingContext& drawingCtx);

// DMA helpers (16B messages)
bool send_dma_frame(const cv::Mat& currentFrame);
int receive_dma_frame(cv::Mat& grayFrame);

#if 0
int main(int argc, const char* argv[])
{
    const std::string patternPath = resolvePatternImagePath(argc >= 2 ? argv[1] : nullptr);
    const std::string hexdumpBeforePath = resolvePatternHexdumpBeforePath();
    const std::string hexdumpAfterPath = resolvePatternHexdumpAfterPath();

    if (argc >= 2)
    {
        std::cout << "Using pattern image from command line: " << argv[1] << std::endl;
    }

    cv::Mat patternImage = cv::imread(patternPath, cv::IMREAD_COLOR);
    if (patternImage.empty())
    {
        std::cerr << "Could not read pattern image: " << patternPath << std::endl;
        return 1;
    }

    cv::resize(patternImage, patternImage, cv::Size(640, 480));

    std::ofstream hexdumpBeforeFile(hexdumpBeforePath.c_str());
    if (!hexdumpBeforeFile.is_open())
    {
        std::cerr << "Could not open before-hexdump file for writing: "
                  << hexdumpBeforePath << std::endl;
        return 1;
    }

    std::cout << "Loaded pattern image: " << patternPath << std::endl;
    writeColorHexdump(hexdumpBeforeFile, patternImage);
    std::cout << "Wrote color hexdump to: " << hexdumpBeforePath << std::endl;

    cv::Mat grayPattern;
    cv::cvtColor(patternImage, grayPattern, cv::COLOR_BGR2GRAY); // grayscale

    cv::Mat blurredPattern;
    cv::GaussianBlur(grayPattern, blurredPattern, cv::Size(5, 5), 0);

    std::ofstream hexdumpAfterFile(hexdumpAfterPath.c_str());
    if (!hexdumpAfterFile.is_open())
    {
        std::cerr << "Could not open after-hexdump file for writing: "
                  << hexdumpAfterPath << std::endl;
        return 1;
    }

    writeHexdump(hexdumpAfterFile, blurredPattern);
    std::cout << "Wrote grayscale hexdump to: " << hexdumpAfterPath << std::endl;

    return 0;
}
#else
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

        // Try opening with V4L2 backend first (most direct for Linux)
        std::cout << "Attempting to open camera device " << CAMERA_INDEX << " with V4L2..." << std::endl;
        cap.open(CAMERA_INDEX, cv::CAP_V4L2);
        
        if (!cap.isOpened())
        {
            std::cerr << "V4L2 backend failed, trying generic backend..." << std::endl;
            cap.open(CAMERA_INDEX);  // Try with default backend
            
            if (!cap.isOpened())
            {
                std::cerr << "Failed to open camera on /dev/video" << CAMERA_INDEX << std::endl;
                std::cerr << "Ensure the camera is connected and permissions are correct." << std::endl;
                std::cerr << "Try: ls -la /dev/video*" << std::endl;
                return 1;
            }
        }

        // Give the camera time to initialize (especially important for USB cameras)
        std::cout << "Camera opened, initializing..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Set format BEFORE first frame is grabbed
        // Start with native resolution, then resize in software if needed
        cap.set(cv::CAP_PROP_FRAME_WIDTH,  1280);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 800);
        
        // Try MJPEG first (better for USB stability), fall back to H264 if needed
        int fourcc = cv::VideoWriter::fourcc('M','J','P','G');
        cap.set(cv::CAP_PROP_FOURCC, fourcc);
        
        // Set FPS (camera default is 25 fps for OV9782)
        cap.set(cv::CAP_PROP_FPS, 25);
        // Confirm what we actually got
        double actualWidth = cap.get(cv::CAP_PROP_FRAME_WIDTH);
        double actualHeight = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
        double actualFps = cap.get(cv::CAP_PROP_FPS);
        
        std::cout << "Camera initialized at " << actualWidth << "x" << actualHeight 
                  << " @ " << actualFps << " fps" << std::endl;
        
        // Try to grab a test frame to verify camera is responsive
        cv::Mat testFrame;
        if (!cap.read(testFrame) || testFrame.empty())
        {
            std::cerr << "Warning: Could not read test frame from camera" << std::endl;
            std::cerr << "Camera may still initialize on first processVideo call" << std::endl;
            // Don't fail here, camera might just need warmup time
        }
        else
        {
            std::cout << "Test frame captured successfully (" << testFrame.cols << "x" << testFrame.rows << ")" << std::endl;
        }

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
#endif

void processVideo(const cv::Mat& patternImage, CameraCalibration& calibration, cv::VideoCapture& capture)
{
    cv::Mat currentFrame;
    capture >> currentFrame;

    if (currentFrame.empty())
    {
        std::cout << "Cannot open video capture device" << std::endl;
        return;
    }

    if (dma_init() != 0)
    {
        std::cerr << "[DMA] init failed" << std::endl;
        return;
    }
    log_breath("POST-INIT");

    cv::Size frameSize(CAM_WIDTH, CAM_HEIGHT);

    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration);
    configureImageOverlay(drawingCtx);

    using Clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::milliseconds(1000 / getTargetFps());
    auto nextFrameDeadline = Clock::now();

    std::atomic<bool> dmaRunning{true};
    std::mutex txMutex;
    std::condition_variable txCv;
    cv::Mat pendingTxFrame;
    bool hasPendingTx = false;

    std::mutex rxMutex;
    cv::Mat latestProcessedFrame;
    bool hasProcessedFrame = false;

    std::thread rxThread([&]()
    {
        std::cerr << "[RX] DMA receive thread started" << std::endl;
        const int timeoutLimit = getEnvInt("AR_DMA_RX_TIMEOUT_LIMIT", 0);
        int consecutiveTimeouts = 0;
        while (dmaRunning.load())
        {
            cv::Mat processed;
            const int rc = receive_dma_frame(processed);
            if (rc != 0)
            {
                if (rc == ETIMEDOUT)
                {
                    ++consecutiveTimeouts;
                    if (consecutiveTimeouts == 1 || (consecutiveTimeouts % 10) == 0)
                    {
                        std::cerr << "[RX] receive timed out (" << consecutiveTimeouts
                                  << " consecutive)" << std::endl;
                    }
                    if (timeoutLimit > 0 && consecutiveTimeouts >= timeoutLimit)
                    {
                        std::cerr << "[RX] timeout limit reached, stopping DMA" << std::endl;
                        dmaRunning.store(false);
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                std::cerr << "[RX] receive failed rc=" << rc << ", stopping DMA" << std::endl;
                dmaRunning.store(false);
                break;
            }

            std::lock_guard<std::mutex> lock(rxMutex);
            latestProcessedFrame = processed;
            hasProcessedFrame = true;
            consecutiveTimeouts = 0;
        }

        std::cerr << "[RX] DMA receive thread exiting" << std::endl;
    });

    std::thread txThread([&]()
    {
        while (dmaRunning.load())
        {
            cv::Mat frameToSend;
            {
                std::unique_lock<std::mutex> lock(txMutex);
                txCv.wait(lock, [&]() { return !dmaRunning.load() || hasPendingTx; });
                if (!dmaRunning.load())
                    break;

                frameToSend = pendingTxFrame;
                hasPendingTx = false;
            }

            if (!send_dma_frame(frameToSend))
            {
                dmaRunning.store(false);
                break;
            }
        }
    });

    bool shouldQuit = false;
    do
    {
        capture >> currentFrame;
        if (currentFrame.empty())
        {
            shouldQuit = true;
            continue;
        }

        cv::Mat displayFrame;
        if (currentFrame.cols != CAM_WIDTH || currentFrame.rows != CAM_HEIGHT)
            cv::resize(currentFrame, displayFrame, cv::Size(CAM_WIDTH, CAM_HEIGHT));
        else
            displayFrame = currentFrame;

        if (displayFrame.type() != CV_8UC3)
        {
            cv::Mat converted;
            displayFrame.convertTo(converted, CV_8U);
            if (converted.channels() == 1)
                cv::cvtColor(converted, displayFrame, cv::COLOR_GRAY2BGR);
            else if (converted.channels() == 4)
                cv::cvtColor(converted, displayFrame, cv::COLOR_BGRA2BGR);
            else
                displayFrame = converted;
        }

        {
            std::lock_guard<std::mutex> lock(txMutex);
            pendingTxFrame = displayFrame;
            hasPendingTx = true;
        }
        txCv.notify_one();

        cv::Mat processedForDetection;
        {
            std::lock_guard<std::mutex> lock(rxMutex);
            if (hasProcessedFrame)
                processedForDetection = latestProcessedFrame;
        }

        if (processedForDetection.empty())
        {
            std::cerr << "[TEST] no DMA frame received yet, skipping detection this frame\n";
            processedForDetection = displayFrame;
        }

        shouldQuit = processFrame(displayFrame, processedForDetection, pipeline, drawingCtx);
        if (!shouldQuit)
        {
            nextFrameDeadline += framePeriod;
            const auto now = Clock::now();
            if (now < nextFrameDeadline)
                std::this_thread::sleep_until(nextFrameDeadline);
            else
                nextFrameDeadline = now;
        }
    } while (!shouldQuit);

    dmaRunning.store(false);
    txCv.notify_all();
    txThread.join();
    rxThread.join();
}

void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image)
{
    cv::Size frameSize(image.cols, image.rows);
    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration);
    // Load optional overlay image once and keep it in rendering context.
    configureImageOverlay(drawingCtx);

    using Clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::milliseconds(1000 / getTargetFps());
    auto nextFrameDeadline = Clock::now();

    bool shouldQuit = false;
    do
    {
        shouldQuit = processFrame(image, image, pipeline, drawingCtx);
        if (!shouldQuit)
        {
            nextFrameDeadline += framePeriod;
            const auto now = Clock::now();
            if (now < nextFrameDeadline)
                std::this_thread::sleep_until(nextFrameDeadline);
            else
                nextFrameDeadline = now;
        }
    } while (!shouldQuit);
}

bool processFrame(const cv::Mat& displayFrame, const cv::Mat& processedFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx)
{
    // Clone image used for background (we will draw overlay on it)
    cv::Mat img = displayFrame.clone();

    // Draw information:
    // COMMENTED OUT: Overlay text disabled for homography testing
    // if (pipeline.m_patternDetector.enableHomographyRefinement)
    //     cv::putText(img, "Pose refinement: On   ('h' to switch off)", cv::Point(10,15), cv::FONT_HERSHEY_PLAIN, 1, CV_RGB(0,200,0));
    // else
    //     cv::putText(img, "Pose refinement: Off  ('h' to switch on)",  cv::Point(10,15), cv::FONT_HERSHEY_PLAIN, 1, CV_RGB(0,200,0));

    cv::putText(img, "RANSAC threshold: " + ToString(pipeline.m_patternDetector.homographyReprojectionThreshold) + "( Use'-'/'+' to adjust)", cv::Point(10, 30), cv::FONT_HERSHEY_PLAIN, 1, CV_RGB(0,200,0));

    // Find a pattern and update it's detection status:
    drawingCtx.isPatternPresent = pipeline.processFrame(processedFrame);

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
    std::string resolvedPath = overlayPath
        ? overlayPath
        : "/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/overlay.png";

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

bool send_dma_frame(const cv::Mat& currentFrame)
{
    if (currentFrame.empty() || currentFrame.type() != CV_8UC3)
    {
        std::cerr << "[DMA] send_dma_frame: invalid input frame (need CV_8UC3)" << std::endl;
        return false;
    }

    cv::Mat frame;
    if (currentFrame.cols != CAM_WIDTH || currentFrame.rows != CAM_HEIGHT)
        cv::resize(currentFrame, frame, cv::Size(CAM_WIDTH, CAM_HEIGHT));
    else
        frame = currentFrame;

    constexpr int blockRows = 5;
    constexpr int bytesPerPixel = 3;
    constexpr int headerBytes = 16;
    constexpr int payloadBytesPerBlock = blockRows * CAM_WIDTH * bytesPerPixel; // 9600
    constexpr int transferBytesPerBlock = headerBytes + payloadBytesPerBlock;   // 9616

    static_assert((payloadBytesPerBlock % 16) == 0, "5-row payload must be 16B aligned");
    static_assert((transferBytesPerBlock % 16) == 0, "transfer size must be 16B aligned");
    static_assert((CAM_HEIGHT % blockRows) == 0, "CAM_HEIGHT must be divisible by 5");

    uint8_t* src = reinterpret_cast<uint8_t*>(virtual_src_addr);

    int blockIndex = 0;
    for (int startRow = 0; startRow < CAM_HEIGHT; startRow += blockRows, ++blockIndex)
    {
        std::memset(src, 0, headerBytes);
        src[0] = static_cast<uint8_t>(blockIndex & 0xFF);

        uint8_t* payload = src + headerBytes;
        size_t payloadOffset = 0;
        for (int r = 0; r < blockRows; ++r)
        {
            const uint8_t* rowBytes = frame.ptr<uint8_t>(startRow + r);
            std::memcpy(payload + payloadOffset, rowBytes, static_cast<size_t>(CAM_WIDTH * bytesPerPixel));
            payloadOffset += static_cast<size_t>(CAM_WIDTH * bytesPerPixel);
        }

        auto startMm2sTransfer = [&]()
        {
            write_dma(dma_virtual_addr, MM2S_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
            write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
            write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
            write_dma(dma_virtual_addr, MM2S_TRNSFR_LENGTH_REGISTER, transferBytesPerBlock);
            return dma_mm2s_sync(dma_virtual_addr);
        };

        int rc = startMm2sTransfer();
        if (rc != 0)
        {
            std::cerr << "[DMA] send_dma_frame: block " << blockIndex
                      << " (rows " << startRow << "-" << (startRow + blockRows - 1)
                      << ") failed rc=" << rc << " (retrying once)" << std::endl;

            write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
            write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);

            rc = startMm2sTransfer();
            if (rc != 0)
            {
                std::cerr << "[DMA] send_dma_frame: block " << blockIndex
                          << " (rows " << startRow << "-" << (startRow + blockRows - 1)
                          << ") retry failed rc=" << rc << std::endl;
                log_breath("MM2S-RETRY-FAIL");
                return false;
            }
        }
        printf("[DMA] send_dma_frame: block %d sent OK\n", blockIndex);
    }

    log_breath("TX-FRAME-DONE");
    std::cout << "[DMA] frame sent successfully" << std::endl;
    return true;
}

int receive_dma_frame(cv::Mat& grayFrame)
{
    grayFrame.create(CAM_HEIGHT, CAM_WIDTH, CV_8UC1);

    constexpr int headerBytes = 16;
    constexpr int payloadBytes = CAM_WIDTH;
    constexpr int rowBytes = headerBytes + payloadBytes; // 656
    static_assert((rowBytes % 16) == 0, "row transfer must be 16B aligned");

    std::uint8_t* rx = reinterpret_cast<std::uint8_t*>(virtual_dst_addr);

    auto resetAndRunS2mm = [&]()
    {
        using Clock = std::chrono::steady_clock;
        write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RESET_DMA);
        const auto deadline = Clock::now() + std::chrono::milliseconds(50);
        while ((read_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER) & RESET_DMA) != 0)
        {
            if (Clock::now() > deadline)
                break;
        }
        write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
    };

    int headerMismatchCount = 0;

    for (int row = 0; row < CAM_HEIGHT; ++row)
    {
        std::uint8_t* dstRow = grayFrame.ptr<std::uint8_t>(row);

        auto startS2mmTransfer = [&]()
        {
            write_dma(dma_virtual_addr, S2MM_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
            write_dma(dma_virtual_addr, S2MM_DST_ADDRESS_REGISTER, DESTINATION_ADDR);
            write_dma(dma_virtual_addr, S2MM_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
            write_dma(dma_virtual_addr, S2MM_BUFF_LENGTH_REGISTER, rowBytes);
            return dma_s2mm_sync(dma_virtual_addr);
        };

        const unsigned int s2mmStatus = read_dma(dma_virtual_addr, S2MM_STATUS_REGISTER);
        if (s2mmStatus & STATUS_HALTED)
        {
            std::cerr << "[DMA] receive_dma_frame: S2MM halted before row " << row << ", resetting" << std::endl;
            resetAndRunS2mm();
        }

        int rc = startS2mmTransfer();
        if (rc != 0)
        {
            std::cerr << "[DMA] receive_dma_frame: row " << row
                      << " failed rc=" << rc << " (retrying once)" << std::endl;
            log_breath("S2MM-ROW-FAIL");
            resetAndRunS2mm();
            rc = startS2mmTransfer();
            if (rc != 0)
            {
                std::cerr << "[DMA] receive_dma_frame: row " << row
                          << " retry failed rc=" << rc << std::endl;
                log_breath("S2MM-RETRY-FAIL");
                return rc;
            }
        }

        if (row == 0)
        {
            printf("[DMA] receive_dma_frame: row0 header bytes: ");
            print_mem(rx, headerBytes);
        }

        const uint16_t headerId16 = static_cast<uint16_t>(rx[0]) | (static_cast<uint16_t>(rx[1]) << 8);
        const uint8_t  headerId8  = rx[0];
        const uint16_t expected16 = static_cast<uint16_t>(row);
        const uint8_t  expected8  = static_cast<uint8_t>(row & 0xFF);
        if (!(headerId16 == expected16 || headerId8 == expected8))
        {
            ++headerMismatchCount;
            std::cerr << "[DMA] warning: row header id=" << headerId16
                      << " (byte0=" << static_cast<int>(headerId8) << ") expected "
                      << expected16 << " at row " << row << std::endl;
        }

        std::memcpy(dstRow, rx + headerBytes, payloadBytes);
    }

    if (headerMismatchCount > 0)
        std::cerr << "[DMA] frame received with row-header mismatches: " << headerMismatchCount << std::endl;

    std::cout << "[DMA] frame received successfully" << std::endl;
    return 0;
}
