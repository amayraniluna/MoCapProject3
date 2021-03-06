/*
 
 Programmer: Amayrani Luna
 Date: c2020
 Notes: Demonstration of blob detection
 Purpose/Description:
 
 This program demonstrates simple blob detection.
 
 Uses:
 
 See brief tutorial here:
 https://www.learnopencv.com/blob-detection-using-opencv-python-c/

 Output/Drawing:
 Draws the result of simple blob detection.
 */

//#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

//includes for background subtraction
#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include <opencv2/video.hpp>
#include "opencv2/features2d.hpp" //new include for this project

#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Capture.h" //needed for capture
#include "cinder/Log.h" //needed to log errors

#include "Osc.h" //add to send OSC


#include "CinderOpenCV.h"

#include "Blob.h"

#define SAMPLE_WINDOW_MOD 300
#define MAX_FEATURES 300
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480

using namespace ci;
using namespace ci::app;
using namespace std;

//for networking
#define LOCALPORT 8887 //we just bind here to send.
#define DESTHOST "127.0.0.1" //this is sending to our OWN computer's IP address
#define DESTPORT 8888 //this is the port we are sending to

//set up our OSC addresses
#define DOWN_OSC_ADDRESS "/MakeItArt/Down"
#define WHERE_OSCADDRESS "/MakeItArt/Where"
#define BLOB_OSCADDRESS "/MakeItArt/Blobs"


class BlobTrackingApp : public App {
public:
    BlobTrackingApp();
    void setup() override;
//    void mouseDown( MouseEvent event ) override;
    void keyDown( KeyEvent event ) override;
    
    void update() override;
    void draw() override;
    
protected:
    CaptureRef                 mCapture; //the camera capture object
    gl::TextureRef             mTexture; //current camera frame in opengl format
    
    SurfaceRef                  mSurface; //current camera frame in cinder format
    
    cv::Ptr<cv::SimpleBlobDetector>     mBlobDetector; //the object that does the blob detection
    std::vector<cv::KeyPoint>   mKeyPoints; //the center points of the blobs (current, previous, and before that) & how big they are.
    std::vector<cv::KeyPoint>   mPrevKeyPoints; //saves previous key points
    
    cv::Mat                     mCurFrame, mBackgroundSubtracted; //current frame & frame with background subtracted in OCV format
    
    cv::Mat                     mSavedFrame; //the frame saved for use for simple background subtraction
    
    cv::Ptr<cv::BackgroundSubtractor> mBackgroundSubtract; //the background subtraction object that does subtractin'
    
    std::vector<Blob>           mBlobs; ///the blobs found in the current frame
    
    std::vector<int> mMapPrevToCurKeypoints; //index# -> mKeyPoints index#
                                            //index data -> index# of the mPrevKeyPoint
    
    enum BackgroundSubtractionState {NONE=0, OPENCV=2, SAVEDFRAME=3};
    BackgroundSubtractionState mUseBackgroundSubtraction;
    
    void blobDetection(BackgroundSubtractionState useBackground); //does our blob detection
    void createBlobs(); //creates the blob objects for each keypoint
    void blobTracking();
    void updateBlobList();
    
    int newBlobID; //the id to assign a new blob.

    //Sending OSC
    osc::SenderUdp           mSender;
    void sendOSC(std::string addr, float down);
    void sendOSC(std::string addr, float x, float y);
    void sendOSC(std::string addr, float ind, float x, float y);
};

BlobTrackingApp::BlobTrackingApp() : mSender(LOCALPORT, DESTHOST, DESTPORT) //initializing our class variables
{
    
}

void BlobTrackingApp::setup()
{
    //set up our OSC sender and bind it to our local port
    try{
        mSender.bind();
    }
    catch( osc::Exception &e)
    {
        CI_LOG_E( "Error binding" << e.what() << " val: " << e.value() );
        quit();
    }
    
    //set up our camera
    try {
        mCapture = Capture::create(WINDOW_WIDTH, WINDOW_HEIGHT); //first default camera
        mCapture->start();
    }
    catch( ci::Exception &exc)
    {
        CI_LOG_EXCEPTION( "Failed to init capture ", exc );
    }
    //setup the blob detector
    // Setup SimpleBlobDetector parameters.
    cv::SimpleBlobDetector::Params params;
    
    // Change thresholds
    //    params.minThreshold = 10;
    //    params.maxThreshold = 200;
    
    // Filter by Circularity - how circular
    params.filterByCircularity = false;
    params.maxCircularity = 0.2;
    
    // Filter by Convexity -- how convex
    params.filterByConvexity = false;
    params.minConvexity = 0.87;
    
    // Filter by Inertia ?
    params.filterByInertia = false;
    params.minInertiaRatio = 0.01;
    
    params.minDistBetweenBlobs = 100.0f; //WAS ORIGINALLY 300.0f
    
    params.filterByColor = false;
    
    params.filterByArea = true;
    params.minArea = 200.0f; //ORIGINALLY 200.0f
    params.maxArea = 900.0f; //ORIGINALLY 1000.0f
    
    //create the blob detector with the above parameters
    mBlobDetector = cv::SimpleBlobDetector::create(params);
    
    //our first available id is 0
    newBlobID = 0;
    
    //use MOG2 -- guassian mixture algorithm to do background subtraction
    mBackgroundSubtract = cv::createBackgroundSubtractorMOG2();
    
    mUseBackgroundSubtraction = BackgroundSubtractionState::NONE;
    mBackgroundSubtracted.data = NULL;

}



void BlobTrackingApp::sendOSC(std::string addr, float x, float y){
    osc:: Message msg;
    msg.setAddress(addr);

    msg.append(x);
    msg.append(y);

    mSender.send(msg);
}


void BlobTrackingApp::sendOSC(std::string addr, float down){
    osc:: Message msg;
    msg.setAddress(addr);

    msg.append(down);

    mSender.send(msg);
}


void BlobTrackingApp::sendOSC(std::string addr, float ind, float x, float y){
    osc:: Message msg;
    msg.setAddress(addr);

    msg.append(ind);
    msg.append(x);
    msg.append(y);
    
    mSender.send(msg);
}

void BlobTrackingApp::keyDown( KeyEvent event )
{
    if( event.getChar() == '1')
    {
        mUseBackgroundSubtraction = BackgroundSubtractionState::NONE;
    }
    if( event.getChar() == '2')
    {
        mUseBackgroundSubtraction = BackgroundSubtractionState::OPENCV;
    }
    if( event.getChar() == '3')
    {
        mUseBackgroundSubtraction = BackgroundSubtractionState::SAVEDFRAME;
        std::cout << "Saving current frame as background!\n";
        mSavedFrame = mCurFrame;
    }
}

//this function detects the blobs.
void BlobTrackingApp::blobDetection(BackgroundSubtractionState useBackground = BackgroundSubtractionState::NONE)
{
    if(!mSurface) return;
    
    cv::Mat frame;
    
    //using the openCV background subtraction
    if(useBackground == BackgroundSubtractionState::OPENCV)
    {
        mBackgroundSubtract->apply(mCurFrame, mBackgroundSubtracted);
        frame = mBackgroundSubtracted;
    }
    else if( useBackground == BackgroundSubtractionState::SAVEDFRAME )
    {
        if(mSavedFrame.data)
        {
            cv::Mat outFrame;
            
            //use frame-differencing to subtract the background
            cv::GaussianBlur(mCurFrame, outFrame, cv::Size(11,11), 0);
            cv::GaussianBlur(mSavedFrame, mBackgroundSubtracted, cv::Size(11,11), 0);
            cv::absdiff(outFrame, mBackgroundSubtracted, mBackgroundSubtracted);
            
            cv::threshold(mBackgroundSubtracted, mBackgroundSubtracted, 25, 255, cv::THRESH_BINARY);
           
            frame = mBackgroundSubtracted;
        }
        else{
            std::cerr << "No background frame has been saved!\n"; //the way the program is designed, this should happen
        }
    }
    else
    {
        frame = mCurFrame;
    }
    //saving curr mKeyPoints into mPrevKeyPoints
    mPrevKeyPoints.clear();
    mPrevKeyPoints = mKeyPoints;
    
    //note the parameters: the frame that you would like to detect the blobs in - an input frame
    //& 2nd, the output -- a vector of points, the center points of each blob.
    mBlobDetector->detect(frame, mKeyPoints);
}

void BlobTrackingApp::update()
{
    if(mCapture && mCapture->checkNewFrame()) //is there a new frame???? (& did camera get created?)
    {
        mSurface = mCapture->getSurface();
        
        if(! mTexture)
            mTexture = gl::Texture::create(*mSurface);
        else
            mTexture->update(*mSurface);
    }
    if(!mSurface) return; //we do nothing if there is no new frame
    mCurFrame = toOcv(Channel(*mSurface));
  
    //update all our blob info
    blobDetection(mUseBackgroundSubtraction);
    
    
    blobTracking();
    updateBlobList();
    
    
    //SEND OSC HERE VVV
    for(int i = 0 ; i < mBlobs.size() ; i++){
        sendOSC(BLOB_OSCADDRESS,(float)mBlobs[i].getBlobID(), (float)mBlobs[i].getCurrX(), (float)mBlobs[i].getCurrY());
    }
}
    
    



//filling in map
void BlobTrackingApp::blobTracking()
{
    mMapPrevToCurKeypoints.clear();
    
    for(int i = 0 ; i < mKeyPoints.size(); i++)//iteratoring through mKeyPoints[]
    {
        float closestDistance = 100; // 300 from params.minDistBetweenBlobs in setup()
        int indexOfClosestDistance = -1;
        
        for(int j = 0 ; j < mPrevKeyPoints.size(); j++)//iterating through mPrevKeyPoints[]
        {
            //comparing the distances from mKeyPoints[i] to points in mPrevKeyPoints to see which  mPrevKeyPoint is closest to mKeyPoints[i]
                float newDistance = ci::distance(fromOcv(mKeyPoints[i].pt), fromOcv(mPrevKeyPoints[j].pt));
            
                if(newDistance < closestDistance){
                    closestDistance = newDistance;
                    indexOfClosestDistance = j;
                }
        }
        
            mMapPrevToCurKeypoints.push_back(indexOfClosestDistance);//if no close point was found add -1 to map
    }
}


//updating mBlobs<>
void BlobTrackingApp::updateBlobList()
{
    std::vector<Blob> mPrevBlobs = mBlobs;
    mBlobs.clear();

    for(int i = 0 ; i < mMapPrevToCurKeypoints.size() ; i++)
    {
        //if blob wasn't found in prev frame, create a new one
        int data = mMapPrevToCurKeypoints[i];
        if(data == -1){
            mBlobs.push_back(Blob(mKeyPoints[i], newBlobID));
            newBlobID++;
        }
            //else update location of matching blob in prev frame
            else{
                mBlobs.push_back(mPrevBlobs[data]);
                mBlobs[i].update(mKeyPoints[i]);//adding keyPoints[i] to the Blobs KeyPoints vector (the last one is the current key point location)
            }
    }
}


void BlobTrackingApp::createBlobs()
{
    mBlobs.clear(); //create a new list of blobs each time
    newBlobID = 0; //reset here - since we're not keeping track of blobs yet!
    
    for(int i=0; i<mKeyPoints.size(); i++)
    {
        mBlobs.push_back(Blob(mKeyPoints[i], newBlobID));
        newBlobID++;
    }
}

void BlobTrackingApp::draw()
{
    gl::clear( Color( 0, 0, 0 ) );
    
    gl::color( 1, 1, 1, 1 );
  
    //draw what image we are detecting the blobs in
    if( mBackgroundSubtracted.data && mUseBackgroundSubtraction )
    {
        gl::draw( gl::Texture::create(fromOcv(mBackgroundSubtracted)) );
    }
    else if( mTexture )
    {
        gl::draw(mTexture);
    }
    
    //draw the blobs
    for(int i=0; i<mBlobs.size(); i++)
    {
        mBlobs[i].draw();
    }
}





CINDER_APP( BlobTrackingApp, RendererGl,
           []( BlobTrackingApp::Settings *settings ) //note: this part is to fix the display after updating OS X 1/15/18
           {
               settings->setHighDensityDisplayEnabled( true );
               settings->setTitle("Blob Tracking Example");
               settings->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);
               //               settings->setFrameRate(FRAMERATE); //set fastest framerate
               } )

