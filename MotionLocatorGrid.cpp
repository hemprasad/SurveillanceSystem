#include "MotionLocatorGrid.h"

#include <vector>
#include <opencv2/opencv.hpp>

bool MotionLocatorGrid::findMaxLocation(cv::Mat mask, int num_locations, 
        cv::Point* dst_loc, cv::Point* dst_loc2) {
    //std::vector<cv::Point> max_locations(num_locations, cv::Point(0, 0));
    
    cv::Mat thresholded(_frame_height, _frame_width, CV_32F, cv::Scalar(0));
    cv::threshold(mask, thresholded, 200, 256, 3); // 0 = binary, 3 - thresh to zero
    cv::Mat mask_f;
    thresholded.convertTo(thresholded, CV_32F);
    divide(256, thresholded, thresholded);

    float x_center = 0;
    float y_center = 0;
    float x_total = 0;
    float y_total = 0;
    float total = 0;
    int count = 0;

    for(int r = 0; r < _frame_height; r++) {
        for(int c = 0; c < _frame_width; c++) {
            float val = thresholded.at<float>(r, c);
            if(thresholded.at<float>(r, c) > 0) {
                x_center += c;
                y_center += r;
                x_total += c*val;
                y_total += r*val;
                total += val;
                count++;
            }
        }
    }

    if(count != 0) {
        x_center = x_center / ((float) count);
        y_center = y_center / ((float) count);
        x_total = x_total / total;
        y_total = y_total / total;
    }

    dst_loc->x = x_center;
    dst_loc->y = y_center;

    dst_loc2->x = x_total;
    dst_loc2->y = y_total;

    return true;
}

bool MotionLocatorGrid::processFrame() {
    cv::Mat mask(_frame_height, _frame_width, CV_8UC1,
            cv::Scalar(0));
    cv::Mat bgd(_frame_height, _frame_width, CV_8UC1,
            cv::Scalar(0));
    getBgd(&bgd);
    VideoFrame_t& this_frame = (*_frame_buffer)[_cur_frame_i];
    
    // Calculate motion probabilities
    _motion_prob_y_diff.getMotionProbs(this_frame.frame, 
            bgd, 
            &mask);
   
    int rc = 0; 
    if( (rc = pthread_rwlock_wrlock(&_last_prob_mask_lock)) != 0) {
        perror("unable to lock on last prob mask.");
    }

    mask.copyTo(_last_prob_mask);

    if( (rc = pthread_rwlock_unlock(&_last_prob_mask_lock)) != 0) {
        perror("unable to unlock on last prob mask.");
    }


    cv::Point max_loc;
    cv::Point max_loc_weighted;
    findMaxLocation(mask, 1, &max_loc, &max_loc_weighted);

    if( (rc = pthread_rwlock_wrlock(&_motion_centers_lock)) != 0) {
        perror("unable to lock on motion centers.");
    }

    _motion_center.x = max_loc.x;
    _motion_center.y = max_loc.y;
    _motion_center_weighted.x = max_loc_weighted.x;
    _motion_center_weighted.y = max_loc_weighted.y;

    std::cout << "In calc: " << _motion_center.x << " " << _motion_center.y << std::endl;  
    if( (rc = pthread_rwlock_unlock(&_motion_centers_lock)) != 0) {
        perror("unable to unlock on motion centers.");
    }

    // Calculate highest diff point of motion probabilties
    return true;
}

cv::Point MotionLocatorGrid::getMotionCenters(int i) {
    int rc = 0;
    if( (rc = pthread_rwlock_rdlock(&_motion_centers_lock)) != 0) {
        perror("unable to lock on motion centers.");
    }

    cv::Point output;
    if (i == 0) {
        output = cv::Point(_motion_center.x, _motion_center.y);
    } else if (i == 1) {
        output = cv::Point(_motion_center_weighted.x, _motion_center_weighted.y);
    } else {
        std::cout << "Unsupported point index for motion center." << std::endl;
    }

    if( (rc = pthread_rwlock_unlock(&_motion_centers_lock)) != 0) {
        perror("unable to unlock on motion centers.");
    }
    return output;
}

bool MotionLocatorGrid::getLastProbMask(cv::Mat* dst) {
    int rc = 0;
    if( (rc = pthread_rwlock_rdlock(&_last_prob_mask_lock)) != 0) {
        perror("unable to lock on prob mask.");
    }

    _last_prob_mask.copyTo(*dst);

    if( (rc = pthread_rwlock_unlock(&_last_prob_mask_lock)) != 0) {
        perror("unable to unlock on prob mask.");
    }
    return true;
}

   /* 
   // Playing around with histogram stuff 
    int histSize = 256; 
    float range[] = {0, 256};
    const float* histRange = {range};
    bool uniform = true;
    bool accumulate = false;
    int channels[] = {0};

    cv::Mat y_hist;
    cv::calcHist(&mask, 1, channels, cv::Mat(), 
            y_hist, 1, &histSize, &histRange,
            uniform, accumulate);
*/
    /*
    for (int i = histSize - 1; i >= 0; i--) {
        if (y_hist.at<float>(i) > 0) {
            std::cout << "i: " << i << " num: " << int(y_hist.at<float>(i)) << std::endl;
        }
    } */

    /*
    int cluster_count = 10;
    cv::Mat thresholded(_frame_height, _frame_width, CV_32F, cv::Scalar(0));
    cv::threshold(mask, thresholded, 120, 256, 3);
    cv::Mat mask_f;
    thresholded.convertTo(mask_f, CV_32F);
   
    int non_zero = countNonZero(mask_f); 
    cv::Mat labels(non_zero, 2, CV_8UC1);
    cv::Mat centers;
    std::cout << "nnz: " << countNonZero(mask_f) << std::endl;
    cv::Mat samples(non_zero, 2, CV_32FC1);

    float val;
    int count = 0;
    for(int r = 0; r < _frame_height; r++) {
        for(int c = 0; c < _frame_width; c++) {
            val = thresholded.at<float>(r, c);
            if (val > 0) {
               samples.at<float>(count, 0) = c;
               samples.at<float>(count, 1) = r;
               count = count + 1;
            }
        }
    }

    cv::TermCriteria termCriteria(cv::TermCriteria::COUNT, 15, 5);
    // TODO convert frame data to floating point
    int compactness = kmeans(samples, 
            cluster_count,  // cluster count
            labels, // labels,
            termCriteria, // term criteria,
            5, // attempts
            cv::KMEANS_PP_CENTERS, // flags
            centers);

    std::cout << "Compactness: " << int(compactness) << std::endl;

    std::vector<float> x_center(cluster_count, 0);
    std::vector<float> y_center(cluster_count, 0);
    std::vector<int> num_points(cluster_count, 0);
    std::vector<int> total_values(cluster_count, 0);
    int label;
    for(int r = 0; r < _frame_height; r++) {
        for(int c = 0; c < _frame_width; c++) {
            label = labels.at<int>(r, c);
            x_center[label] += c;
            y_center[label] += r;
            total_values[label] += samples.at<float>(r, c);
            num_points[label]++;
        }
    }

    float max_value = 0.0;
    int max_index = 0;
    for(int i = 0; i < cluster_count; i++) {
        x_center[i] = x_center[i] / num_points[i];
        y_center[i] = y_center[i] / num_points[i];

        if (total_values[i] > max_value) {
            max_value = total_values[i];
            max_index = i;
        }
    }

    std::cout << "Max Val: " << float(max_value) << "Index: " << int(max_index) 
        << "X: " << float(x_center[max_index]) << "Y: " << float(y_center[max_index])
        << std::endl;
*/
    /*
    double max_val; double min_val;
    cv::Point max_loc; cv::Point min_loc;

    cv::Mat ignore = cv::Mat(frame_height, frame_width, );
    for(int i = 0; i < num_locations; i++) {
        cv::minMaxLoc(mask, 
                &min_val, 
                &max_val,
                &min_loc, 
                &max_loc,
                ignore); 
        
        max_locations[i].x = max_loc.x;
        max_locations[i].y = max_loc.y;

        std::cout << max_locations[i] << ": " 
            << int(mask.at<unsigned char>(max_locations[i].x, max_locations[i].y))
            << std::endl;

        // TODO: figure out some way to not stomp on data
        ignore.at<int>(max_locations[i].x, 
                max_locations[i].y) = 1;
    }

    // std::cout << mask << std::endl;
*/
//    return max_locations;
