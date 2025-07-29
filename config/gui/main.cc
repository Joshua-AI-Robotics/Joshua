#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <string>

class SimpleGUI {
private:
    cv::Mat display_image_;
    std::string window_name_;
    int slider_value_;
    bool checkbox_value_;
    std::string text_input_;
    
public:
    SimpleGUI() : window_name_("Project Joshua GUI"), slider_value_(50), 
                  checkbox_value_(false), text_input_("Hello, Project Joshua!") {
        // Higher resolution display image
        display_image_ = cv::Mat(900, 1200, CV_8UC3, cv::Scalar(30, 30, 30));
        
        // Create window and trackbars
        cv::namedWindow(window_name_, cv::WINDOW_NORMAL);
        cv::resizeWindow(window_name_, 1200, 900);
        
        // Create trackbars
        cv::createTrackbar("Slider", window_name_, &slider_value_, 100, nullptr);
    }
    
    void update() {
        // Clear the image with a darker background
        display_image_ = cv::Scalar(30, 30, 30);
        
        // Draw GUI elements
        drawText();
        drawButtons();
        drawInfo();
        
        // Show the image
        cv::imshow(window_name_, display_image_);
    }
    
    void drawText() {
        // Title with better positioning and larger font
        cv::putText(display_image_, "Project Joshua Control Panel", 
                   cv::Point(40, 80), cv::FONT_HERSHEY_DUPLEX, 1.5, 
                   cv::Scalar(255, 255, 255), 2);
        
        // Slider value with better formatting
        std::string slider_text = "Slider Value: " + std::to_string(slider_value_);
        cv::putText(display_image_, slider_text, 
                   cv::Point(40, 140), cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                   cv::Scalar(200, 200, 200), 2);
        
        // Checkbox status
        std::string checkbox_text = "Feature Enabled: " + std::string(checkbox_value_ ? "Yes" : "No");
        cv::putText(display_image_, checkbox_text, 
                   cv::Point(40, 200), cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                   cv::Scalar(200, 200, 200), 2);
        
        // Text input
        cv::putText(display_image_, "Text: " + text_input_, 
                   cv::Point(40, 260), cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                   cv::Scalar(200, 200, 200), 2);
    }
    
    void drawButtons() {
        // Click Me button - larger and better positioned
        cv::rectangle(display_image_, cv::Point(40, 320), cv::Point(240, 380), 
                     cv::Scalar(100, 100, 255), -1);
        cv::rectangle(display_image_, cv::Point(40, 320), cv::Point(240, 380), 
                     cv::Scalar(150, 150, 255), 2);
        cv::putText(display_image_, "Click Me!", 
                   cv::Point(80, 355), cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                   cv::Scalar(255, 255, 255), 2);
        
        // Exit button - larger and better positioned
        cv::rectangle(display_image_, cv::Point(280, 320), cv::Point(400, 380), 
                     cv::Scalar(255, 100, 100), -1);
        cv::rectangle(display_image_, cv::Point(280, 320), cv::Point(400, 380), 
                     cv::Scalar(255, 150, 150), 2);
        cv::putText(display_image_, "Exit", 
                   cv::Point(340, 355), cv::FONT_HERSHEY_SIMPLEX, 1.0, 
                   cv::Scalar(255, 255, 255), 2);
    }
    
    void drawInfo() {
        // Better positioned info text
        cv::putText(display_image_, "Press ESC to exit", 
                   cv::Point(40, 800), cv::FONT_HERSHEY_SIMPLEX, 0.7, 
                   cv::Scalar(150, 150, 150), 1);
        
        cv::putText(display_image_, "Click in window to toggle checkbox", 
                   cv::Point(40, 830), cv::FONT_HERSHEY_SIMPLEX, 0.7, 
                   cv::Scalar(150, 150, 150), 1);
        
        cv::putText(display_image_, "Use the slider above to adjust values", 
                   cv::Point(40, 860), cv::FONT_HERSHEY_SIMPLEX, 0.7, 
                   cv::Scalar(150, 150, 150), 1);
    }
    
    void handleMouse(int event, int x, int y, int flags) {
        if (event == cv::EVENT_LBUTTONDOWN) {
            // Check if Click Me button was clicked (updated coordinates)
            if (x >= 40 && x <= 240 && y >= 320 && y <= 380) {
                std::cout << "Button clicked! Slider value: " << slider_value_ << std::endl;
            }
            // Check if Exit button was clicked (updated coordinates)
            else if (x >= 280 && x <= 400 && y >= 320 && y <= 380) {
                cv::destroyAllWindows();
                exit(0);
            }
            // Toggle checkbox
            else {
                checkbox_value_ = !checkbox_value_;
            }
        }
    }
    
    void run() {
        // Set mouse callback
        cv::setMouseCallback(window_name_, 
                           [](int event, int x, int y, int flags, void* userdata) {
                               static_cast<SimpleGUI*>(userdata)->handleMouse(event, x, y, flags);
                           }, this);
        
        while (true) {
            update();
            
            int key = cv::waitKey(30);
            if (key == 27) // ESC key
                break;
        }
        
        cv::destroyAllWindows();
    }
};

int main() {
    std::cout << "Starting Project Joshua GUI..." << std::endl;
    
    SimpleGUI gui;
    gui.run();
    
    return 0;
} 