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
    std::lock_guard<std::mutex> lock(mutex_);
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

std::vector<uint8_t> Serial::Read(size_t bytes_to_read){
    std::lock_guard<std::mutex> lock(mutex_);
    if (!serial_->is_open()) {
        LOG(ERROR) << "Error: Serial port not open for reading.";
        throw std::runtime_error("Serial port not open for reading.");
    }

    std::vector<uint8_t> buffer(bytes_to_read);
    boost::system::error_code ec;
    
    // Set up a deadline timer for the read operation
    boost::asio::steady_timer timer(*io_context_);
    timer.expires_from_now(std::chrono::milliseconds(100)); // 100ms timeout
    timer.async_wait([&](const boost::system::error_code& e) {
        if (!e) { // Timer not cancelled, means timeout occurred
            serial_->cancel(); // Cancel the pending read operation
        }
    });

    size_t bytes_read = 0;
    try {
        bytes_read = boost::asio::read(*serial_, boost::asio::buffer(buffer), ec);
    } catch (const boost::system::system_error& e) {
        LOG(ERROR) << "Error reading from serial port: " << e.what();
        throw std::runtime_error("Error reading from serial port.");
    }

    timer.cancel(); // Cancel the timer if read completes

    if (ec == boost::asio::error::operation_aborted) {
        LOG(ERROR) << "Serial read operation timed out or was cancelled.";
        return {}; // Return empty vector on timeout/cancellation
    }
    else if (ec) {
        LOG(ERROR) << "Error reading from serial port: " << ec.message();
        throw boost::system::system_error(ec, "Read failed");
    }
    
    if (bytes_read != bytes_to_read) {
        LOG(WARNING) << "Read " << bytes_read << " bytes, expected " << bytes_to_read;
    }
    
    return buffer;
}

void Serial::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!serial_->is_open()) {
        LOG(ERROR) << "Error: Serial port not open for flushing.";
        return;
    }
    try {
        serial_->cancel(); // Cancel any pending read operations
        // Read and discard any remaining bytes in the buffer
        while (true) {
            char c;
            boost::system::error_code ec;
            boost::asio::read(*serial_, boost::asio::buffer(&c, 1), ec);
            if (ec == boost::asio::error::eof || ec == boost::asio::error::would_block) {
                break; // No more data to read
            } else if (ec) {
                LOG(ERROR) << "Error flushing serial port: " << ec.message();
                break;
            }
        }
    } catch (const boost::system::system_error& e) {
        LOG(ERROR) << "Error flushing serial port: " << e.what();
    }
}

}
