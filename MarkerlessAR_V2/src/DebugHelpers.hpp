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

#ifndef DEBUG_HELPERS_HPP
#define DEBUG_HELPERS_HPP

#include <string>
#include <sstream>

// Does lexical cast of the input argument to string
template <typename T>
std::string ToString(const T& value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

namespace cv
{
    // This function used to show and save the image to the disk (used for during chapter writing).
    inline void showAndSave(std::string name, const cv::Mat& m)
    {
        cv::imshow(name, m);
        cv::imwrite(name + ".png", m);
        //cv::waitKey(25);
    }

    // Draw matches between two images
    inline cv::Mat getMatchesImage(cv::Mat query, cv::Mat pattern, const std::vector<cv::KeyPoint>& queryKp, const std::vector<cv::KeyPoint>& trainKp, std::vector<cv::DMatch> matches, int maxMatchesDrawn)
    {
        cv::Mat outImg;

        if (matches.size() > maxMatchesDrawn)
        {
            matches.resize(maxMatchesDrawn);
        }

        // Drop matches that reference out-of-range keypoints to avoid drawMatches assertions.
        if (!matches.empty())
        {
            std::vector<cv::DMatch> filtered;
            filtered.reserve(matches.size());
            for (size_t i = 0; i < matches.size(); ++i)
            {
                const cv::DMatch& m = matches[i];
                if (m.queryIdx >= 0 && m.trainIdx >= 0 &&
                    m.queryIdx < static_cast<int>(queryKp.size()) &&
                    m.trainIdx < static_cast<int>(trainKp.size()))
                {
                    filtered.push_back(m);
                }
            }
            matches.swap(filtered);
        }

        cv::drawMatches
            (
            query, 
            queryKp, 
            pattern, 
            trainKp,
            matches, 
            outImg, 
            cv::Scalar(0,200,0,255), 
            cv::Scalar::all(-1),
            std::vector<char>(), 
            cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS
            );

        return outImg;
    }
}

#endif
