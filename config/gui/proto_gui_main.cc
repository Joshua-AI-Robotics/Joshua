#include "proto_gui.h"

int main() {
    std::cout << "Starting Project Joshua Proto GUI..." << std::endl;
    
    ProtoGUI gui;
    gui.show();
    
    return Fl::run();
} 