/*
---------------------------------------------------------------------
--- Author         : Ahmet Ozlu
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
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define NOMINMAX
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#define CAMERA_INDEX 0
#define CAM_WIDTH  640
#define CAM_HEIGHT 480

namespace
{
bool parseEnvBool(const char* name, bool defaultValue)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw)
        return defaultValue;

    const std::string value(raw);
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

int parseEnvInt(const char* name, int defaultValue, int minValue, int maxValue)
{
    const char* raw = std::getenv(name);
    if (!raw || !*raw)
        return defaultValue;

    const int parsed = std::atoi(raw);
    if (parsed < minValue || parsed > maxValue)
        return defaultValue;

    return parsed;
}

class MjpegHttpStreamer
{
public:
    MjpegHttpStreamer()
        : m_enabled(parseEnvBool("AR_STREAM_ENABLE", false))
        , m_running(false)
        , m_serverFd(-1)
        , m_port(parseEnvInt("AR_STREAM_PORT", 5969, 1, 65535))
        , m_bindAddress(resolveBindAddress())
        , m_jpegQuality(parseEnvInt("AR_STREAM_JPEG_QUALITY", 80, 30, 100))
        , m_frameSequence(0)
    {
    }

    ~MjpegHttpStreamer()
    {
        stop();
    }

    bool enabled() const
    {
        return m_enabled;
    }

    bool start()
    {
        if (!m_enabled || m_running)
            return true;

        m_serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_serverFd < 0)
        {
            std::cerr << "MJPEG streamer: could not create socket" << std::endl;
            return false;
        }

        const int reuse = 1;
        setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(static_cast<uint16_t>(m_port));

        if (::inet_pton(AF_INET, m_bindAddress.c_str(), &serverAddr.sin_addr) != 1)
        {
            std::cerr << "MJPEG streamer: invalid AR_STREAM_BIND_IPV4 value: "
                      << m_bindAddress << std::endl;
            ::close(m_serverFd);
            m_serverFd = -1;
            return false;
        }

        if (::bind(m_serverFd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
        {
            std::cerr << "MJPEG streamer: bind failed on " << m_bindAddress
                      << ":" << m_port << std::endl;
            ::close(m_serverFd);
            m_serverFd = -1;
            return false;
        }

        if (::listen(m_serverFd, 4) < 0)
        {
            std::cerr << "MJPEG streamer: listen failed" << std::endl;
            ::close(m_serverFd);
            m_serverFd = -1;
            return false;
        }

        m_running = true;
        m_serverThread = std::thread(&MjpegHttpStreamer::serverLoop, this);

        std::cout << "MJPEG streamer listening on http://" << m_bindAddress
                  << ":" << m_port << "/stream.mjpg" << std::endl;
        return true;
    }

    void stop()
    {
        if (!m_running)
            return;

        m_running = false;
        m_frameReady.notify_all();

        if (m_serverFd >= 0)
        {
            ::shutdown(m_serverFd, SHUT_RDWR);
            ::close(m_serverFd);
            m_serverFd = -1;
        }

        if (m_serverThread.joinable())
            m_serverThread.join();
    }

    void publishFrame(const cv::Mat& frame)
    {
        if (!m_running || frame.empty())
            return;

        std::vector<unsigned char> encoded;
        std::vector<int> params;
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(m_jpegQuality);

        if (!cv::imencode(".jpg", frame, encoded, params))
            return;

        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_latestFrame.swap(encoded);
            ++m_frameSequence;
        }

        m_frameReady.notify_all();
    }

private:
    static std::string resolveBindAddress()
    {
        const char* raw = std::getenv("AR_STREAM_BIND_IPV4");
        if (!raw || !*raw)
            return "0.0.0.0";
        return raw;
    }

    void serverLoop()
    {
        while (m_running)
        {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            const int clientFd = ::accept(m_serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientFd < 0)
            {
                if (m_running)
                    std::cerr << "MJPEG streamer: accept failed" << std::endl;
                continue;
            }

            std::thread(&MjpegHttpStreamer::handleClient, this, clientFd).detach();
        }
    }

    void handleClient(int clientFd)
    {
        std::string request(1024, '\0');
        const ssize_t received = ::recv(clientFd, &request[0], request.size(), 0);
        if (received <= 0)
        {
            ::close(clientFd);
            return;
        }

        request.resize(static_cast<size_t>(received));
        const bool wantsRoot = request.find("GET / ") == 0;
        const bool wantsStream = request.find("GET /stream.mjpg ") == 0 || wantsRoot;

        if (!wantsStream)
        {
            static const char kNotFound[] =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n\r\n"
                "Not found\r\n";
            sendAll(clientFd, kNotFound, sizeof(kNotFound) - 1);
            ::close(clientFd);
            return;
        }

        static const char kHeader[] =
            "HTTP/1.1 200 OK\r\n"
            "Cache-Control: no-cache\r\n"
            "Pragma: no-cache\r\n"
            "Connection: close\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";

        if (!sendAll(clientFd, kHeader, sizeof(kHeader) - 1))
        {
            ::close(clientFd);
            return;
        }

        size_t lastSeenSequence = 0;
        while (m_running)
        {
            std::vector<unsigned char> frame;
            size_t currentSequence = 0;

            {
                std::unique_lock<std::mutex> lock(m_frameMutex);
                m_frameReady.wait(lock, [this, lastSeenSequence] {
                    return !m_running || (!m_latestFrame.empty() && m_frameSequence > lastSeenSequence);
                });

                if (!m_running)
                    break;

                frame = m_latestFrame;
                currentSequence = m_frameSequence;
            }

            std::ostringstream partHeader;
            partHeader << "--frame\r\n"
                       << "Content-Type: image/jpeg\r\n"
                       << "Content-Length: " << frame.size() << "\r\n\r\n";

            const std::string header = partHeader.str();
            if (!sendAll(clientFd, header.c_str(), header.size()) ||
                !sendAll(clientFd, reinterpret_cast<const char*>(frame.data()), frame.size()) ||
                !sendAll(clientFd, "\r\n", 2))
            {
                break;
            }

            lastSeenSequence = currentSequence;
        }

        ::close(clientFd);
    }

    static bool sendAll(int fd, const char* data, size_t size)
    {
        size_t totalSent = 0;
        while (totalSent < size)
        {
            const ssize_t sent = ::send(fd, data + totalSent, size - totalSent, 0);
            if (sent <= 0)
                return false;
            totalSent += static_cast<size_t>(sent);
        }

        return true;
    }

private:
    bool m_enabled;
    bool m_running;
    int m_serverFd;
    int m_port;
    std::string m_bindAddress;
    int m_jpegQuality;
    std::thread m_serverThread;
    std::mutex m_frameMutex;
    std::condition_variable m_frameReady;
    std::vector<unsigned char> m_latestFrame;
    size_t m_frameSequence;
};

int getTargetFps()
{
    // Keep default smoothness while avoiding busy render loops.
    return parseEnvInt("AR_TARGET_FPS", 30, 1, 120);
}

bool isHeadlessRequested()
{
    return parseEnvBool("AR_HEADLESS", false);
}

std::string resolvePatternImagePath()
{
    const char* envPatternPath = std::getenv("AR_PATTERN_IMAGE");
    if (envPatternPath && *envPatternPath)
        return envPatternPath;

    const char* candidates[] = {
        "pattern.png",
        "../Artifacts/pattern.png",
        "Artifacts/pattern.png",
        "/home/shreeya607/seniordesign/AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern.png"
    };

    for (const char* candidate : candidates)
    {
        cv::Mat image = cv::imread(candidate, cv::IMREAD_COLOR);
        if (!image.empty())
            return candidate;
    }

    return "Artifacts/pattern.png";
}

std::string resolvePatternHexdumpPath()
{
    const char* envHexdumpPath = std::getenv("AR_PATTERN_HEXDUMP");
    if (envHexdumpPath && *envHexdumpPath)
        return envHexdumpPath;

    return "/home/shreeya607/seniordesign/AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern_hex.txt";
}

std::string resolvePatternHexdumpBeforePath()
{
    const char* envBeforePath = std::getenv("AR_PATTERN_HEX_BEFORE");
    if (envBeforePath && *envBeforePath)
        return envBeforePath;

    return "/home/shreeya607/seniordesign/AR_Application_Software/MarkerlessAR_V2/Artifacts/pattern_before_hex.txt";
}

std::string resolvePatternHexdumpAfterPath()
{
    const char* envAfterPath = std::getenv("AR_PATTERN_HEX_AFTER");
    if (envAfterPath && *envAfterPath)
        return envAfterPath;

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
}

void processVideo(const cv::Mat& patternImage, CameraCalibration& calibration, cv::VideoCapture& capture);
void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image);
bool processFrame(const cv::Mat& cameraFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx);
static void configureImageOverlay(ARDrawingContext& drawingCtx);

int main(int argc, const char * argv[])
{
    if (argc >= 2 && std::string(argv[1]) == "--write-pattern-hexdump")
    {
        const std::string patternPath = resolvePatternImagePath();
        const std::string hexdumpBeforePath = resolvePatternHexdumpBeforePath();
        const std::string hexdumpAfterPath = resolvePatternHexdumpAfterPath();

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
        cv::cvtColor(patternImage, grayPattern, cv::COLOR_BGR2GRAY);

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

    // Change this calibration to yours:
    CameraCalibration calibration(526.58037684199849f, 524.65577209994706f, 318.41744018680112f, 202.96659047014398f);

    if (argc < 2)
    {
        std::cout << "Input image not specified" << std::endl;
        std::cout << "Usage: ARProject.out <pattern image> [filepath to recorded video or image]" << std::endl;
        std::cout << "       ARProject.out --write-pattern-hexdump" << std::endl;
        return 1;
    }

    cv::Mat patternImage = cv::imread(argv[1]);
    if (patternImage.empty())
    {
        std::cout << "Input image cannot be read" << std::endl;
        return 2;
    }

    if (argc == 2)
    {
        cv::VideoCapture cap;
        cap.open(CAMERA_INDEX, cv::CAP_V4L2);
        if (!cap.isOpened())
        {
            std::cerr << "Failed to open camera" << std::endl;
            return 1;
        }

        cap.set(cv::CAP_PROP_FRAME_WIDTH, CAM_WIDTH);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, CAM_HEIGHT);
        cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        cap.set(cv::CAP_PROP_FPS, 30);

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
                processVideo(patternImage, calibration, cap);
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
    cv::Mat currentFrame;
    capture >> currentFrame;

    if (currentFrame.empty())
    {
        std::cout << "Cannot open video capture device" << std::endl;
        return;
    }

    cv::Size frameSize(currentFrame.cols, currentFrame.rows);

    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration, !isHeadlessRequested());
    MjpegHttpStreamer streamer;
    if (streamer.enabled() && !streamer.start())
        return;

    configureImageOverlay(drawingCtx);

    using Clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::milliseconds(1000 / getTargetFps());
    auto nextFrameDeadline = Clock::now();

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
        if (streamer.enabled())
            streamer.publishFrame(drawingCtx.getLastRenderedFrame());

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

void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image)
{
    cv::Size frameSize(image.cols, image.rows);
    ARPipeline pipeline(patternImage, calibration);
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration, !isHeadlessRequested());
    MjpegHttpStreamer streamer;
    if (streamer.enabled() && !streamer.start())
        return;

    configureImageOverlay(drawingCtx);

    using Clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::milliseconds(1000 / getTargetFps());
    auto nextFrameDeadline = Clock::now();

    bool shouldQuit = false;
    do
    {
        shouldQuit = processFrame(image, pipeline, drawingCtx);
        if (streamer.enabled())
            streamer.publishFrame(drawingCtx.getLastRenderedFrame());

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

bool processFrame(const cv::Mat& cameraFrame, ARPipeline& pipeline, ARDrawingContext& drawingCtx)
{
    cv::Mat img = cameraFrame.clone();

    cv::putText(img,
                "RANSAC threshold: " + ToString(pipeline.m_patternDetector.homographyReprojectionThreshold) + "( Use'-'/'+' to adjust)",
                cv::Point(10, 30),
                cv::FONT_HERSHEY_PLAIN,
                1,
                CV_RGB(0, 200, 0));

    drawingCtx.isPatternPresent = pipeline.processFrame(cameraFrame);
    drawingCtx.patternPose = pipeline.getPatternLocation();

    if (drawingCtx.isPatternPresent)
        drawingCtx.setPatternOverlayState(true, pipeline.getPatternInfo().points2d);
    else
        drawingCtx.setPatternOverlayState(false, std::vector<cv::Point2f>());

    drawingCtx.updateBackground(img);
    drawingCtx.updateWindow();

    int keyCode = -1;
    if (drawingCtx.isDisplayEnabled())
        keyCode = cv::waitKey(5);

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
    const char* overlayPath = std::getenv("AR_OVERLAY_IMAGE");
    std::string resolvedPath = overlayPath
        ? overlayPath
        : "/home/unc-design/augmented-reality-glasses/AR_Application_Software/MarkerlessAR_V2/Artifacts/overlay.png";

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
