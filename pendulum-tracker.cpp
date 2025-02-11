/*
@file pendulum-tracker.cpp (Pendulumv30's child)
@brief An attempt to recognize and to track a generic moving object, and then save its temporal position.
@author arantxax
@date Feb 03, 2020
*/
 
#include "opencv2/highgui/highgui.hpp" 
#include "opencv2/imgproc/imgproc.hpp" 
#include "opencv2/video.hpp" 
#include "opencv2/videoio.hpp" 
#include "opencv2/imgcodecs.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/core.hpp"

#include <iostream> 
#include <iomanip>
#include <time.h>
#include <unistd.h>
#include <cmath>
#include <fstream>

using namespace cv; 
using namespace std;

#define PI 3.14159265
#define diameter 0.07625

#define mass 1.4
#define weight 15.5
#define gravity 9.78760

Mat gray_blur(Mat image){
    Mat final_image;
    cvtColor(image, final_image, COLOR_RGB2GRAY);
    blur (final_image, final_image, Size(3,3));
    return final_image;
}

int main(int argc, char** argv){  
    //![Check entry]
    if (argc != 2)
    {
        cout << "usage: " << argv[0] << " <Video Path>" << endl;
        return -1;
    }
    //![Load source video]
    // when the input is a camera, set zero intead of string
    VideoCapture capture(argv[1]);
    if (!capture.isOpened())// check if we succeeded 
    {
        cout <<"Error opening video stream or file"<< endl;
        return -1; 
    }
    int frame_width = static_cast<int>(capture.get(CAP_PROP_FRAME_WIDTH));
    int frame_height = static_cast<int>(capture.get(CAP_PROP_FRAME_HEIGHT));
    Size frame_size(frame_width, frame_height);
    double fps = capture.get(CAP_PROP_FPS);
    cout << "Frame size: " << frame_size << endl;
    cout << "FPS: " << fps << endl;
    //![Load source video]
    
    //![Declare variables to store the frames]
    Mat frame1, frame2, difframe;
    Mat canny_output, roi, hsv, hsv_roi, mask;
    Mat frame, foreground, foregroundMask, background, dst, extra;
    extra = imread("Equation100x60.png");
    //![Declare variables to store the frames]

    //![Object Detection]
    //no prior knowledge of the streaming: a difference can be a tip
    //Take first two frames of the video
    capture >> frame1;
    capture >> frame2;
    absdiff(gray_blur(frame1), gray_blur(frame2), difframe);
    threshold(difframe, difframe, 50, 255, THRESH_BINARY);

    //Detect edges using canny
    int thresh = 100;
    vector<vector<Point>> contours;
    Canny(difframe, canny_output, thresh, thresh*2, 3 );
    
    //Find contours
    findContours(canny_output, contours, RETR_TREE, CHAIN_APPROX_SIMPLE,Point(0,0));  
    if (contours.size()==0)//check for recognition
    {
        cout <<"Didn't find any contour"<< endl;
        return -1; 
    }
    vector<vector<Point>> contours_poly(contours.size());
    vector<Point2f> centers (contours.size());
    vector<float> radius(contours.size());
        
    //Erase noise by choosing the greatest object and draw a circle to identify the moving body
    int id = 0;
    double area = 0.0, max_area = 0.0;
    for(size_t i = 0; i < contours.size(); i++ )
    {
        approxPolyDP( contours[i], contours_poly[i], 3, true );
        minEnclosingCircle( contours_poly[i], centers[i], radius[i]);
        area = contourArea(contours[i], false);
        id = area > max_area ? i : id;
        max_area = max(max_area, area );
    }

    //Physical information in pixels
    Point center = centers[id];
    double scale = diameter/(2*radius[id]);
    cout << "raio: " << radius[id] << endl;
    //![Object Detection]
    
    //![Setup ROI for tracking]
    //Region of Interest's location = Object's location
    Point offset = Point (2*radius[id], 2*radius[id]);
    Rect track_window (center - offset, center + offset);
    roi = frame2(track_window);
    cvtColor(roi, hsv_roi, COLOR_BGR2HSV);
    inRange(hsv_roi, Scalar(0, 60, 32), Scalar(180, 255, 255), mask); //low light values are discarded
    
    //set up ROI histogram
    float range_[] = {0, 180};
    const float* range[] = {range_};
    Mat roi_hist;
    int histSize[] = { 180 };
    int channels[] = { 0 };
    calcHist(&hsv_roi, 1, channels, mask, roi_hist, 1, histSize, range);
    normalize(roi_hist, roi_hist, 0, 255, NORM_MINMAX); 
    //Setup the termination criteria, either 10 iteraction or move by at least 1 pt
    TermCriteria term_crit(TermCriteria::EPS | TermCriteria::COUNT, 10, 1.0);
    //![Setup ROI for tracking]
    
    //![Background Subtraction Method]
    Ptr<BackgroundSubtractor> model; 
    model = createBackgroundSubtractorMOG2();
    bool doUpdateModel = true; 
    bool doSmoothMask = true;
    //![Background Subtraction Method]
    
    //![General handling] 
    int nFrame = 0;
    int i = 0; //general counter
    int criteria = 2; //2 by default
    //![General handling]
    
    //![Declare Physics' variables]    
    int64 start; //for general time counting
    start = getTickCount();  
    //elapsed time
    TickMeter tm; //in the time of measurement
    tm.start();
    double dt = 0.0;
    double elapsed_time = 0.0;
    
    //position
    int size = 3; //3 by default
    vector<float> X(size), Y(size);
    
    //velocity
    double vx = 0.0;
    double vy = 0.0;
    
    //acceleration
    double ax = 0.0;
    double ay = 0.0;
    
    //Rope's length
    double L = 0.0;
    double theta = 0.0;
    //![Declare Physics' variables]
    
    //![Vector analysis]
    Vec2d tangencial {0.0, 0.0};
    Vec2d radial {0.0, 0.0};
    Vec2d tangencial_unit {0.0, 0.0};
    Vec2d radial_unit {0.0, 0.0};
    Vec2d a {0.0, 0.0};
    Vec2d Tm {0.0,0.0};
    Vec2d g {0.0,gravity};
    Vec2d a_radial {0.0, 0.0};
    Vec2d a_tangencial {0.0, 0.0};
    Vec2d y_axis {0,1};
    //![Vector analysis]
    
    //![Cartesian lines and MMQ]
    int N=0;
    double x0 = 0.0, y0 = 0.0; 	//joint point
    Point p0;    			//joint point
    double m = 0.0, b = 0.0; 		//slope and intercept
    double sum_m = 0.0, sum_b = 0.0;
    double sum_m2 = 0.0, sum_mb = 0.0; //sum of squared m and sum of product m*b
    double sum2_m = 0.0; 		//squared sum of m
    //![Cartesian lines and MMQ]
    
    //![Declare entities to draw]
    double xmin = 0.0;
    double ymin = 0.0;
    double roi_width = 0.0;
    double roi_height = 0.0;
    Point a_t, a_r;
    Point velocity;
    //![Declare entities to draw]
    
    //![Initialize Videowriter]
    VideoWriter video("./output.avi", VideoWriter::fourcc('M','J','P','G'), fps, frame_size, true);
    //![Initialize Videowriter]

    //![Initialize export to data file]
    ofstream outputfile;
    outputfile.open ("output.dat");
    outputfile << "t" << "\t" //<< "dt" << "\t" << "nFrame" << "\t"
    << "x[size/2]" <<  "\t" << "Y[size/2]"  << "\t" 
    <<  "vx" << "\t" << "vy" << "\t" 
    <<  "ax" << "\t" << "ay" <<  endl;
/*    << "L_rope" << "\t" << "L_cp" << "\t"
    << "theta_y" << "\t" << "theta_EDO" << endl;
   << "a_radial" << "\t" << "a_tang" << endl;  
*/    //![Initialize export to data file]
    
    //![Loop over all frames]
    while (true)
    { 
        //![input frames-end of capture]
        capture >> frame;
        if (frame.empty()){
            break;
        }
        //![input frames-end of capture] 
        
        //![Background Subtraction Model]
        // update the background model
        model->apply(frame, foregroundMask, doUpdateModel ? -1 : 0); 

        // show foreground image and mask (with optional smoothing)
        if (doSmoothMask)
        {
            GaussianBlur(foregroundMask, foregroundMask, Size(11, 11), 3.5, 3.5);
            threshold(foregroundMask, foregroundMask, 200, 255, THRESH_BINARY);
        } 
        
        if (foreground.empty()){
            foreground.create(frame_size, frame.type());
        }
        
        foreground = Scalar::all(0);
        frame.copyTo(foreground, foregroundMask);
        
        // show background image
        model->getBackgroundImage(background);
        if (!background.empty()) {
            //imshow("mean background image", background);
        }
        //![Background Subtraction Model]
        
        //![Meanshift tracking]
        //ROI Updated
        cvtColor(foreground, hsv, COLOR_BGR2HSV);
        calcBackProject(&hsv, 1, channels, roi_hist, dst, range);
        //apply meanshift to get the new location 
        meanShift(dst, track_window, term_crit); //Finds an object on a back projection image
        xmin = (double) track_window.x;
        ymin = (double) track_window.y;
        roi_width = (double) track_window.width;
        roi_height = (double) track_window.height;
        //![Meanshift tracking]    
              
        if (nFrame % criteria == 0){ //throw away adjacent frames
             cout << "current frame is: " << nFrame << endl;
            //![Physics]
            //![Elapsed time]
            //general time counting
            elapsed_time = (double) (getTickCount() - start) *1.0f/ getTickFrequency();
            cout << "\nelapsed time: " << format("%9.4f s", elapsed_time) << endl;
            //![Elapsed time]
            
            //![Position]
            X[i] = xmin + roi_width/2;
            Y[i] = ymin + roi_height/2;
            //![Position]
            
            //![Elapsed time]
            //dt > 0 (avoid division by zero)
            tm.stop();
            dt = tm.getTimeSec();
            //![Elapsed time]
            
            //Enough data to proceed further calculation
            if (i == size-1){ //full vector        

            	if (size == 5){
            	    //five points centered at i=2
            	    //![Velocity]
            	    vx = (1*X[0] - 8*X[1] + 0*X[2] + 8*X[3] - 1*X[4])*scale/(12*dt);
            	    vy = (1*Y[0] - 8*Y[1] + 0*Y[2] + 8*Y[3] - 1*Y[4])*scale/(12*dt);
            	    //![Velocity]
            	    
            	    //![Acceleration]
            	    //Using second derivative using middle point method
            	    ax = (-X[0] + 16*X[1] - 30*X[2] + 16*X[3] - X[4])*scale/(12*dt*dt);
            	    ay = (-Y[0] + 16*Y[1] - 30*Y[2] + 16*Y[3] - Y[4])*scale/(12*dt*dt);
            	    //![Acceleration]
            	}
            	if (size == 3){
            	   //three points centered at i=1
            	   //![Velocity]
            	   vx= (-1*X[0]+0*X[1]+1*X[2])*scale/(2*dt);
            	   vy= (-1*Y[0]+0*Y[1]+1*Y[2])*scale/(2*dt);
            	   //![Velocity]
            	   
            	   //![Acceleration]
            	   ax = (1*X[0]-2*X[1]+1*X[2])*scale/(1*dt*dt);
            	   ay = (1*Y[0]-2*Y[1]+1*Y[2])*scale/(1*dt*dt);
            	   //![Acceleration]
            	}    	
        	//![Physics]
        	
        	//![write information to a file]
        	outputfile <<
        	format("%9.4f", elapsed_time) //<< "\t" << format("%9.4f", dt) << "\t" << nFrame
        	<< "\t"  << X[size/2] <<  "\t" << Y[size/2] 
        	<< "\t" <<  vx << "\t" << vy 
        	<< "\t" <<  ax << "\t" << ay <<  endl;
        	//![write information to a file]  
        	    	
        	//![Cartesian Line]
        	//We would like to determine the joint point using MMQ
        	//We do know the velocity's orientation, which is tangencial to the circular path
        	//Then the circle's radius is perpendicular to the velocity
        	m = - 1/ (vy/vx);	        //perpendicular slope
        	b = Y[size/2] - m*X[size/2];	//intercept
        	
        	//What we have in hands: 	m_i and b_i
        	//What we want to determine:	y and x
        	//MMQ method for this case: sum(y - m_i *x - b)^2 = 0
        	sum_m =+ m;
        	sum_b =+ b;
        	sum_m2 =+ m*m;
        	sum_mb =+ m*b;
        	sum2_m =+ sum_m*sum_m;
        	N++;  
        	//![Cartesian Line]
        	
        	
        	//![Joint point]
        	if (N * sum_m2 - sum2_m != 0 ){
        	   x0 = -(N*sum_mb     - sum_m*sum_b)  / (N * sum_m2 - sum2_m);
        	   y0 =  (sum_m2*sum_b - sum_mb*sum_m) / (N * sum_m2 - sum2_m);
        	   if (x0 == x0 || y0 == y0){//Nan interesting facts
        	   	p0 = Point (x0, y0);
        	   }
        	   else{
        	   	p0 = Point (X[size/2], 0);
        	   }
        	}
        	//![Joint point]
        	
        	//![Data's update]
        	for (int n=0; n < size-1; n++){
            		X[n]=X[n+1];
            		Y[n]=Y[n+1];
        	}
        	//reset to last index and overwrite content
        	i = size-2;      	
            }
            i++;
            //![Data's update]

            //![Vector analysis]      	
            //tangencial and radial directions
            tangencial = {vx, vy};
            radial = {center.x-(double)p0.x, center.y-(double)p0.y};
            cout << "radial " << radial << endl;
            if (norm(tangencial)!=0 && norm(radial)!=0){ //avoid division by zero
            	tangencial_unit = tangencial/norm(tangencial); //NORM_L2
            	radial_unit = radial/norm(radial);   //NORM_L2
            	
            	//They are perpendicular when tangencial_unit.dot(radial_unit)==0 
            	//cout << "Check perpendicularity: " << tangencial_unit.dot(radial_unit) << endl;
            	
            	//Acceleration projection in radial and transversal directions
            	a = {ax, ay};
            	a_tangencial = a.dot(tangencial_unit) * tangencial_unit;
            	a_radial     = a.dot(radial_unit)     * radial_unit;
            	cout << "Acceleration radial " << a_radial << endl;
            	if (a_radial[1]>0){
            		a_radial = {0, 0};
            	}
            	
            	//Tension/m = gcos(theta) + a_radial
                Tm = g.dot(radial_unit)*radial_unit + a_radial;
                
                //Tension/m = gcos(theta) + v^2/L
                L = pow(norm(tangencial),2)/(norm(a_radial)) ;
                theta = acos(radial_unit.dot(y_axis)/norm(y_axis)*norm(radial_unit));
                Tm = norm(tangencial)/L + gravity*cos(theta);
                

            }
            //![Vector analysis]   
            
		//![write information to a file]
/*		outputfile << format("%9.4f", elapsed_time) << "\t" 
               //Rope lenght (L) NORM_L2
               << norm(radial)*scale << "\t"
               //Rope lenght (L) v^2/a_radial
               << pow(norm(tangencial),2)/(norm(a_radial)) << "\t";
               //Rope lenght (L) period = 2*pi*sqrt(L/g) = 2.02 s
               L = gravity*pow(2.02,2)/pow(2*PI,2);
               //Theta angle (theta) Angle between rope and y axis
               outputfile  << acos(radial_unit.dot(y_axis)/norm(y_axis)*norm(radial_unit))*180.0/PI 
               //Theta angle (theta) 2th order EDO: a_tangencial = - omega^2 * theta = - g/L * theta
               << "\t" << L*norm(a_tangencial)/gravity*180.0/PI << endl;
             << norm (a_radial) << "\t" << norm (a_tangencial) << endl;
*/              //![write information to a file] 

        }
        nFrame++;
        tm.reset(); //reset cronometer
        tm.start();
        //![Data's update]
                   
        //![Drawing]
        //show the image with a point mark at the centroid
        center = Point (xmin + roi_width/2, ymin + roi_height/2);
        //circle(foreground,center,5,Scalar(0,240,255),-1); //BGR
        rectangle(foreground, track_window, Scalar (0,240,255),2);
        
        //Rescale before drawing
        float scaled_cols = roi_width/frame_width * (1/scale);
        
        //show the image with the velocity vector's result
        Point velocity = Point (tangencial*scaled_cols);
        arrowedLine (frame, center, center + velocity, Scalar(255, 255, 255), 3, 8);
        
        //show the image with tangencial acceleration's result
        a_t = Point(a_tangencial*scaled_cols);
        a_r = Point (a_radial*scaled_cols);
        arrowedLine (frame, center, center + a_t, Scalar(0, 0, 255), 3, 8);
        //arrowedLine (frame, center, center + a_r, Scalar(0, 255, 0), 3, 8);
        
        //get the frame number and write it on the current frame
        rectangle(frame, Point(10, 2), Point(200,20), Scalar(255,255,255), -1);
        string label = format("elapsed time: %.2f s", elapsed_time);
        putText(frame, label, Point(15, 15),FONT_HERSHEY_SIMPLEX, 0.5 , Scalar(0,0,0));
        video.write(frame);
        
        //Show label
        rectangle(frame, Point(frame_width/2, 10), Point(frame_width,50), Scalar(120,120,120), -1);
        circle(frame,Point (frame_width/2+10,20), 5,Scalar(255,255,255),-1); //BGR
        putText(frame, "Velocidade", Point(frame_width/2+20, 25),FONT_HERSHEY_SIMPLEX, 0.5 , Scalar(255,255,255));
        circle(frame, Point (frame_width/2+10,40),5,Scalar(0,0,255),-1); //BGR
        putText(frame, "Aceleracao restauradora", Point(frame_width/2+20, 45),FONT_HERSHEY_SIMPLEX, 0.5 , Scalar(0,0,255));
        
        //show pendulum's period equation
        Rect board(Point(10, 20), Size(extra.size()));
        Mat dst = frame(board);
        extra.copyTo(dst);
        
/*        //show miniaturized joint point, rope and angle
        rectangle(frame, Point(10, 20), Point(200,150), Scalar(255,255,255), -1);
        line (frame, Point (80,20), Point (80,20) + Point(100*radial_unit),
        Scalar(0, 255, 0), 2, LINE_AA, 0);
        line (frame, Point (80,20), Point (80,150), Scalar(0, 0, 0), 1, LINE_AA, 0);
        if (radial_unit[0]<0){
        	ellipse (frame, Point (80,20), Size(50,50), 90, 0, 
        	-gravity*norm(a_tangencial)/L*180.0/PI,
        	Scalar(0, 255, 0), 1, LINE_8, 0) ;
        }else{
        	ellipse (frame, Point (80,20), Size(50,50), 90, 0, 
        	gravity*norm(a_tangencial)/L*180.0/PI,
        	Scalar(0, 255, 0), 1, LINE_8, 0) ;
        }*/

        imshow("foreground mask", foregroundMask);
        imshow("foreground image", foreground);
        imshow("frame", frame);
        int keyboard = waitKey(30); 
        if (keyboard == 'q' || keyboard == 27)
            break;
        //![Drawing]
    }
    //![Loop over all frames]
    outputfile.close();
    return 0;
}