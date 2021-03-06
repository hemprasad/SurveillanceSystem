#include <time.h>
#include <opencv2/opencv.hpp>
#include <pthread.h>
#include <vector>

#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Options.hpp>

#include <stdio.h>
#include <stdlib.h>

#include "video_frame.h"
#include "BgdCapturerAverage.h"
#include "MotionLocBlobThresh.h"
#include "IPCamProcessor.h"
#include "cvblob.h"

// Height and width of frame in pixels
const static int FRAME_HEIGHT = 240;
const static int FRAME_WIDTH = 352;

// Length of video frame buffer
const static int FRAME_BUFLEN = 100;

// Number of frames per background
const static int FRAMES_PER_BGD = 20;

// The index to which the current frame is being written
static int cur_frame_i = 0;
static std::vector<VideoFrame_t> video_frame_buffer(sizeof(VideoFrame_t));

// Background capture thread
// Will have access to data in video_frame_buffer 
void* capture_background(void* arg) {
	BgdCapturerAverage* bgdCapturerAverage =
		(BgdCapturerAverage*) arg;
    if(!bgdCapturerAverage->runInThread()) {
		perror("Error capturing background");
		return NULL;
	}
	return NULL;	
}

// IP Camera capture and annotation thread
// Will have access to data in video_frame_buffer 
void* run_ip_cam(void* arg) {
	IPCamProcessor* ipCamProcessor =
		(IPCamProcessor*) arg;
    if(!ipCamProcessor->runInThread()) {
		perror("Error running ip cam capture");
		return NULL;
	}
	return NULL;	
}

// Motion location thread
// Will have access to data in video_frame_buffer
// Thread is responsible for r/w locking on data it wants to read/modify
void* locate_motion(void* arg) {
	MotionLocBlobThresh* motionLocBlobThresh =
		(MotionLocBlobThresh*) arg;
	if(!motionLocBlobThresh->runInThread()) {
		perror("Error locating motion");
		return NULL;
	}
	return NULL;	
}

int main(int argc, char** argv) {
    // Capture default webcam feed
    cv::VideoCapture video_cap(0);
	// Initialize frame width and frame height for frame capture
    if(!(video_cap.set(CV_CAP_PROP_FRAME_WIDTH, FRAME_WIDTH) &
				video_cap.set(CV_CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT))) {
		perror("Can not set frame width and height");
	}
    
    const std::string ipStreamAddress = "http://192.168.2.30/video.mjpg";
    // Capture default webcam feed
    cv::VideoCapture video_cap_ip;
    if(!video_cap_ip.open(ipStreamAddress)) {
        std::cout << "error opening ip video stream" << std::endl;
        return -1;
    }
    
    
    std::cout << "after opening video stream" << std::endl;

   // Return code for initializing rwlocks 
   int rc = 0; 
   // Initialize frame buffer 
    for(int i = 0; i < FRAME_BUFLEN; i++) {
        // Initialize frame to empty frame of correct size
        video_frame_buffer[i].frame = 
            cv::Mat(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC1, cv::Scalar(0));
        
        // Initialize frame to empty frame of correct size
        video_frame_buffer[i].ip_frame = 
            cv::Mat(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC1, cv::Scalar(0));
        
        // Empty timestamp
        video_frame_buffer[i].timestamp = time_t();
        
        // Malloc rwlock to be associated with all data in this frame
        // and initialize rwlock
        pthread_rwlock_t* rw_lock = 
            (pthread_rwlock_t*) malloc(sizeof(pthread_rwlock_t));
        // TODO: check attr for initialization
        if( (rc = pthread_rwlock_init(rw_lock, NULL)) != 0) {
            perror("rwlock initialization failed.");
        }
        video_frame_buffer[i].rw_lock = rw_lock; 

        // Set variable for signaling other threads to exit when
        // main thread wants to exit
        // TODO: look into pthread condition variables
        video_frame_buffer[i].exit_thread = false;
    } 
    
    // Intialize background capturing option
    BgdCapturerAverage bgdCapturerAverage(&video_frame_buffer,
            FRAME_BUFLEN, 
            FRAME_WIDTH, 
            FRAME_HEIGHT, 
            FRAMES_PER_BGD);
	
    // Start thread for capturing background
	pthread_t background_capture_thread;
	if(pthread_create(&background_capture_thread, 
				NULL, 
				&capture_background, 
				&bgdCapturerAverage)) {
		perror("Could not create thread to capture background.");
		return  -1;
	}
    
    
    /* 
       cv::VideoWriter output_video;
       const std::string filename = "output.avi";
       int ex = CV_FOURCC('M', 'J', 'P', 'G'); // MPEG-1 codec
       output_video.open(filename, ex, 30, cv::Size(FRAME_WIDTH,
       FRAME_HEIGHT), true);
       */

    // Intialize background capturing option
	MotionLocBlobThresh motionLocBlobThresh(&video_frame_buffer,
           FRAME_BUFLEN, FRAME_WIDTH, FRAME_HEIGHT);
	
    // Start thread for capturing background
	pthread_t motion_location_thread;
	if(pthread_create(&motion_location_thread,
				NULL, 
				&locate_motion,
				&motionLocBlobThresh)) {
		perror("Could not create thread to locate motion.");
		return  -1;
	}
    
    // Intialize IP Cam capturing class
    IPCamProcessor ipCamProcessor(&video_frame_buffer,
            FRAME_BUFLEN,
            FRAME_WIDTH, 
            FRAME_HEIGHT,
            &motionLocBlobThresh);

    // Start thread for capturing background
    pthread_t ip_cam_thread;
    if(pthread_create(&ip_cam_thread,
                NULL, 
                &run_ip_cam,
                &ipCamProcessor)) {
        perror("Could not create thread to capture ip cam feed.");
        return  -1;
    } 
	
    // Display live video feed window
	cv::namedWindow("livefeed", 1);	
	// cv::namedWindow("livecolor", 1);	
    
    cURLpp::Cleanup myCleanup;

    // Stream video
    for(;;) {
        // TODO: double check ordering of this and the lock. Do I want this after the lock?
        const VideoFrame& this_video_frame = video_frame_buffer[cur_frame_i];
      
        // Acquire write lock on this frame
        if( (rc = pthread_rwlock_wrlock(this_video_frame.rw_lock)) != 0) {
            perror ("Failed to acquire write lock on next video frame.");
        }
       
        
        video_cap.grab();
        video_cap_ip.grab();
        video_cap_ip.grab();
        video_cap_ip.grab();
        video_cap_ip.grab();
        video_cap.retrieve(
                video_frame_buffer[cur_frame_i].color_frame); 
   
        video_cap_ip.retrieve(video_frame_buffer[cur_frame_i].color_ip_frame);
        //if (!video_cap_ip.read(fromIP)) {
        //   std::cout << "no frame" << std::endl;
        //    cv:::waitKey();
        //}
        
        //cv::resize(fromIP, 
        //        fromIP,
        //        cv::Size(FRAME_WIDTH, FRAME_HEIGHT));

        cv::Mat color_frame;
        (video_frame_buffer[cur_frame_i].color_frame).copyTo(color_frame);
        cvtColor(video_frame_buffer[cur_frame_i].color_frame, 
                video_frame_buffer[cur_frame_i].frame, CV_BGR2GRAY);

        time(&video_frame_buffer[cur_frame_i].timestamp);
        cv::Mat toDraw;
        (video_frame_buffer[cur_frame_i].frame).copyTo(toDraw);
        
        cv::Mat prob_mask;
        motionLocBlobThresh.getLastProbMask(&prob_mask);
        
        cv::Mat bgd;
        bgdCapturerAverage.getBgd(&bgd);
        hconcat(toDraw,
                prob_mask,
                toDraw);
        hconcat(toDraw,
                bgd,
                toDraw);

        motionLocBlobThresh.annotateMatWithBlobs(&color_frame);
        cv::Mat y_frame_with_blobs;
        cvtColor(color_frame, y_frame_with_blobs, CV_BGR2GRAY);
        //hconcat(toDraw,
        //        y_frame_with_blobs,
        //        toDraw);
        // std::cout << "IP: " << fromIP.size() << std::endl;
        // std::cout << "prob mask: " << prob_mask.size() << std::endl;
        cvtColor(video_frame_buffer[cur_frame_i].color_ip_frame, 
                video_frame_buffer[cur_frame_i].ip_frame, 
                CV_BGR2GRAY);
        // hconcat(toDraw, video_frame_buffer[cur_frame_i].ip_frame, toDraw);

        cv::Mat annotated_features;
        ipCamProcessor.getLastPair(&annotated_features);
        hconcat(toDraw, annotated_features, toDraw);

        cv::imshow("livefeed", toDraw);
        //output_video.write(color_frame);
        // cv::imshow("livecolor", color_frame); 


        // Release write lock on this frame
        if( (rc = pthread_rwlock_unlock(this_video_frame.rw_lock)) != 0) {
            perror ("Failed to release write lock on next video frame.");
        }

        int prev_frame_i = cur_frame_i;
        cur_frame_i = (cur_frame_i + 1) % FRAME_BUFLEN;

        int key = cv::waitKey(30);
        if( (key == 66) | (key == 98)) { // B or b
            if ( (rc = pthread_rwlock_rdlock(
                            video_frame_buffer[prev_frame_i].rw_lock))
                    != 0) {
            perror ("Failed to acquire read lock on video frame to capture as bgd.");
            }

            std::cout << "setting bgd" << std::endl;

            bgdCapturerAverage.
                setBgd(video_frame_buffer[prev_frame_i].frame);

            motionLocBlobThresh.
                setBgd(video_frame_buffer[prev_frame_i].frame);
        
            if ( (rc = pthread_rwlock_unlock(
                            video_frame_buffer[prev_frame_i].rw_lock))
                    != 0) {
            perror ("Failed to release read lock on video frame to capture as bgd.");
            }
        } else if (key >= 0) {
            if( (rc = pthread_rwlock_wrlock(
                            video_frame_buffer[prev_frame_i].rw_lock)) != 0) {
                perror ("Failed to acquire write lock on prev video frame.");
            }
            if( (rc = pthread_rwlock_wrlock(
                            video_frame_buffer[cur_frame_i].rw_lock)) != 0) {
                perror ("Failed to acquire write lock on cur video frame.");
            }
            
            // TODO: check if these both refer to same frame
            video_frame_buffer[prev_frame_i].exit_thread = true;
            video_frame_buffer[cur_frame_i].exit_thread = true; 
            
            if( (rc = pthread_rwlock_unlock(
                            video_frame_buffer[prev_frame_i].rw_lock)) != 0) {
                perror ("Failed to acquire write lock on prev video frame.");
            }
            if( (rc = pthread_rwlock_unlock(
                            video_frame_buffer[cur_frame_i].rw_lock)) != 0) {
                perror ("Failed to acquire write lock on cur video frame.");
            }
            break; 
        } 
    } 
    
    if ( (rc = pthread_join(background_capture_thread, NULL)) != 0) {
        perror("Background capture thread did not join.");
    }

    if ( (rc = pthread_join(motion_location_thread, NULL)) != 0) {
        perror("Motion location thread did not join.");
    }
 
   // TODO: notify background capture thread that it should end
   // TODO: add join for background capture thread
   // TODO: memory cleanup for rwlocks 
    for(int i = 0; i < FRAME_BUFLEN; i++) {
        pthread_rwlock_destroy(video_frame_buffer[i].rw_lock);
        free(video_frame_buffer[i].rw_lock);
    }

    video_cap.release();
    cv::destroyWindow("livefeed");
    return 0;
}
