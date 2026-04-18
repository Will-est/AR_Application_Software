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

#ifndef ARDrawingContext_HPP
#define ARDrawingContext_HPP

// File includes:
#include "GeometryTypes.hpp"
#include "CameraCalibration.hpp"

// Standard includes:
#include <opencv2/opencv.hpp>
#include <vector>

void ARDrawingContextDrawCallback(void* param);

class ARDrawingContext
{
public:
  ARDrawingContext(std::string windowName, cv::Size frameSize, const CameraCalibration& c, bool enableWindow = true);
  ~ARDrawingContext();

  
  bool                isPatternPresent;
  Transformation      patternPose;


  //! Set the new frame for the background
  void updateBackground(const cv::Mat& frame);

  //! Compose the same 2D overlay (if enabled) onto a CPU BGR frame for streaming/recording.
  //! `backgroundBgr` must be a 3-channel BGR image. `outBgr` will be a cloned/composited 3-channel BGR.
  bool composeFrameForStreaming(const cv::Mat& backgroundBgr, cv::Mat& outBgr) const;

  //! Set/replace image overlay (supports 3-channel BGR or 4-channel BGRA).
  //! If no alpha channel is present, one will be synthesized.
  void setOverlayImage(const cv::Mat& overlayImage);

  //! Enable or disable overlay compositing.
  //! When disabled, only the camera background is rendered.
  void setOverlayEnabled(bool enabled);

  //! Update whether a pattern is present and where its 2D corners are in the frame.
  //! The corner order is expected to be pattern points [0..3] from the detector.
  void setPatternOverlayState(bool patternPresent, const std::vector<cv::Point2f>& patternQuad);

  void updateWindow();

private:
    friend void ARDrawingContextDrawCallback(void* param);
    //! Render entire scene in the OpenGl window
    void draw();

private:
  bool               m_windowEnabled;
  bool               m_isTextureInitialized;
  unsigned int       m_backgroundTextureId;
  CameraCalibration  m_calibration;
  cv::Mat            m_backgroundImage;
  //! Overlay source image stored as BGRA for alpha compositing.
  cv::Mat            m_overlayImage;
  //! Global switch for overlay compositing.
  bool               m_overlayEnabled;
  //! Per-frame pattern detection status consumed by the renderer.
  bool               m_overlayPatternPresent;
  //! Per-frame 2D image-space pattern corners used for homography warp.
  std::vector<cv::Point2f> m_overlayPatternQuad;
  std::string        m_windowName;
};

#endif
