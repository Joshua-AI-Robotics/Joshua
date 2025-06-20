#include "robot/comm_interface/serial/serial.h"

namespace robot::comm_interface{
Serial::Serial(std::shared_ptr<boost::asio::io_context> io, std::string uart_port, int uart_baudrate):
    io_context_(io), uart_port_(uart_port), uart_baudrate_(uart_baudrate)
    {
        try {
            serial_ = std::make_unique<boost::asio::serial_port>(*io_context_, uart_port_);

            serial_->set_option(boost::asio::serial_port_base::baud_rate(uart_baudrate));
            serial_->set_option(boost::asio::serial_port_base::character_size(8));
            serial_->set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
            serial_->set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
            serial_->set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
        } catch (const boost::system::system_error& e) {
            LOG(ERROR) << e.what();
            throw std::runtime_error("Error opening serial port.");
        }
    }

Serial::~Serial() {
    if (serial_->is_open()) {
        try {
            serial_->close();
        } catch (const boost::system::system_error& e) {
            LOG(ERROR) << "Error closing serial port: " << e.what();
            throw std::runtime_error("Error closing serial port.");
        }
    }
}

void Serial::Write(const std::vector<uint8_t>& data){
    std::lock_guard<std::mutex> lock(mutex_); // Acquire lock
    if (!serial_->is_open()) {
        LOG(ERROR) << "Error: Serial port not open for writing.";
        throw std::runtime_error("Serial port not open for writing.");
    }
    try {
        boost::asio::write(*serial_, boost::asio::buffer(data));
    } catch (const boost::system::system_error& e) {
        LOG(ERROR) << "Error writing to serial port: " << e.what();
        throw std::runtime_error("Error writing to serial port.");
    }
}

std::string Serial::Read(){
    std::lock_guard<std::mutex> lock(mutex_); // Acquire lock
    if (!serial_->is_open()) {
        LOG(ERROR) << "Error: Serial port not open for reading.";
        throw std::runtime_error("Serial port not open for reading.");
    }
    try {
        boost::asio::streambuf buffer;
        boost::asio::read_until(*serial_, buffer, '\n'); // Read until newline or choose a fixed size
        std::istream is(&buffer);
        std::string s;
        std::getline(is, s);
        return s;
    } catch (const boost::system::system_error& e) {
        LOG(ERROR) << "Error reading from serial port: " << e.what();
        throw std::runtime_error("Error reading from serial port.");
    }
    throw std::runtime_error("Read function reached end without return or throw."); // Should be unreachable
}

}
