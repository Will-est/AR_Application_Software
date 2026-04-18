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
#include "CollectDa.hpp"
#include "dma_driver.hpp"

// Standard includes:
#include <opencv2/opencv.hpp>
#include <cstdlib>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>
#include <semaphore.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
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
bool envTruthy(const char* value)
{
    if (!value)
        return false;
    while (*value && std::isspace(static_cast<unsigned char>(*value)))
        ++value;
    if (!*value)
        return false;

    if (std::strcmp(value, "1") == 0)
        return true;
    if (std::strcmp(value, "0") == 0)
        return false;

    std::string lowered(value);
    for (char& ch : lowered)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    return lowered == "true" || lowered == "yes" || lowered == "y" || lowered == "on";
}

bool shouldRunHeadless()
{
    // Force GUI/headless via env vars. Headless is also the safe default when no display is available.
    const bool forceGui = envTruthy(std::getenv("AR_GUI"));
    const bool forceHeadless = envTruthy(std::getenv("AR_HEADLESS"));
    const char* display = std::getenv("DISPLAY");
    const bool hasDisplay = display && *display;

    if (forceGui)
        return false;
    if (forceHeadless)
        return true;
    return !hasDisplay;
}

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

    for (size_t offset = 0; offset < byteCount; offset += DMA_TRANSFER_SIZE)
    {
        output << std::setfill('0') << std::setw(8) << std::hex << offset << "  ";

        for (size_t index = 0; index < DMA_TRANSFER_SIZE; ++index)
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
        for (size_t index = 0; index < DMA_TRANSFER_SIZE && offset + index < byteCount; ++index)
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

    for (size_t offset = 0; offset < byteCount; offset += DMA_TRANSFER_SIZE)
    {
        output << std::setfill('0') << std::setw(8) << std::hex << offset << "  ";

        for (size_t index = 0; index < DMA_TRANSFER_SIZE; ++index)
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
        for (size_t index = 0; index < DMA_TRANSFER_SIZE && offset + index < byteCount; ++index)
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

	class HttpMjpegServer
	{
	public:
	    HttpMjpegServer(int port, int jpegQuality, int encodeFps)
	        : m_port(port)
	        , m_jpegQuality(max(1, min(100, jpegQuality)))
	        , m_encodeFps(max(0, min(120, encodeFps)))
	        , m_running(true)
	        , m_listenFd(-1)
	    {
	        if (m_encodeFps > 0)
	            m_encodePeriod = std::chrono::microseconds(static_cast<int>(1000000 / max(1, m_encodeFps)));
	        m_acceptThread = std::thread([this]() { this->acceptLoop(); });
	    }

	    ~HttpMjpegServer()
	    {
	        stop();
	    }

	    void stop()
	    {
	        bool expected = true;
	        if (!m_running.compare_exchange_strong(expected, false))
	            return;

	        m_frameCv.notify_all();

	        if (m_listenFd >= 0)
	        {
	            ::shutdown(m_listenFd, SHUT_RDWR);
	            ::close(m_listenFd);
	            m_listenFd = -1;
	        }

	        if (m_acceptThread.joinable())
	            m_acceptThread.join();
	    }

	    void pushFrameBgr(const cv::Mat& frameBgr)
	    {
	        if (!m_running.load())
	            return;
	        if (frameBgr.empty())
	            return;

	        const auto now = std::chrono::steady_clock::now();
	        if (m_encodeFps > 0)
	        {
	            if (now < m_nextEncodeDeadline)
	                return;
	            m_nextEncodeDeadline = now + m_encodePeriod;
	        }

	        cv::Mat bgr;
	        if (frameBgr.type() == CV_8UC3)
	        {
	            bgr = frameBgr;
	        }
	        else
	        {
	            if (frameBgr.channels() == 1)
	                cv::cvtColor(frameBgr, bgr, cv::COLOR_GRAY2BGR);
	            else if (frameBgr.channels() == 4)
	                cv::cvtColor(frameBgr, bgr, cv::COLOR_BGRA2BGR);
	            else
	                frameBgr.convertTo(bgr, CV_8U);
	        }

	        std::vector<uchar> encoded;
	        std::vector<int> params;
	        params.push_back(cv::IMWRITE_JPEG_QUALITY);
	        params.push_back(m_jpegQuality);
	        if (!cv::imencode(".jpg", bgr, encoded, params))
	            return;

	        {
	            std::lock_guard<std::mutex> lock(m_mutex);
	            m_latestJpeg.swap(encoded);
	            ++m_frameSeq;
	        }
	        m_frameCv.notify_all();
	    }

	private:
	    static bool sendAll(int fd, const void* data, size_t size)
	    {
	        const char* p = static_cast<const char*>(data);
	        size_t remaining = size;
	        while (remaining > 0)
	        {
	            ssize_t rc = ::send(fd, p, remaining, MSG_NOSIGNAL);
	            if (rc <= 0)
	                return false;
	            p += static_cast<size_t>(rc);
	            remaining -= static_cast<size_t>(rc);
	        }
	        return true;
	    }

	    static bool sendAll(int fd, const std::string& s)
	    {
	        return sendAll(fd, s.data(), s.size());
	    }

	    void acceptLoop()
	    {
	        m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
	        if (m_listenFd < 0)
	        {
	            std::perror("[MJPEG] socket");
	            return;
	        }

	        int yes = 1;
	        (void)::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	        sockaddr_in addr{};
	        addr.sin_family = AF_INET;
	        addr.sin_port = htons(static_cast<uint16_t>(m_port));
	        addr.sin_addr.s_addr = htonl(INADDR_ANY);

	        if (::bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
	        {
	            std::perror("[MJPEG] bind");
	            return;
	        }

	        if (::listen(m_listenFd, 8) != 0)
	        {
	            std::perror("[MJPEG] listen");
	            return;
	        }

	        std::cerr << "[MJPEG] listening on 0.0.0.0:" << m_port
	                  << " (open http://<device-ip>:" << m_port << "/ on your laptop)" << std::endl;

	        pollfd pfd{};
	        pfd.fd = m_listenFd;
	        pfd.events = POLLIN;

	        while (m_running.load())
	        {
	            int prc = ::poll(&pfd, 1, 500);
	            if (!m_running.load())
	                break;
	            if (prc <= 0)
	                continue;
	            if (!(pfd.revents & POLLIN))
	                continue;

	            sockaddr_in clientAddr{};
	            socklen_t clientLen = sizeof(clientAddr);
	            int clientFd = ::accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
	            if (clientFd < 0)
	                continue;

	            std::thread([this, clientFd]() { this->handleClient(clientFd); }).detach();
	        }
	    }

	    void handleClient(int clientFd)
	    {
	        auto closeFd = [clientFd]()
	        {
	            ::shutdown(clientFd, SHUT_RDWR);
	            ::close(clientFd);
	        };

	        char buf[4096];
	        ssize_t n = ::recv(clientFd, buf, sizeof(buf) - 1, 0);
	        if (n <= 0)
	        {
	            closeFd();
	            return;
	        }
	        buf[n] = '\0';
	        std::string req(buf);

	        std::string path = "/";
	        size_t lineEnd = req.find("\r\n");
	        std::string firstLine = (lineEnd == std::string::npos) ? req : req.substr(0, lineEnd);
	        if (firstLine.rfind("GET ", 0) == 0)
	        {
	            size_t pathStart = 4;
	            size_t pathEnd = firstLine.find(' ', pathStart);
	            if (pathEnd != std::string::npos && pathEnd > pathStart)
	                path = firstLine.substr(pathStart, pathEnd - pathStart);
	        }

	        if (path == "/stream" || path == "/stream.mjpg" || path == "/stream.mjpeg")
	        {
	            if (!sendAll(clientFd,
	                    "HTTP/1.0 200 OK\r\n"
	                    "Server: AR-MJPEG\r\n"
	                    "Cache-Control: no-cache\r\n"
	                    "Pragma: no-cache\r\n"
	                    "Connection: close\r\n"
	                    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n"))
	            {
	                closeFd();
	                return;
	            }

	            std::uint64_t lastSeq = 0;
	            while (m_running.load())
	            {
	                std::vector<uchar> jpeg;
	                std::uint64_t seq = 0;
	                {
	                    std::unique_lock<std::mutex> lock(m_mutex);
	                    m_frameCv.wait_for(lock, std::chrono::milliseconds(200), [&]()
	                    {
	                        return !m_running.load() || (!m_latestJpeg.empty() && m_frameSeq != lastSeq);
	                    });
	                    if (!m_running.load())
	                        break;
	                    if (m_latestJpeg.empty() || m_frameSeq == lastSeq)
	                        continue;
	                    jpeg = m_latestJpeg;
	                    seq = m_frameSeq;
	                }

	                std::string header = "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
	                                     std::to_string(jpeg.size()) + "\r\n\r\n";
	                if (!sendAll(clientFd, header) ||
	                    !sendAll(clientFd, jpeg.data(), jpeg.size()) ||
	                    !sendAll(clientFd, "\r\n"))
	                {
	                    break;
	                }
	                lastSeq = seq;
	            }

	            closeFd();
	            return;
	        }

	        const char* body =
	            "<!doctype html><html><head><meta charset='utf-8'/>"
	            "<title>AR MJPEG</title>"
	            "<style>body{font-family:sans-serif;margin:16px}img{max-width:100%;height:auto}</style>"
	            "</head><body>"
	            "<h3>AR MJPEG Stream</h3>"
	            "<p><a href='/stream.mjpg'>Open raw stream</a></p>"
	            "<img src='/stream.mjpg' alt='stream'/>"
	            "</body></html>";

	        std::string resp = "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: " +
	                           std::to_string(std::strlen(body)) + "\r\n\r\n" + body;
	        (void)sendAll(clientFd, resp);
	        closeFd();
	    }

	private:
	    int m_port;
	    int m_jpegQuality;
	    int m_encodeFps;
	    std::chrono::microseconds m_encodePeriod{0};
	    std::chrono::steady_clock::time_point m_nextEncodeDeadline{};

	    std::atomic<bool> m_running;
	    int m_listenFd;
	    std::thread m_acceptThread;

	    std::mutex m_mutex;
	    std::condition_variable m_frameCv;
	    std::vector<uchar> m_latestJpeg;
	    std::uint64_t m_frameSeq{0};
	};
	}

	#define ACCEL_BASECTRL_ADDR              0x00 
	static void log_breath(const char* tag)
	{
    fprintf(stderr,
        "[BREATH %s] in=%3u  out=%3u  bgr_fifo=%3u  pad_fifo=%3u  gray_fifo=%3u\n Accelerator Done: %3u",
        tag,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_IN_BREATH)        & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_OUT_BREATH)       & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_BGR_FIFO_BREATH)  & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_PAD_FIFO_BREATH)  & 0xFF,
        read_dma(accel_virtual_addr, ACCEL_CTRL_ADDR_GRAY_FIFO_BREATH) & 0xFF,
        ((read_dma(accel_virtual_addr, ACCEL_BASECTRL_ADDR) & 0x02) >> 1));
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
bool processFrame(const cv::Mat& displayFrame,
                  const cv::Mat& processedFrame,
                  ARPipeline& pipeline,
                  ARDrawingContext& drawingCtx,
                  HttpMjpegServer* mjpegServer,
                  bool headless);

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
    accel_virtual_addr[0] |= (0x81);

#if COLLECTDA
    struct CollectDaGuard
    {
        ~CollectDaGuard() { collectda::shutdown(); }
    } collectDaGuard;
    collectda::init();
#endif

    cv::Size frameSize(CAM_WIDTH, CAM_HEIGHT);

    ARPipeline pipeline(patternImage, calibration);
    const bool headless = shouldRunHeadless();
    if (headless)
        std::cout << "[HEADLESS] DISPLAY not set (or AR_HEADLESS=1); GUI window disabled. Use AR_MJPEG_PORT to view over HTTP." << std::endl;
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration, !headless);
    configureImageOverlay(drawingCtx);

    std::unique_ptr<HttpMjpegServer> mjpegServer;
    const int mjpegPort = getEnvInt("AR_MJPEG_PORT", 0);
    if (mjpegPort > 0)
        mjpegServer.reset(new HttpMjpegServer(mjpegPort,
                                             getEnvInt("AR_MJPEG_QUALITY", 80),
                                             getEnvInt("AR_MJPEG_FPS", 15)));

    using Clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::milliseconds(1000 / getTargetFps());
    auto nextFrameDeadline = Clock::now();
    const auto monoUsNow = []() -> std::uint64_t
    {
        const auto now = Clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count());
    };

    std::atomic<bool> dmaRunning{true};
    std::mutex txMutex;
    std::condition_variable txCv;
    cv::Mat pendingTxFrame;
    bool hasPendingTx = false;

    std::mutex rxMutex;
    cv::Mat latestProcessedFrame;
    bool hasProcessedFrame = false;

    // Ping-pong DMA:
    // - TX sends `warmupRows` first, then RX is allowed to start.
    // - After warmup: RX receives 1 row, then TX sends 1 row (pipeline depth = warmupRows).
    // - TX exits the per-frame loop once it has sent all rows; RX continues until the full frame is received.
    const int warmupRows = max(0, min(CAM_HEIGHT, getEnvInt("AR_DMA_PINGPONG_WARMUP_ROWS", 6)));

    auto sem_wait_intr = [](sem_t* sem)
    {
        while (sem_wait(sem) == -1 && errno == EINTR)
        {
        }
    };

    auto sem_drain = [](sem_t* sem)
    {
        while (sem_trywait(sem) == 0)
        {
        }
    };

    sem_t frameBeginSem{};
    sem_t warmupDoneSem{};
    sem_t rxTurnSem{};
    sem_t txTurnSem{};
    sem_t frameDoneSem{};

    if (sem_init(&frameBeginSem, 0, 0u) != 0 ||
        sem_init(&warmupDoneSem, 0, 0u) != 0 ||
        sem_init(&rxTurnSem, 0, 0u) != 0 ||
        sem_init(&txTurnSem, 0, 0u) != 0 ||
        sem_init(&frameDoneSem, 0, 0u) != 0)
    {
        std::perror("[DMA] sem_init(pingpong) failed");
        return;
    }

    std::atomic<unsigned long long> pingpongFrameSeq{0};
    std::atomic<unsigned long long> activeFrameId{0};
    std::atomic<int> lastPingpongRxRc{0};
#if COLLECTDA
    std::atomic<std::uint64_t> lastPingpongDmaStartUs{0};
#endif

    const auto send_row_bgr = [&](const cv::Mat& frame, int rowIndex) -> bool
    {
        if (!dma_virtual_addr || !virtual_src_addr)
            return false;
        if (rowIndex < 0 || rowIndex >= frame.rows)
            return false;

        constexpr int headerBytes = DMA_TRANSFER_SIZE;
        constexpr int payloadBytes = CAM_WIDTH * 3;
        constexpr int transferBytes = headerBytes + payloadBytes; // 1936
        static_assert((payloadBytes % DMA_TRANSFER_SIZE) == 0, "payload must be 16B aligned");
        static_assert((transferBytes % DMA_TRANSFER_SIZE) == 0, "transfer size must be 16B aligned");

        std::uint8_t* src = reinterpret_cast<std::uint8_t*>(virtual_src_addr);
        std::memset(src, 0, headerBytes);
        src[0] = static_cast<std::uint8_t>(rowIndex & 0xFF);
        src[1] = static_cast<std::uint8_t>((rowIndex >> 8) & 0xFF);

        std::uint8_t* payload = src + headerBytes;
        const std::uint8_t* rowBytes = frame.ptr<std::uint8_t>(rowIndex);
        std::memcpy(payload, rowBytes, static_cast<size_t>(payloadBytes));

        auto startMm2sTransfer = [&]()
        {
            write_dma(dma_virtual_addr, MM2S_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
            write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
            write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
            write_dma(dma_virtual_addr, MM2S_TRNSFR_LENGTH_REGISTER, transferBytes);
            return dma_mm2s_sync(dma_virtual_addr);
        };

        int rc = startMm2sTransfer();
        if (rc != 0)
        {
            std::cerr << "[DMA] tx: row " << rowIndex << " failed rc=" << rc << " (retrying once)" << std::endl;

            write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
            write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);

            rc = startMm2sTransfer();
            if (rc != 0)
            {
                std::cerr << "[DMA] tx: row " << rowIndex << " retry failed rc=" << rc << std::endl;
                log_breath("MM2S-ROW-RETRY-FAIL");
                return false;
            }
        }

        return true;
    };

    const auto receive_row_gray = [&](cv::Mat& grayFrame, int rowIndex) -> int
    {
        if (!dma_virtual_addr || !virtual_dst_addr)
            return ENODEV;

        constexpr int headerBytes = DMA_TRANSFER_SIZE;
        constexpr int payloadBytes = CAM_WIDTH;
        constexpr int rowBytes = headerBytes + payloadBytes; // 656
        static_assert((rowBytes % DMA_TRANSFER_SIZE) == 0, "row transfer must be 16B aligned");

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
            std::cerr << "[DMA] rx: S2MM halted before row " << rowIndex << ", resetting" << std::endl;
            resetAndRunS2mm();
        }

        int rc = startS2mmTransfer();
        if (rc != 0)
        {
            std::cerr << "[DMA] rx: row " << rowIndex << " failed rc=" << rc << " (retrying once)" << std::endl;
            log_breath("S2MM-ROW-FAIL");
            resetAndRunS2mm();
            rc = startS2mmTransfer();
            if (rc != 0)
            {
                std::cerr << "[DMA] rx: row " << rowIndex << " retry failed rc=" << rc << std::endl;
                log_breath("S2MM-RETRY-FAIL");
                return rc;
            }
            std::cerr << "[DMA] rx: row " << rowIndex << " retry OK" << std::endl;
            log_breath("S2MM-RETRY-OK");
        }

        const uint16_t headerId16 = static_cast<uint16_t>(rx[0]) | (static_cast<uint16_t>(rx[1]) << 8);
        const uint8_t headerId8 = rx[0];
        const uint16_t expected16 = static_cast<uint16_t>(rowIndex);
        const uint8_t expected8 = static_cast<uint8_t>(rowIndex & 0xFF);
        if (!(headerId16 == expected16 || headerId8 == expected8))
        {
            std::cerr << "[DMA] warning: row header id=" << headerId16
                      << " (byte0=" << static_cast<int>(headerId8) << ") expected "
                      << expected16 << " at row " << rowIndex << std::endl;
            printf("[DMA] rx: row %d raw header (%d bytes): ", rowIndex, headerBytes);
            print_mem(rx, headerBytes);
            printf("[DMA] rx: row %d payload prefix (32 bytes): ", rowIndex);
            print_mem(rx + headerBytes, 32);
        }

        std::memcpy(grayFrame.ptr<std::uint8_t>(rowIndex), rx + headerBytes, payloadBytes);

        return 0;
    };

    std::thread rxThread([&]()
    {
        const int timeoutLimit = getEnvInt("AR_DMA_RX_TIMEOUT_LIMIT", 0);
        int consecutiveTimeouts = 0;
        while (dmaRunning.load())
        {
            sem_wait_intr(&frameBeginSem);
            if (!dmaRunning.load())
                break;

            sem_wait_intr(&warmupDoneSem);
            if (!dmaRunning.load())
                break;

            const unsigned long long frameId = activeFrameId.load();
            cv::Mat processed(CAM_HEIGHT, CAM_WIDTH, CV_8UC1);
            int frameRc = 0;
            for (int row = 0; row < CAM_HEIGHT && dmaRunning.load(); ++row)
            {
                if (row < (CAM_HEIGHT - warmupRows))
                    sem_wait_intr(&rxTurnSem);

                const int rc = receive_row_gray(processed, row);
                if (rc != 0)
                {
                    frameRc = rc;
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
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    else
                    {
                        std::cerr << "[RX] receive failed rc=" << rc << ", stopping DMA" << std::endl;
                        dmaRunning.store(false);
                    }
                    break;
                }

                consecutiveTimeouts = 0;
                if (row < (CAM_HEIGHT - warmupRows))
                    sem_post(&txTurnSem);
            }

            lastPingpongRxRc.store(frameRc);

#if COLLECTDA
            if (frameRc == 0)
            {
                const std::uint64_t startUs = lastPingpongDmaStartUs.load();
                const std::uint64_t endUs = monoUsNow();
                if (startUs != 0 && endUs >= startUs)
                    collectda::onPreprocessUs(endUs - startUs);
            }
#endif

            if (frameRc == 0 && dmaRunning.load())
            {
                std::lock_guard<std::mutex> lock(rxMutex);
                latestProcessedFrame = processed;
                hasProcessedFrame = true;
            }

            if (frameRc != 0)
            {
                std::cerr << "[PINGPONG] frame " << frameId << " RX failed rc=" << frameRc << std::endl;
            }

            sem_post(&frameDoneSem);
        }

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

            if (frameToSend.empty())
                continue;

            if (frameToSend.type() != CV_8UC3)
            {
                std::cerr << "[DMA] txThread: invalid frame (need CV_8UC3)" << std::endl;
                continue;
            }

            cv::Mat frame;
            if (frameToSend.cols != CAM_WIDTH || frameToSend.rows != CAM_HEIGHT)
                cv::resize(frameToSend, frame, cv::Size(CAM_WIDTH, CAM_HEIGHT));
            else
                frame = frameToSend;

            sem_drain(&warmupDoneSem);
            sem_drain(&rxTurnSem);
            sem_drain(&txTurnSem);
            sem_drain(&frameDoneSem);

            const unsigned long long frameId = pingpongFrameSeq.fetch_add(1) + 1;
            activeFrameId.store(frameId);
            lastPingpongRxRc.store(0);
#if COLLECTDA
            lastPingpongDmaStartUs.store(monoUsNow());
#endif
            sem_post(&frameBeginSem);

            bool ok = true;
            for (int row = 0; row < warmupRows; ++row)
            {
                if (!dmaRunning.load())
                {
                    ok = false;
                    break;
                }
                if (!send_row_bgr(frame, row))
                {
                    ok = false;
                    dmaRunning.store(false);
                    break;
                }
            }

            sem_post(&warmupDoneSem);
            if (!ok)
            {
                sem_post(&rxTurnSem);
                sem_post(&txTurnSem);
                sem_post(&frameDoneSem);
                break;
            }

            // RX runs first after warmup.
            sem_post(&rxTurnSem);

            for (int row = warmupRows; row < CAM_HEIGHT && dmaRunning.load(); ++row)
            {
                sem_wait_intr(&txTurnSem);
                if (!dmaRunning.load())
                {
                    ok = false;
                    break;
                }

                if (!send_row_bgr(frame, row))
                {
                    ok = false;
                    dmaRunning.store(false);
                    break;
                }

                sem_post(&rxTurnSem);
            }

            // Let RX drain remaining rows (last `warmupRows` rows) without blocking.
            sem_post(&rxTurnSem);

            sem_wait_intr(&frameDoneSem);
            const int rxRc = lastPingpongRxRc.load();
            if (!(ok && rxRc == 0))
            {
                std::cerr << "[PINGPONG] frame " << frameId << " complete with errors (txOk="
                          << (ok ? 1 : 0) << ", rxRc=" << rxRc << ")" << std::endl;
                log_breath("PINGPONG-FRAME-ERR");
            }
            if (!ok)
                break;
        }
    });

    bool shouldQuit = false;
    auto lastNoDmaLog = Clock::now() - std::chrono::seconds(10);
    do
    {
        capture >> currentFrame;
        if (currentFrame.empty())
        {
            shouldQuit = true;
            continue;
        }

#if COLLECTDA
        collectda::onFrameArrival();
#endif

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
                processedForDetection = latestProcessedFrame.clone();
        }

        if (processedForDetection.empty())
        {
            const auto now = Clock::now();
            if (now - lastNoDmaLog >= std::chrono::seconds(1))
            {
                std::cerr << "[DMA] no processed frame available yet (skipping detection)" << std::endl;
                lastNoDmaLog = now;
            }
        }

#if COLLECTDA
        const auto frameStart = Clock::now();
#endif
        shouldQuit = processFrame(displayFrame, processedForDetection, pipeline, drawingCtx, mjpegServer.get(), headless);
#if COLLECTDA
        const auto frameEnd = Clock::now();
        const auto frameUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart).count());
        collectda::onFrameProcessedUs(frameUs);
#endif
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
    sem_post(&frameBeginSem);
    sem_post(&warmupDoneSem);
    sem_post(&rxTurnSem);
    sem_post(&txTurnSem);
    sem_post(&frameDoneSem);
    txThread.join();
    rxThread.join();
    sem_destroy(&frameBeginSem);
    sem_destroy(&warmupDoneSem);
    sem_destroy(&rxTurnSem);
    sem_destroy(&txTurnSem);
    sem_destroy(&frameDoneSem);
}

void processSingleImage(const cv::Mat& patternImage, CameraCalibration& calibration, const cv::Mat& image)
{
    cv::Size frameSize(image.cols, image.rows);
    ARPipeline pipeline(patternImage, calibration);
    const bool headless = shouldRunHeadless();
    if (headless)
        std::cout << "[HEADLESS] DISPLAY not set (or AR_HEADLESS=1); GUI window disabled. Use AR_MJPEG_PORT to view over HTTP." << std::endl;
    ARDrawingContext drawingCtx("Markerless AR", frameSize, calibration, !headless);
    // Load optional overlay image once and keep it in rendering context.
    configureImageOverlay(drawingCtx);

    std::unique_ptr<HttpMjpegServer> mjpegServer;
    const int mjpegPort = getEnvInt("AR_MJPEG_PORT", 0);
    if (mjpegPort > 0)
        mjpegServer.reset(new HttpMjpegServer(mjpegPort,
                                             getEnvInt("AR_MJPEG_QUALITY", 80),
                                             getEnvInt("AR_MJPEG_FPS", 15)));

    using Clock = std::chrono::steady_clock;
    const auto framePeriod = std::chrono::milliseconds(1000 / getTargetFps());
    auto nextFrameDeadline = Clock::now();

    bool shouldQuit = false;
    do
    {
        shouldQuit = processFrame(image, image, pipeline, drawingCtx, mjpegServer.get(), headless);
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

bool processFrame(const cv::Mat& displayFrame,
                  const cv::Mat& processedFrame,
                  ARPipeline& pipeline,
                  ARDrawingContext& drawingCtx,
                  HttpMjpegServer* mjpegServer,
                  bool headless)
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
    if (processedFrame.empty())
    {
        drawingCtx.isPatternPresent = false;
    }
    else
    {
        drawingCtx.isPatternPresent = pipeline.processFrame(processedFrame);
    }
#if COLLECTDA
    collectda::onPatternFound(drawingCtx.isPatternPresent);
#endif

    // Update a pattern pose:
    if (drawingCtx.isPatternPresent)
        drawingCtx.patternPose = pipeline.getPatternLocation();

    // Update 2D pattern corners for pattern-locked image overlay.
    // This uses existing detector output and does not alter detection behavior.
    if (drawingCtx.isPatternPresent)
        drawingCtx.setPatternOverlayState(true, pipeline.getPatternInfo().points2d);
    else
        drawingCtx.setPatternOverlayState(false, std::vector<cv::Point2f>());

    // Set a new camera frame:
    drawingCtx.updateBackground(img);

    if (mjpegServer)
    {
        cv::Mat streamFrame;
        if (!drawingCtx.composeFrameForStreaming(img, streamFrame))
            streamFrame = img;
        mjpegServer->pushFrameBgr(streamFrame);
    }

    int keyCode = -1;
    if (!headless)
    {
        // Request redraw of the window:
        drawingCtx.updateWindow();

        // Read the keyboard input:
        keyCode = cv::waitKey(5);
    }

    bool shouldQuit = false;
    if (!headless && (keyCode == '+' || keyCode == '='))
    {
        pipeline.m_patternDetector.homographyReprojectionThreshold += 0.2f;
        pipeline.m_patternDetector.homographyReprojectionThreshold = min(10.0f, pipeline.m_patternDetector.homographyReprojectionThreshold);
    }
    else if (!headless && keyCode == '-')
    {
        pipeline.m_patternDetector.homographyReprojectionThreshold -= 0.2f;
        pipeline.m_patternDetector.homographyReprojectionThreshold = max(0.0f, pipeline.m_patternDetector.homographyReprojectionThreshold);
    }
    else if (!headless && keyCode == 'h')
    {
        pipeline.m_patternDetector.enableHomographyRefinement = !pipeline.m_patternDetector.enableHomographyRefinement;
    }
    else if (!headless && (keyCode == 27 || keyCode == 'q'))
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

// bool send_dma_frame(const cv::Mat& currentFrame)
// {
//     if (currentFrame.empty())
//     {
//         std::cerr << "[DMA] send_dma_frame: invalid input frame (empty)" << std::endl;
//         return false;
//     }

//     static std::atomic<unsigned long long> totalAttempts{0};
//     static std::atomic<unsigned long long> totalSendsOk{0};
//     static std::atomic<unsigned long long> consecutiveSendsOk{0};

//     if (!dma_virtual_addr || !virtual_src_addr)
//     {
//         std::cerr << "[DMA] send_dma_frame: DMA not initialized" << std::endl;
//         return false;
//     }

//     constexpr int payloadBytesPerBlock = 128;
//     constexpr int transferBytesPerBlock = payloadBytesPerBlock;

//     static_assert((payloadBytesPerBlock % 16) == 0, "payload must be 16B aligned");
//     static_assert((transferBytesPerBlock % 16) == 0, "transfer size must be 16B aligned");

//     std::uint8_t* src = reinterpret_cast<std::uint8_t*>(virtual_src_addr);

//     if (accel_virtual_addr)
//         accel_virtual_addr[0] = 1;

//     constexpr int blockCount = 1;
//     for (int blockIndex = 0; blockIndex < blockCount; ++blockIndex)
//     {
//         const unsigned long long attemptIndex = totalAttempts.fetch_add(1) + 1;
//         for (int i = 0; i < payloadBytesPerBlock; ++i)
//             src[i] = static_cast<std::uint8_t>((blockIndex + i) & 0xFF);

//         auto startMm2sTransfer = [&]()
//         {
//             write_dma(dma_virtual_addr, MM2S_STATUS_REGISTER, STATUS_IOC_IRQ | STATUS_DELAY_IRQ | STATUS_ERR_IRQ);
//             write_dma(dma_virtual_addr, MM2S_SRC_ADDRESS_REGISTER, SOURCE_ADDR);
//             write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);
//             write_dma(dma_virtual_addr, MM2S_TRNSFR_LENGTH_REGISTER, transferBytesPerBlock);
//             return dma_mm2s_sync(dma_virtual_addr);
//         };

//         int rc = startMm2sTransfer();
//         if (rc != 0)
//         {
//             const auto streak = consecutiveSendsOk.load();
//             const auto total = totalSendsOk.load();
//             std::cerr << "[DMA] send_dma_frame: dummy block " << blockIndex
//                       << " first-attempt failed rc=" << rc
//                       << " (attempt=" << attemptIndex
//                       << ", ok-streak=" << streak
//                       << ", ok-total=" << total
//                       << ", resetting streak + retrying once)" << std::endl;
//             consecutiveSendsOk.store(0);

//             write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RESET_DMA);
//             write_dma(dma_virtual_addr, MM2S_CONTROL_REGISTER, RUN_DMA | ENABLE_ALL_IRQ);

//             rc = startMm2sTransfer();
//             if (rc != 0)
//             {
//                 const auto streak = consecutiveSendsOk.load();
//                 const auto total = totalSendsOk.load();
//                 std::cerr << "[DMA] send_dma_frame: dummy block " << blockIndex
//                           << " retry failed rc=" << rc
//                           << " (attempt=" << attemptIndex
//                           << " (ok-streak=" << streak
//                           << ", ok-total=" << total << ")" << std::endl;
//                 log_breath("MM2S-DUMMY-RETRY-FAIL");
//                 return false;
//             }
//         }

//         const unsigned long long totalOk = totalSendsOk.fetch_add(1) + 1;
//         const unsigned long long streakOk = consecutiveSendsOk.fetch_add(1) + 1;
//         printf("[DMA] send_dma_frame: dummy block %d sent OK (%d bytes) (attempt=%llu ok-streak=%llu ok-total=%llu)\n",
//                blockIndex,
//                transferBytesPerBlock,
//                attemptIndex,
//                streakOk,
//                totalOk);
//     }

//     log_breath("TX-DUMMY-DONE");
//     std::cout << "[DMA] send_dma_frame: sent dummy payload successfully" << std::endl;
//     return true;
// }
int receive_dma_frame(cv::Mat& grayFrame);
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

    constexpr int blockRows = 1;
    constexpr int bytesPerPixel = 3;
    constexpr int headerBytes = DMA_TRANSFER_SIZE;
    constexpr int payloadBytesPerBlock = blockRows * CAM_WIDTH * bytesPerPixel; // 9600
    constexpr int transferBytesPerBlock = headerBytes + payloadBytesPerBlock;   // 9616

    static_assert((payloadBytesPerBlock % DMA_TRANSFER_SIZE) == 0, "1-row payload must be 16B aligned");
    static_assert((transferBytesPerBlock % DMA_TRANSFER_SIZE) == 0, "transfer size must be 16B aligned");
    static_assert((CAM_HEIGHT % blockRows) == 0, "CAM_HEIGHT must be divisible by 16");

    uint8_t* src = reinterpret_cast<uint8_t*>(virtual_src_addr);


    int blockIndex = 0;
    for (int startRow = 0; startRow < CAM_HEIGHT; startRow += blockRows, ++blockIndex)
    {
        if(startRow > 5){
            cv::Mat dummyFrame;
            receive_dma_frame(frame);
        }

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
            //accel_virtual_addr[0] |= 1;
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
        //accel_virtual_addr[0] &= ~(0x1);
    }

    return true;
}

int receive_dma_frame(cv::Mat& grayFrame)
{
    grayFrame.create(CAM_HEIGHT, CAM_WIDTH, CV_8UC1);

    constexpr int headerBytes = DMA_TRANSFER_SIZE;
    constexpr int payloadBytes = CAM_WIDTH;
    constexpr int rowBytes = headerBytes + payloadBytes; // 656
    static_assert((rowBytes % DMA_TRANSFER_SIZE) == 0, "row transfer must be 16B aligned");

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

    return 0;
}
